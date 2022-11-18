// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2017 NXP
 * Copyright (C) 2018 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#define CTRL_STRIDE_OFF(_t, _r)	(_t * 4 * _r)
#define CHANCTRL		0x0
#define CHANMASK(n, t)		(CTRL_STRIDE_OFF(t, 0) + 0x4 * (n) + 0x4)
#define CHANSET(n, t)		(CTRL_STRIDE_OFF(t, 1) + 0x4 * (n) + 0x4)
#define CHANSTATUS(n, t)	(CTRL_STRIDE_OFF(t, 2) + 0x4 * (n) + 0x4)
#define CHAN_MINTDIS(t)		(CTRL_STRIDE_OFF(t, 3) + 0x4)
#define CHAN_MASTRSTAT(t)	(CTRL_STRIDE_OFF(t, 3) + 0x8)

#define CHAN_MAX_OUTPUT_INT	0x8

struct irqsteer_data {
	struct irq_chip		chip;
	void __iomem		*regs;
	struct clk		*ipg_clk;
	int			irq[CHAN_MAX_OUTPUT_INT];
	int			irq_count;
	raw_spinlock_t		lock;
	int			reg_num;
	int			channel;
	struct irq_domain	*domain;
	u32			*saved_reg;
	bool			inited;

	struct device		*dev;
	struct device		*pd_csi;
	struct device		*pd_isi;
};

static int imx_irqsteer_get_reg_index(struct irqsteer_data *data,
				      unsigned long irqnum)
{
	return (data->reg_num - irqnum / 32 - 1);
}

static int imx_irqsteer_attach_pd(struct irqsteer_data *data)
{
	struct device *dev = data->dev;
	struct device_link *link;

	data->pd_csi = dev_pm_domain_attach_by_name(dev, "pd_csi");
	if (IS_ERR(data->pd_csi ))
		return PTR_ERR(data->pd_csi);
	else if (!data->pd_csi)
		return 0;

	link = device_link_add(dev, data->pd_csi,
			DL_FLAG_STATELESS |
			DL_FLAG_PM_RUNTIME);
	if (IS_ERR(link))
		return PTR_ERR(link);

	data->pd_isi = dev_pm_domain_attach_by_name(dev, "pd_isi_ch0");
	if (IS_ERR(data->pd_isi))
		return PTR_ERR(data->pd_isi);
	else if (!data->pd_isi)
		return 0;

	link = device_link_add(dev, data->pd_isi,
			DL_FLAG_STATELESS |
			DL_FLAG_PM_RUNTIME);
	if (IS_ERR(link))
		return PTR_ERR(link);

	return 0;
}

static void imx_irqsteer_irq_unmask(struct irq_data *d)
{
	struct irqsteer_data *data = d->chip_data;
	int idx = imx_irqsteer_get_reg_index(data, d->hwirq);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&data->lock, flags);
	val = readl_relaxed(data->regs + CHANMASK(idx, data->reg_num));
	val |= BIT(d->hwirq % 32);
	writel_relaxed(val, data->regs + CHANMASK(idx, data->reg_num));
	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static void imx_irqsteer_irq_mask(struct irq_data *d)
{
	struct irqsteer_data *data = d->chip_data;
	int idx = imx_irqsteer_get_reg_index(data, d->hwirq);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&data->lock, flags);
	val = readl_relaxed(data->regs + CHANMASK(idx, data->reg_num));
	val &= ~BIT(d->hwirq % 32);
	writel_relaxed(val, data->regs + CHANMASK(idx, data->reg_num));
	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static struct irq_chip imx_irqsteer_irq_chip = {
	.name		= "irqsteer",
	.irq_mask	= imx_irqsteer_irq_mask,
	.irq_unmask	= imx_irqsteer_irq_unmask,
};

