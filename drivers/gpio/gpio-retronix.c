#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include "gpiolib.h"

extern int gpio_export_with_name(unsigned gpio, bool direction_may_change, const char *name);

static int rtx_gio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cnp;
	u32 val;
	u32 delay;
	int nb = 0;

	for_each_child_of_node(np, cnp) {
		const char *name = NULL;
		int gpio;
		bool dmc;
		int max_gpio = 1;
		int i;

		of_property_read_string(cnp, "gpio-export,name", &name);

		if (!name)
			max_gpio = of_gpio_count(cnp);

		for (i = 0; i < max_gpio; i++) {
			gpio = of_get_gpio(cnp, i);
			if (devm_gpio_request(&pdev->dev, gpio, name ? name : of_node_full_name(cnp)))
				continue;

			if (!of_property_read_u32(cnp, "gpio-export,output", &val))
			{
				if (!of_property_read_u32(cnp, "gpio-export,delay", &delay))
				{
					gpio_direction_output(gpio, !val);
					msleep( delay ) ;
				}
				gpio_direction_output(gpio, val);
			}
			else
			{
				gpio_direction_input(gpio);
			}

			dmc = of_property_read_bool(cnp, "gpio-export,direction_may_change");
			
			dev_info(&pdev->dev, "%s(%d) gpio exported\n", name ? name : of_node_full_name(cnp),gpio);
			
			gpio_export_with_name(gpio, dmc, name);
			
			nb++;
		}
	}

	dev_info(&pdev->dev, "%d gpio(s) exported\n", nb);

	return 0;
}

static int rtx_gio_remove(struct platform_device *pdev)
{
	int ret;


	return 0;
}

static struct of_device_id gpio_export_ids[] = {
	{ .compatible = "gpio-export" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gpio_export_ids);

static struct platform_driver rtx_gio_device_driver = {
	.probe		= rtx_gio_probe,
	.remove		= rtx_gio_remove,
	.driver		= {
		.name	= "gpio-export",
		.of_match_table = gpio_export_ids,
		.owner		= THIS_MODULE,
	}
};

static int __init rtx_gio_init(void)
{
	return platform_driver_register(&rtx_gio_device_driver);
}
postcore_initcall(rtx_gio_init);

static void __exit rtx_gio_exit(void)
{
	platform_driver_unregister(&rtx_gio_device_driver);
}
module_exit(rtx_gio_exit);

MODULE_AUTHOR("Tom Wang");
MODULE_DESCRIPTION("RTX GIO Driver");
MODULE_LICENSE("GPL v2");

