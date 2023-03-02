// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2012 Freescale Semiconductor, Inc.
// Copyright 2012 Linaro Ltd.

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/control.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/gpio.h>

#include "../codecs/sgtl5000.h"
#include "imx-audmux.h"

#define DAI_NAME_SIZE	32

struct imx_sgtl5000_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
	char codec_dai_name[DAI_NAME_SIZE];
	char platform_name[DAI_NAME_SIZE];
	struct clk *codec_clk;
	unsigned int clk_frequency;
};

struct imx_priv {
	enum of_gpio_flags hp_active_low;
	enum of_gpio_flags mic_active_low;
	bool is_headset_jack;
	int amp_gpio;
	struct snd_kcontrol *headphone_kctl;
	struct platform_device *pdev;
	struct platform_device *asrc_pdev;
	struct snd_card *snd_card;
};

static struct imx_priv card_priv;

static struct snd_soc_jack imx_hp_jack;
static struct snd_soc_jack_pin imx_hp_jack_pin = {
	.pin = "Headphone Jack",
	.mask = SND_JACK_HEADPHONE,
};
static struct snd_soc_jack_gpio imx_hp_jack_gpio = {
	.name = "headphone detect",
	.report = SND_JACK_HEADPHONE,
	.debounce_time = 250,
	.invert = 0,
};

static struct snd_soc_jack imx_mic_jack;
static struct snd_soc_jack_pin imx_mic_jack_pins = {
	.pin = "Mic Jack",
	.mask = SND_JACK_MICROPHONE,
};
static struct snd_soc_jack_gpio imx_mic_jack_gpio = {
	.name = "mic detect",
	.report = SND_JACK_MICROPHONE,
	.debounce_time = 250,
	.invert = 0,
};

static int hp_jack_status_check(void *data)
{
	struct imx_priv *priv = &card_priv;
	struct snd_soc_jack *jack = data;
	struct snd_soc_dapm_context *dapm = &jack->card->dapm;
	int hp_status, ret;

	hp_status = gpio_get_value(imx_hp_jack_gpio.gpio);

	if (hp_status != priv->hp_active_low) {
		snd_soc_dapm_disable_pin(dapm, "Ext Spk");
		if (priv->is_headset_jack) {
			snd_soc_dapm_enable_pin(dapm, "Mic Jack");
			snd_soc_dapm_disable_pin(dapm, "Main MIC");
		}
		ret = imx_hp_jack_gpio.report;
		snd_kctl_jack_report(priv->snd_card, priv->headphone_kctl, 1);
	} else {
		snd_soc_dapm_enable_pin(dapm, "Ext Spk");
		if (priv->is_headset_jack) {
			snd_soc_dapm_disable_pin(dapm, "Mic Jack");
			snd_soc_dapm_enable_pin(dapm, "Main MIC");
		}
		ret = 0;
		snd_kctl_jack_report(priv->snd_card, priv->headphone_kctl, 0);
	}

	return ret;
}

static int mic_jack_status_check(void *data)
{
	struct imx_priv *priv = &card_priv;
	struct snd_soc_jack *jack = data;
	struct snd_soc_dapm_context *dapm = &jack->card->dapm;
	int mic_status, ret;

	mic_status = gpio_get_value(imx_mic_jack_gpio.gpio);

	if (mic_status != priv->mic_active_low) {
		snd_soc_dapm_disable_pin(dapm, "Main MIC");
		ret = imx_mic_jack_gpio.report;
	} else {
		snd_soc_dapm_enable_pin(dapm, "Main MIC");
		ret = 0;
	}

	return ret;
}

static int spk_amp_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct imx_priv *priv = &card_priv;

	if (!gpio_is_valid(priv->amp_gpio))
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value( priv->amp_gpio , 1 ) ;
	else
		gpio_set_value( priv->amp_gpio , 0 ) ;

	return 0;
}

static const struct snd_soc_dapm_widget imx_sgtl5000_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", spk_amp_event),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("Main MIC", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
	SND_SOC_DAPM_SPK("Line Out Jack", NULL),
};

static int imx_sqtl5000_jack_init(struct snd_soc_card *card,
		struct snd_soc_jack *jack, struct snd_soc_jack_pin *pin,
		struct snd_soc_jack_gpio *gpio)
{
	int ret;

	ret = snd_soc_card_jack_new(card, pin->pin, pin->mask, jack, pin, 1);
	if (ret) {
		return ret;
	}

	ret = snd_soc_jack_add_gpios(jack, 1, gpio);
	if (ret)
		return ret;

	return 0;
}