static int imx_irqsteer_irq_map(struct irq_domain *h, unsigned int irq,
				irq_hw_number_t hwirq)
{
	struct irqsteer_data *irqsteer_data = h->host_data;

	irq_set_status_flags(irq, IRQ_LEVEL);
	irq_set_chip_data(irq, h->host_data);
	irq_set_chip_and_handler(irq, &irqsteer_data->chip, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops imx_irqsteer_domain_ops = {
	.map		= imx_irqsteer_irq_map,
	.xlate		= irq_domain_xlate_onecell,
};

static int imx_irqsteer_get_hwirq_base(struct irqsteer_data *data, u32 irq)
{
	int i;

	for (i = 0; i < data->irq_count; i++) {
		if (data->irq[i] == irq)
			return i * 64;
	}

	return -EINVAL;
}

static void imx_irqsteer_irq_handler(struct irq_desc *desc)
{
	struct irqsteer_data *data = irq_desc_get_handler_data(desc);
	int hwirq;
	int irq, i;

	chained_irq_enter(irq_desc_get_chip(desc), desc);

	irq = irq_desc_get_irq(desc);
	hwirq = imx_irqsteer_get_hwirq_base(data, irq);
	if (hwirq < 0) {
		pr_warn("%s: unable to get hwirq base for irq %d\n",
			__func__, irq);
		return;
	}

	for (i = 0; i < 2; i++, hwirq += 32) {
		int idx = imx_irqsteer_get_reg_index(data, hwirq);
		unsigned long irqmap;
		int pos;

		if (hwirq >= data->reg_num * 32)
			break;

		irqmap = readl_relaxed(data->regs +
				       CHANSTATUS(idx, data->reg_num));

		for_each_set_bit(pos, &irqmap, 32)
			generic_handle_domain_irq(data->domain, pos + hwirq);
	}

	chained_irq_exit(irq_desc_get_chip(desc), desc);
}

#ifdef CONFIG_PM_SLEEP
static int imx_irqsteer_chans_enable(struct irqsteer_data *data)
{
	return 0;
}
#else
static int imx_irqsteer_chans_enable(struct irqsteer_data *data)
{
	int ret;

	ret = clk_prepare_enable(irqsteer_data->ipg_clk);
	if (ret) {
		dev_err(data->dev, "failed to enable ipg clk: %d\n", ret);
		return ret;
	}

	/* steer all IRQs into configured channel */
	writel_relaxed(BIT(data->channel), data->regs + CHANCTRL);

	/* read back CHANCTRL register cannot reflact on HW register
	 * real value due to the HW action, so add one flag here.
	 */
	data->inited = true;
	return 0;
}
#endif

static int imx_irqsteer_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct irqsteer_data *data;
	u32 irqs_num;
	int i, ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->chip = imx_irqsteer_irq_chip;
	data->chip.parent_device = &pdev->dev;
	data->dev = &pdev->dev;
	data->inited = false;
	data->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->regs)) {
		dev_err(&pdev->dev, "failed to initialize reg\n");
		return PTR_ERR(data->regs);
	}

	data->ipg_clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(data->ipg_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(data->ipg_clk),
				     "failed to get ipg clk\n");

	ret = imx_irqsteer_attach_pd(data);
	if (ret < 0 && ret == -EPROBE_DEFER)
		return ret;

	ret = device_reset(&pdev->dev);
	if (ret == -EPROBE_DEFER)
		return ret;

	raw_spin_lock_init(&data->lock);

	ret = of_property_read_u32(np, "fsl,num-irqs", &irqs_num);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "fsl,channel", &data->channel);
	if (ret)
		return ret;

	/*
	 * There is one output irq for each group of 64 inputs.
	 * One register bit map can represent 32 input interrupts.
	 */
	data->irq_count = DIV_ROUND_UP(irqs_num, 64);
	data->reg_num = irqs_num / 32;

	if (IS_ENABLED(CONFIG_PM_SLEEP)) {
		data->saved_reg = devm_kzalloc(&pdev->dev,
					sizeof(u32) * data->reg_num,
					GFP_KERNEL);
		if (!data->saved_reg)
			return -ENOMEM;
	}

	ret = imx_irqsteer_chans_enable(data);
	if (ret)
		return ret;

	data->domain = irq_domain_add_linear(np, data->reg_num * 32,
					     &imx_irqsteer_domain_ops, data);
	if (!data->domain) {
		dev_err(&pdev->dev, "failed to create IRQ domain\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!data->irq_count || data->irq_count > CHAN_MAX_OUTPUT_INT) {
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < data->irq_count; i++) {
		data->irq[i] = irq_of_parse_and_map(np, i);
		if (!data->irq[i]) {
			ret = -EINVAL;
			goto out;
		}

		irq_set_chained_handler_and_data(data->irq[i],
						 imx_irqsteer_irq_handler,
						 data);
	}

	platform_set_drvdata(pdev, data);

	pm_runtime_enable(&pdev->dev);
	return 0;
out:
	clk_disable_unprepare(data->ipg_clk);
	return ret;
}

