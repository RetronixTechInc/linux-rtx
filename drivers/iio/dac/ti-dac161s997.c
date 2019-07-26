/*
 * Driver for Texas Instruments' DAC161S997 DAC chip.
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
#include <linux/iio/sysfs.h>

#define DAC161S997_REG_XFER			0x1
#define DAC161S997_REG_NOP			0x2
#define DAC161S997_REG_WR_MODE		0x3
#define DAC161S997_REG_DACCODE		0x4
#define DAC161S997_REG_ERR_CONFIG	0x5
#define DAC161S997_REG_ERR_LOW		0x6
#define DAC161S997_REG_ERR_HIGH		0x7
#define DAC161S997_REG_RESET		0x8
#define DAC161S997_REG_STATUS		0x9

#define DAC161S997_RESET_VALUE		0xc33c
#define DAC161S997_CONFIG_VALUE		0x07FF

#define DAC161S997_OUTPUT_CONVERT(uA)           (((unsigned long)uA * 2732 + 500 ) / 1000)
#define DAC161S997_CONVERT_uA(value)           (((unsigned long)value * 1000 + 1370 ) / 2732)
#define DAC161S997_ERR_VALUE_MASK               0xff00
#define DAC161S997_ERR_VALUE_SHIFT              8
#define DAC161S997_CONVERT_ERR_VALUE(uA)        ((DAC161S997_OUTPUT_CONVERT(uA) & DAC161S997_ERR_VALUE_MASK))

#define DAC161S997_DEFAULT_LOW_uA				3270
#define DAC161S997_DEFAULT_HIGH_uA				21800

struct dac161s997 {
	struct spi_device *spi;

	struct regulator *reg;
	
	unsigned long out_value ;
	
	unsigned long low_uA ;
	unsigned long high_uA ;

	union {
		__be32 d32;
		u8 d8[4];
	} data[4] ____cacheline_aligned;
};


static int dac161s997_write(struct iio_dev *indio_dev, unsigned int reg, unsigned int val)
{
	struct dac161s997 *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0].d8[0],
			.rx_buf = &st->data[1].d8[0],
			.len = 4,
		}, 
	};

	mutex_lock(&indio_dev->mlock);
	st->data[0].d8[2] = reg ;
	st->data[0].d8[1] = ((val>>8)&0xFF) ;
	st->data[0].d8[0] = (val&0xFF) ;
	//st->data[0].d32 = cpu_to_be32((reg << 16) | (val&0xFFFF));
	
	ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
	
	mutex_unlock(&indio_dev->mlock);

	//printk("%s(%d) : %08X \n",__FILE__,__LINE__,st->data[0].d32);
	//printk("%s(%d) : %08X \n",__FILE__,__LINE__,st->data[1].d32);

	return ret;
}

static int dac161s997_read(struct iio_dev *indio_dev, unsigned int reg, unsigned int *val)
{
	struct dac161s997 *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0].d8[0],
			.rx_buf = &st->data[1].d8[0],
			.cs_change = 1,
			.len = 4,
		}, 
		{
			.tx_buf = &st->data[2].d8[0],
			.rx_buf = &st->data[3].d8[0],
			.len = 4,
		}, 
	};

	mutex_lock(&indio_dev->mlock);

	st->data[0].d8[2] = (reg|0x80) ;
	st->data[0].d8[1] = 0 ;
	st->data[0].d8[0] = 0 ;

	st->data[2].d8[2] = DAC161S997_REG_NOP ;
	st->data[2].d8[1] = 0 ;
	st->data[2].d8[0] = 0 ;

	//st->data[0].d32 = cpu_to_be32((1 << 23) | (reg << 16));
	
	//st->data[2].d32 = cpu_to_be32( DAC161S997_REG_NOP << 16);

	ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
	
	if (ret >= 0)
		*val = st->data[3].d32 & 0xffff;
	//printk("%s(%d) : %08X\n",__FILE__,__LINE__,st->data[3].d32);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int dac161s997_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *channel, int val, int val2, long mask)
{
	struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long writevalue ;
	int ret ;
	
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if ( val < st->low_uA ) {
			return -EINVAL;
		}
		if ( val > st->high_uA ) {
			return -EINVAL;
		}
		
		st->out_value = val ;
		
		writevalue = DAC161S997_OUTPUT_CONVERT(st->out_value) ;
		
		ret = dac161s997_write(indio_dev, DAC161S997_REG_DACCODE, (unsigned int)writevalue) ;
		dac161s997_write(indio_dev,DAC161S997_REG_NOP,0) ;
		
		st->out_value = DAC161S997_CONVERT_uA(writevalue) ;
		
		//printk("%s(%d) : %08X\n",__FILE__,__LINE__,writevalue);
		return ret ;
	default:
		return -EINVAL;
	}
}

static int dac161s997_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *channel, int *val, int *val2, long mask)
{
	struct dac161s997 *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		//*val = st->out_value ;
		
		{
			unsigned long readvalue;
	
			dac161s997_read(indio_dev,DAC161S997_REG_DACCODE,(unsigned int *)&readvalue) ;
			*val = DAC161S997_CONVERT_uA(readvalue) ;
			//printk("%s(%d) : %08X\n",__FILE__,__LINE__,readvalue);
		}
		
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

static void dac161s997_reset(struct iio_dev *indio_dev,int mode) 
{
	struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long writevalue ;
	
	if ( mode ) {
		dac161s997_write(indio_dev,DAC161S997_REG_RESET,DAC161S997_RESET_VALUE) ;
		dac161s997_write(indio_dev,DAC161S997_REG_NOP,0) ;
	}
	
	st->low_uA    = DAC161S997_DEFAULT_LOW_uA  ;
	st->high_uA   = DAC161S997_DEFAULT_HIGH_uA ;
	st->out_value = DAC161S997_DEFAULT_LOW_uA  ;

	dac161s997_write(indio_dev,DAC161S997_REG_ERR_CONFIG,DAC161S997_CONFIG_VALUE) ;
			
	writevalue = (DAC161S997_CONVERT_ERR_VALUE(st->low_uA) & 0x7f00) ;
	dac161s997_write(indio_dev,DAC161S997_REG_ERR_LOW,(unsigned int)writevalue) ;
			
	writevalue = (DAC161S997_CONVERT_ERR_VALUE(st->high_uA) & 0xff00) ;
	if ( writevalue < 0x80 ) {
		writevalue = 0x80 ;
	}
	dac161s997_write(indio_dev,DAC161S997_REG_ERR_HIGH,(unsigned int)writevalue) ;
	dac161s997_write(indio_dev,DAC161S997_REG_NOP,0) ;
					
	writevalue = DAC161S997_OUTPUT_CONVERT(st->out_value) ;
	dac161s997_write(indio_dev, DAC161S997_REG_DACCODE, (unsigned int)writevalue) ;
	dac161s997_write(indio_dev,DAC161S997_REG_NOP,0) ;	
}

/* ------------------------------------------------------------------ */
static ssize_t dac161s997_store_reset(struct device *dev,	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	
	if ( len && buf ) {
		if ( buf[0] == '1' ) {
			dac161s997_reset(indio_dev,1) ;
		}
	}
	
	return len;
}

