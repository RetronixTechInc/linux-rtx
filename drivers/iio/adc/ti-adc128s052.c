/*
 * Copyright (C) 2014 Angelo Compagnucci <angelo.compagnucci@gmail.com>
 *
 * Driver for Texas Instruments' ADC128S052, ADC122S021 and ADC124S021 ADC chip.
 * Datasheets can be found here:
 * http://www.ti.com/lit/ds/symlink/adc128s052.pdf
 * http://www.ti.com/lit/ds/symlink/adc122s021.pdf
 * http://www.ti.com/lit/ds/symlink/adc124s021.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

#define ADC128S052 0
#define ADC122S021 1
#define ADC124S021 2


struct adc128_configuration {
	const struct iio_chan_spec	*channels;
	u8				num_channels;
};

struct adc128 {
	struct spi_device *spi;

	struct regulator *reg;
	
	int chip_config ;

	u8 txbuffer[4] ____cacheline_aligned;
	u8 rxbuffer[4] ____cacheline_aligned;

	__be16	rx_buf[2] ____cacheline_aligned;
	__be16	tx_buf[2] ____cacheline_aligned;

};

static int adc128_adc_conversion(struct iio_dev *indio_dev, u8 channel)
{
	struct adc128 *st = iio_priv(indio_dev);
	int ret;
	int ret_value ;
		
	mutex_lock(&indio_dev->mlock);
	
	if ( st->chip_config == ADC124S021 ) {
		
		struct spi_transfer t[] = {
			{
				.tx_buf = &st->tx_buf[0],
				.rx_buf = &st->rx_buf[0],
				.len = 4,
			}, 
		};		
		
		st->tx_buf[0] = cpu_to_be16((channel << 3));
		st->tx_buf[1] = cpu_to_be16((channel << 3));
		ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
		ret_value = (st->rx_buf[1]&0xFFF) ;
		
	} else {
		st->txbuffer[0] = (channel << 3);
		st->txbuffer[1] = 0;

		ret = spi_write(st->spi, &st->txbuffer, 2);
		if (ret < 0) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}

		ret = spi_read(st->spi, &st->rxbuffer, 2);
		
		ret_value = ((st->rxbuffer[0] << 8 | st->rxbuffer[1]) & 0xFFF) ;
	}

	mutex_unlock(&indio_dev->mlock);

	if (ret < 0)
		return ret;

	return (ret_value) ;
}

static int adc128_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *channel, int *val,
			   int *val2, long mask)
{
	int ret;
	struct adc128 *st = iio_priv(indio_dev);
	
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = adc128_adc_conversion(indio_dev, channel->channel);
		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:

		ret = regulator_get_voltage(st->reg);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}

}

#define ADC128_VOLTAGE_CHANNEL(num)	\
	{ \
		.type = IIO_VOLTAGE, \
		.indexed = 1, \
		.channel = (num), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) \
	}


#define ADC128_VOLTAGE_CHANNEL1(num)	\
	{ \
		.type = IIO_VOLTAGE, \
		.indexed = 1, \
		.channel = (num), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) ,\
		.scan_type = {						\
			.sign = 'u',					\
			.realbits = 12,			\
			.storagebits = 16,				\
			.endianness = IIO_BE,				\
		}, \
	}

	

static const struct iio_chan_spec adc128s052_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
	ADC128_VOLTAGE_CHANNEL(1),
	ADC128_VOLTAGE_CHANNEL(2),
	ADC128_VOLTAGE_CHANNEL(3),
	ADC128_VOLTAGE_CHANNEL(4),
	ADC128_VOLTAGE_CHANNEL(5),
	ADC128_VOLTAGE_CHANNEL(6),
	ADC128_VOLTAGE_CHANNEL(7),
};

static const struct iio_chan_spec adc122s021_channels[] = {
	ADC128_VOLTAGE_CHANNEL(0),
	ADC128_VOLTAGE_CHANNEL(1),
};

static const struct iio_chan_spec adc124s021_channels[] = {
	ADC128_VOLTAGE_CHANNEL1(0),
	ADC128_VOLTAGE_CHANNEL1(1),
	ADC128_VOLTAGE_CHANNEL1(2),
	ADC128_VOLTAGE_CHANNEL1(3),
};

static const struct adc128_configuration adc128_config[] = {
	{ adc128s052_channels, ARRAY_SIZE(adc128s052_channels) },
	{ adc122s021_channels, ARRAY_SIZE(adc122s021_channels) },
	{ adc124s021_channels, ARRAY_SIZE(adc124s021_channels) },
};

static const struct iio_info adc128_info = {
	.read_raw = adc128_read_raw,
	.driver_module = THIS_MODULE,
};

static int adc128_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adc128 *adc;
	int config = spi_get_device_id(spi)->driver_data;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;
	adc->chip_config = config ;
	
	if ( config == ADC124S021 ) {
		spi->bits_per_word = 16 ;
		spi->mode |= SPI_MODE_3 ;
	}
	
	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->dev.of_node = spi->dev.of_node;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &adc128_info;

	indio_dev->channels = adc128_config[config].channels;
	indio_dev->num_channels = adc128_config[config].num_channels;

	adc->reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(adc->reg))
		return PTR_ERR(adc->reg);

	ret = regulator_enable(adc->reg);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);

	return ret;
}

static int adc128_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adc128 *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(adc->reg);

	return 0;
}

static const struct of_device_id adc128_of_match[] = {
	{ .compatible = "ti,adc128s052", },
	{ .compatible = "ti,adc122s021", },
	{ .compatible = "ti,adc124s021", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, adc128_of_match);

static const struct spi_device_id adc128_id[] = {
	{ "adc128s052", ADC128S052 },	/* index into adc128_config */
	{ "adc122s021",	ADC122S021 },
	{ "adc124s021", ADC124S021 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adc128_id);

static struct spi_driver adc128_driver = {
	.driver = {
		.name = "adc128s052",
		.of_match_table = of_match_ptr(adc128_of_match),
	},
	.probe = adc128_probe,
	.remove = adc128_remove,
	.id_table = adc128_id,
};
module_spi_driver(adc128_driver);

MODULE_AUTHOR("Angelo Compagnucci <angelo.compagnucci@gmail.com>");
MODULE_DESCRIPTION("Texas Instruments ADC128S052");
MODULE_LICENSE("GPL v2");