static int imx_irqsteer_remove(struct platform_device *pdev)
{
	struct irqsteer_data *irqsteer_data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < irqsteer_data->irq_count; i++)
		irq_set_chained_handler_and_data(irqsteer_data->irq[i],
						 NULL, NULL);

	irq_domain_remove(irqsteer_data->domain);

	return pm_runtime_force_suspend(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static void imx_irqsteer_init(struct irqsteer_data *data)
{
	/* steer all IRQs into configured channel */
	writel_relaxed(BIT(data->channel), data->regs + CHANCTRL);

	/* read back CHANCTRL register cannot reflact on HW register
	 * real value due to the HW action, so add one flag here.
	 */
	data->inited = true;
}

static void imx_irqsteer_save_regs(struct irqsteer_data *data)
{
	int i;

	for (i = 0; i < data->reg_num; i++)
		data->saved_reg[i] = readl_relaxed(data->regs +
						CHANMASK(i, data->reg_num));
}

static void imx_irqsteer_restore_regs(struct irqsteer_data *data)
{
	int i;

	writel_relaxed(BIT(data->channel), data->regs + CHANCTRL);
	for (i = 0; i < data->reg_num; i++)
		writel_relaxed(data->saved_reg[i],
			       data->regs + CHANMASK(i, data->reg_num));
}

static int imx_irqsteer_runtime_suspend(struct device *dev)
{
	struct irqsteer_data *irqsteer_data = dev_get_drvdata(dev);

	imx_irqsteer_save_regs(irqsteer_data);
	clk_disable_unprepare(irqsteer_data->ipg_clk);

	return 0;
}

static int imx_irqsteer_runtime_resume(struct device *dev)
{
	struct irqsteer_data *irqsteer_data = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(irqsteer_data->ipg_clk);
	if (ret) {
		dev_err(dev, "failed to enable ipg clk: %d\n", ret);
		return ret;
	}

	/* don't need restore registers when first sub_irq requested */
	if (!irqsteer_data->inited)
		imx_irqsteer_init(irqsteer_data);
	else
		imx_irqsteer_restore_regs(irqsteer_data);

	return 0;
}
#endif

static const struct dev_pm_ops imx_irqsteer_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				      pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(imx_irqsteer_runtime_suspend,
			   imx_irqsteer_runtime_resume, NULL)
};

static const struct of_device_id imx_irqsteer_dt_ids[] = {
	{ .compatible = "fsl,imx-irqsteer", },
	{},
};
MODULE_DEVICE_TABLE(of, imx_irqsteer_dt_ids);

static struct platform_driver imx_irqsteer_driver = {
	.driver = {
		.name = "imx-irqsteer",
		.of_match_table = imx_irqsteer_dt_ids,
		.pm = &imx_irqsteer_pm_ops,
	},
	.probe = imx_irqsteer_probe,
	.remove = imx_irqsteer_remove,
};
module_platform_driver(imx_irqsteer_driver);
MODULE_LICENSE("GPL v2");