static IIO_DEVICE_ATTR(reset   , S_IWUSR           , NULL                   , dac161s997_store_reset  , 0 ) ;

/* ------------------------------------------------------------------ */

static ssize_t dac161s997_show_low_uA(struct device *dev, struct device_attribute *attr, const char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long readvalue;
	
	//dac161s997_read(indio_dev,DAC161S997_REG_ERR_LOW,(unsigned int *)&readvalue) ;
	//printk("%s(%d) : %08X\n",__FILE__,__LINE__,readvalue);
	
	return sprintf( (char *)buf , "%d\n" , (int)st->low_uA ) ;
}

static ssize_t dac161s997_store_low_uA(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long val;
	unsigned long writevalue ;
	
	if ( kstrtoul(buf, 0, &val) )
		return -EINVAL;

	if ( val > st->high_uA )
		return -EINVAL;

	st->low_uA = val ;

	if ( st->out_value < st->low_uA ) {
		st->out_value = st->low_uA ;
		writevalue = DAC161S997_OUTPUT_CONVERT(st->out_value) ;
		
		dac161s997_write(indio_dev, DAC161S997_REG_DACCODE, (unsigned int)writevalue) ;
		dac161s997_write(indio_dev,DAC161S997_REG_NOP,0) ;
	}
	
	writevalue = (DAC161S997_CONVERT_ERR_VALUE(st->low_uA) & 0x7f00) ;
	dac161s997_write(indio_dev,DAC161S997_REG_ERR_LOW,writevalue) ;
	dac161s997_write(indio_dev,DAC161S997_REG_NOP,0) ;
	
	return len;
}

static IIO_DEVICE_ATTR(low_uA, S_IRUSR | S_IWUSR, dac161s997_show_low_uA, dac161s997_store_low_uA, 0);

/* ------------------------------------------------------------------ */
static ssize_t dac161s997_show_high_uA(struct device *dev,	struct device_attribute *attr, const char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long readvalue;
	
	//dac161s997_read(indio_dev,DAC161S997_REG_ERR_HIGH,(unsigned int *)&readvalue) ;
	//printk("%s(%d) : %08X\n",__FILE__,__LINE__,readvalue);
	
	return sprintf( (char *)buf , "%d\n" , (int)st->high_uA ) ;
}

static ssize_t dac161s997_store_high_uA(struct device *dev,	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long val;
	unsigned long writevalue ;
	
	if ( kstrtoul(buf, 0, &val) )
		return -EINVAL;

	if ( val < st->low_uA )
		return -EINVAL;

	st->high_uA = val ;

	if ( st->out_value > st->high_uA ) {
		st->out_value = st->high_uA ;
		
		writevalue = DAC161S997_OUTPUT_CONVERT(st->out_value) ;
		
		dac161s997_write(indio_dev, DAC161S997_REG_DACCODE, (unsigned int)writevalue) ;
		dac161s997_write(indio_dev,DAC161S997_REG_NOP,0) ;
	}
	
	writevalue = (DAC161S997_CONVERT_ERR_VALUE(st->high_uA) & 0xff00) ;
	if ( writevalue < 0x80 ) {
		writevalue = 0x80 ;
	}
	
	dac161s997_write(indio_dev,DAC161S997_REG_ERR_HIGH,writevalue) ;
	dac161s997_write(indio_dev,DAC161S997_REG_NOP,0) ;
	
	return len;
}