static ssize_t headphone_show(struct device_driver *dev, char *buf)
{
	struct imx_priv *priv = &card_priv;
	int hp_status;

	/* Check if headphone is plugged in */
	hp_status = gpio_get_value(imx_hp_jack_gpio.gpio);

	if (hp_status != priv->hp_active_low)
		strcpy(buf, "Headphone()\n");
	else
		strcpy(buf, "Speaker()\n");

	return strlen(buf);
}

static ssize_t micphone_show(struct device_driver *dev, char *buf)
{
	struct imx_priv *priv = &card_priv;
	int mic_status;

	/* Check if headphone is plugged in */
	mic_status = gpio_get_value(imx_mic_jack_gpio.gpio);

	if (mic_status != priv->mic_active_low)
		strcpy(buf, "Mic Jack()\n");
	else
		strcpy(buf, "Main MIC()\n");

	return strlen(buf);
}

static DRIVER_ATTR_RO(headphone);
static DRIVER_ATTR_RO(micphone);
//static DRIVER_ATTR(headphone, S_IRUGO | S_IWUSR, show_headphone, NULL);
//static DRIVER_ATTR(micphone, S_IRUGO | S_IWUSR, show_micphone, NULL);

static int imx_sgtl5000_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct imx_sgtl5000_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct device *dev = rtd->card->dev;
	int ret;

	ret = snd_soc_dai_set_sysclk(rtd->codec_dai, SGTL5000_SYSCLK,
				     data->clk_frequency, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(dev, "could not set codec driver clock params\n");
		return ret;
	}

	return 0;
}


static int imx_sgtl5000_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ssi_np, *codec_np;
	struct platform_device *ssi_pdev;
	struct i2c_client *codec_dev;
	struct imx_sgtl5000_data *data = NULL;
	struct snd_soc_dai_link_component *comp;
	struct imx_priv *priv = &card_priv;
	int int_port, ext_port;
	int ret;
	int gpio;

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		return ret;
	}
	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		return ret;
	}

	/*
	 * The port numbering in the hardware manual starts at 1, while
	 * the audmux API expects it starts at 0.
	 */
	int_port--;
	ext_port--;
	ret = imx_audmux_v2_configure_port(int_port,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TCLKDIR,
			IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux internal port setup failed\n");
		return ret;
	}
	ret = imx_audmux_v2_configure_port(ext_port,
			IMX_AUDMUX_V2_PTCR_SYN,
			IMX_AUDMUX_V2_PDCR_RXDSEL(int_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux external port setup failed\n");
		return ret;
	}

	ssi_np = of_parse_phandle(pdev->dev.of_node, "ssi-controller", 0);
	codec_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	if (!ssi_np || !codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	ssi_pdev = of_find_device_by_node(ssi_np);
	if (!ssi_pdev) {
		dev_dbg(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EPROBE_DEFER;
		goto fail;
	}
	put_device(&ssi_pdev->dev);
	codec_dev = of_find_i2c_device_by_node(codec_np);
	if (!codec_dev) {
		dev_dbg(&pdev->dev, "failed to find codec platform device\n");
		ret = -EPROBE_DEFER;
		goto fail;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	comp = devm_kzalloc(&pdev->dev, 3 * sizeof(*comp), GFP_KERNEL);
	if (!comp) {
		ret = -ENOMEM;
		goto fail;
	}

	data->codec_clk = clk_get(&codec_dev->dev, NULL);
	if (IS_ERR(data->codec_clk)) {
		ret = PTR_ERR(data->codec_clk);
		goto fail;
	}

	data->clk_frequency = clk_get_rate(data->codec_clk);

	/* gpio for amp on/off */
	gpio = of_get_named_gpio( pdev->dev.of_node , "amp-ena-gpios" , 0 ) ;
	if ( gpio_is_valid( gpio ) )
	{
		priv->amp_gpio = gpio ;
//	        if ( devm_gpio_request( &pdev->dev , priv->amp_gpio , "amp-gpio" ) == 0 )
		if ( gpio_request_one( priv->amp_gpio , GPIOF_DIR_OUT , "amp-gpio" ) == 0 )
		{
		    gpio_set_value( priv->amp_gpio , 1 ) ;
		}
		else
		{
		    printk( KERN_ERR "cannot request gpio for Amp enable\n" ) ;
		    priv->amp_gpio = 0 ;
		}
	}

	data->dai.cpus		= &comp[0];
	data->dai.codecs	= &comp[1];
	data->dai.platforms	= &comp[2];

	data->dai.num_cpus	= 1;
	data->dai.num_codecs	= 1;
	data->dai.num_platforms	= 1;

	data->dai.name = "HiFi";
	data->dai.stream_name = "HiFi";
	data->dai.codecs->dai_name = "sgtl5000";
	data->dai.codecs->of_node = codec_np;
	data->dai.cpus->of_node = ssi_np;
	data->dai.platforms->of_node = ssi_np;
	data->dai.init = &imx_sgtl5000_dai_init;
	data->dai.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			    SND_SOC_DAIFMT_CBM_CFM;

	data->card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto fail;
	ret = snd_soc_of_parse_audio_routing(&data->card, "audio-routing");
	if (ret)
		goto fail;
	data->card.num_links = 1;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = &data->dai;
	data->card.dapm_widgets = imx_sgtl5000_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_sgtl5000_dapm_widgets);

	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
				ret);
		goto fail;
	}

	priv->snd_card = data->card.snd_card;

	imx_hp_jack_gpio.gpio = of_get_named_gpio_flags(pdev->dev.of_node,
			"hp-det-gpios", 0, &priv->hp_active_low);

	imx_mic_jack_gpio.gpio = of_get_named_gpio_flags(pdev->dev.of_node,
			"mic-det-gpios", 0, &priv->mic_active_low);

	if (gpio_is_valid(imx_hp_jack_gpio.gpio) &&
	    gpio_is_valid(imx_mic_jack_gpio.gpio) &&
	    imx_hp_jack_gpio.gpio == imx_mic_jack_gpio.gpio)
		priv->is_headset_jack = true;

	if (gpio_is_valid(imx_hp_jack_gpio.gpio)) {
		priv->headphone_kctl = snd_kctl_jack_new("Headphone", NULL);
		ret = snd_ctl_add(priv->snd_card, priv->headphone_kctl);
		if (ret)
			dev_warn(&pdev->dev, "failed to create headphone jack kctl\n");

		if (priv->is_headset_jack) {
			imx_hp_jack_pin.mask |= SND_JACK_MICROPHONE;
			imx_hp_jack_gpio.report |= SND_JACK_MICROPHONE;
		}
		imx_hp_jack_gpio.jack_status_check = hp_jack_status_check;
		imx_hp_jack_gpio.data = &imx_hp_jack;
		ret = imx_sqtl5000_jack_init(&data->card, &imx_hp_jack,
					   &imx_hp_jack_pin, &imx_hp_jack_gpio);
		if (ret) {
			dev_warn(&pdev->dev, "hp jack init failed (%d)\n", ret);
			goto out;
		}

		ret = driver_create_file(pdev->dev.driver, &driver_attr_headphone);
		if (ret)
			dev_warn(&pdev->dev, "create hp attr failed (%d)\n", ret);
	}

	if (gpio_is_valid(imx_mic_jack_gpio.gpio)) {
		if (!priv->is_headset_jack) {
			imx_mic_jack_gpio.jack_status_check = mic_jack_status_check;
			imx_mic_jack_gpio.data = &imx_mic_jack;
			ret = imx_sqtl5000_jack_init(&data->card, &imx_mic_jack,
					&imx_mic_jack_pins, &imx_mic_jack_gpio);
			if (ret) {
				dev_warn(&pdev->dev, "mic jack init failed (%d)\n", ret);
				goto out;
			}
		}
		ret = driver_create_file(pdev->dev.driver, &driver_attr_micphone);
		if (ret)
			dev_warn(&pdev->dev, "create mic attr failed (%d)\n", ret);
	}

out:
	ret = 0;

	of_node_put(ssi_np);
	of_node_put(codec_np);

	return 0;

fail:
	if (data && !IS_ERR(data->codec_clk))
		clk_put(data->codec_clk);
	of_node_put(ssi_np);
	of_node_put(codec_np);

	return ret;
}

static int imx_sgtl5000_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct imx_sgtl5000_data *data = snd_soc_card_get_drvdata(card);

	driver_remove_file(pdev->dev.driver, &driver_attr_micphone);
	driver_remove_file(pdev->dev.driver, &driver_attr_headphone);
	clk_put(data->codec_clk);

	return 0;
}

static const struct of_device_id imx_sgtl5000_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-sgtl5000", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_sgtl5000_dt_ids);

static struct platform_driver imx_sgtl5000_driver = {
	.driver = {
		.name = "imx-sgtl5000",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_sgtl5000_dt_ids,
	},
	.probe = imx_sgtl5000_probe,
	.remove = imx_sgtl5000_remove,
};
module_platform_driver(imx_sgtl5000_driver);

MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("Freescale i.MX SGTL5000 ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-sgtl5000");