static IIO_DEVICE_ATTR(high_uA  , S_IRUSR | S_IWUSR , dac161s997_show_high_uA , dac161s997_store_high_uA , 0 ) ;

/* ------------------------------------------------------------------ */
static ssize_t dac161s997_show_status(struct device *dev,	struct device_attribute *attr, const char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	//struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long readvalue;
	
	dac161s997_read(indio_dev,DAC161S997_REG_STATUS,(unsigned int *)&readvalue) ;
	
	return sprintf( (char *)buf , "%08X\n" , (unsigned int)readvalue ) ;
}

static IIO_DEVICE_ATTR(status  , S_IRUSR           , dac161s997_show_status , NULL                    , 0 ) ;

/* ------------------------------------------------------------------ */
static ssize_t dac161s997_show_config(struct device *dev,	struct device_attribute *attr, const char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	//struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long readvalue;
	
	dac161s997_read(indio_dev,DAC161S997_REG_ERR_CONFIG,(unsigned int *)&readvalue) ;
	
	return sprintf( (char *)buf , "%08X\n" , (unsigned int)readvalue ) ;
}

static IIO_DEVICE_ATTR(config  , S_IRUSR           , dac161s997_show_config , NULL                    , 0 ) ;

/* ------------------------------------------------------------------ */
static struct attribute *dac161s997_attributes[] = {
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_dev_attr_low_uA.dev_attr.attr,
	&iio_dev_attr_high_uA.dev_attr.attr,
	&iio_dev_attr_status.dev_attr.attr,
	&iio_dev_attr_config.dev_attr.attr,
	NULL,
};

static const struct attribute_group dac161s997_attribute_group = {
	.attrs = dac161s997_attributes,
};

/* ------------------------------------------------------------------ */
static int dac161s997_initial_setup(struct iio_dev *indio_dev)
{
	struct dac161s997 *st = iio_priv(indio_dev);
	unsigned long  writevalue ;
	
	spi_setup(st->spi);
	
	dac161s997_reset(indio_dev,0) ;

	return 0;
}


static const struct iio_chan_spec dac161s997_channel = {
		.type		= IIO_VOLTAGE,
		.indexed	= 1,
		.output		= 1,
		.channel	= 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
};

static const struct iio_info dac161s997_info = {
	.write_raw = dac161s997_write_raw,
	.read_raw = dac161s997_read_raw,
	.attrs = &dac161s997_attribute_group,
	.driver_module = THIS_MODULE,
};

static int dac161s997_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct dac161s997 *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));

	if (!indio_dev) {
		return -ENOMEM;
	}

	st = iio_priv(indio_dev);
	st->spi = spi;
	
	st->low_uA    = DAC161S997_DEFAULT_LOW_uA ;
	st->high_uA   = DAC161S997_DEFAULT_HIGH_uA ;	
	st->out_value = DAC161S997_DEFAULT_LOW_uA  ;
	spi->bits_per_word = 24 ;
	spi->mode |= SPI_MODE_3 ;

	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->dev.of_node = spi->dev.of_node;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &dac161s997_info;

	indio_dev->channels = &dac161s997_channel;
	indio_dev->num_channels = 1;

	st->reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(st->reg)) {
		return PTR_ERR(st->reg);
	}

	ret = regulator_enable(st->reg);
	if (ret < 0) {
		return ret;
	}
	
	dac161s997_initial_setup( indio_dev ) ;
	
	ret = iio_device_register(indio_dev);
	
	if (ret) {
		dev_err(&spi->dev, "Failed to register iio device: %d\n",
				ret);
		goto error_disable_reg;
	}
	return 0 ;

error_disable_reg:
	regulator_disable(st->reg);

	return ret;
}

static int dac161s997_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct dac161s997 *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(st->reg);

	return 0;
}

static const struct of_device_id dac161s997_of_match[] = {
	{ .compatible = "ti,dac161s997", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, dac161s997_of_match);

static const struct spi_device_id dac161s997_id[] = {
	{ "dac161s997", 0},	
	{ }
};
MODULE_DEVICE_TABLE(spi, dac161s997_id);

static struct spi_driver dac161s997_driver = {
	.driver = {
		.name = "dac161s997",
		.of_match_table = of_match_ptr(dac161s997_of_match),
	},
	.probe = dac161s997_probe,
	.remove = dac161s997_remove,
	.id_table = dac161s997_id,
};
module_spi_driver(dac161s997_driver);

MODULE_AUTHOR("Chase Chang <chasechang@retronix.com.tw>");
MODULE_DESCRIPTION("Texas Instruments DAC161S997");
MODULE_LICENSE("GPL v2");
