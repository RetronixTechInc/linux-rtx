/*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/memory.h>

/* We've been assigned a range on the "Low-density serial ports" major */
#define VPC3S_MAJOR			207
#define VPC3S_MINOR_START	32
#define VPC3S_DRIVER_NAME   "VPC3S"
#define VPC3S_DEV_NAME		"ttyvpc3s"

#define VPC3S_MCTRL_TIMEOUT	(250*HZ/1000)

#define	VPC3S_READ_BYTE		0x13		/* read byte */
#define	VPC3S_WRITE_BYTE	0x12		/* write byte */
#define	VPC3S_READ			0x03		/* read byte(s) */
#define	VPC3S_WRITE			0x02		/* write byte(s)/sector */

struct vpc3s {
	struct spi_device *spi;

	struct mutex lock;

	/* bin file */
	struct bin_attribute	bin;
	/* mem */
	struct memory_accessor	mem;
};

/* ------------------------------------------------------------------ */

static ssize_t vpc3s_read(struct vpc3s *st, char *buf, unsigned offset, size_t count)
{
	u8			command[4];
	u8			*cp;
	ssize_t			status;
	struct spi_transfer	t[2];
	struct spi_message	m;
	u8			instr;

	if (unlikely(offset >= st->bin.size))
		return 0;
	if ((offset + count) > st->bin.size)
		count = st->bin.size - offset;
	if (unlikely(!count))
		return count;

	cp = command;
	/* 0x00 -> 0x15 only read one byte*/
	if ( offset <= 0x15 ) {
		instr = VPC3S_READ_BYTE ;
	} else {
		instr = VPC3S_READ ;
	}

	*cp++ = instr;

	/* 16-bit address is written MSB first */
	*cp++ = offset >> 8;
	*cp++ = offset >> 0;
	
	/* 0x00 -> 0x15 only read one byte*/
	if ( offset <= 0x15 ) {
		count = 1 ;
	}
	
	spi_message_init(&m);
	memset(t, 0, sizeof t);

	t[0].tx_buf = command;
	t[0].len = 3 ;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = count;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&st->lock);

	/* Read it all at once.
	 *
	 * REVISIT that's potentially a problem with large chips, if
	 * other devices on the bus need to be accessed regularly or
	 * this chip is clocked very slowly
	 */
	status = spi_sync(st->spi, &m);
	dev_dbg(&st->spi->dev,
		"read %Zd bytes at %d --> %d\n",
		count, offset, (int) status);

	mutex_unlock(&st->lock);
	
	return status ? status : count;
}

static ssize_t vpc3s_write(struct vpc3s *st, const char *buf, loff_t off, size_t count)
{
	ssize_t			status = 0;
	unsigned		written = 0;
	unsigned		buf_size;
	u8			*bounce;

	if (unlikely(off >= st->bin.size))
		return -EFBIG;
	if ((off + count) > st->bin.size)
		count = st->bin.size - off;
	if (unlikely(!count))
		return count;

	/* Temp buffer starts with command and address */
	buf_size = 1024*2;
	bounce = kmalloc(buf_size + 3, GFP_KERNEL);
	if (!bounce)
		return -ENOMEM;

	/* For write, rollover is within the page ... so we write at
	 * most one page, then manually roll over to the next page.
	 */
	mutex_lock(&st->lock);
	do {
		unsigned	segment;
		unsigned	offset = (unsigned) off;
		u8		*cp = bounce;
		u8		instr;
		
		if ( offset <= 0x15 ) {
			instr = VPC3S_WRITE_BYTE ;
		} else {
			instr = VPC3S_WRITE;
		}
		
		*cp++ = instr;

		/* 16-bit address is written MSB first */
		*cp++ = offset >> 8;
		*cp++ = offset >> 0;
		
		/* Write as much of a page as we can */
		segment = buf_size - (offset % buf_size);
		if (segment > count)
			segment = count;
		
		/* 0x00 -> 0x15 only write one byte*/
		if ( offset <= 0x15 ) {
			segment = 1 ;
		}
		
		memcpy(cp, buf, segment);
		status = spi_write(st->spi, bounce,	segment + 3 );
		
		dev_dbg(&st->spi->dev, "write %u bytes at %u --> %d\n",	segment, offset, (int) status);
		
		if (status < 0)
			break;

		off += segment;
		buf += segment;
		count -= segment;
		written += segment;

	} while (count > 0);

	mutex_unlock(&st->lock);

	kfree(bounce);
	return written ? written : status;
}

static ssize_t vpc3s_bin_read(struct file *filp, struct kobject *kobj, struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device	*dev;
	struct vpc3s	*st;

	dev = container_of(kobj, struct device, kobj);
	st = dev_get_drvdata(dev);

	return vpc3s_read(st, buf, off, count);
}

static ssize_t vpc3s_bin_write(struct file *filp, struct kobject *kobj, struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device	*dev;
	struct vpc3s	*st;

	dev = container_of(kobj, struct device, kobj);
	st = dev_get_drvdata(dev);

	return vpc3s_write(st, buf, off, count);
}

static ssize_t vpc3s_mem_read(struct memory_accessor *mem, char *buf, off_t offset, size_t count)
{
	struct vpc3s *st = container_of(mem, struct vpc3s, mem);

	return vpc3s_read(st, buf, offset, count);
}

static ssize_t vpc3s_mem_write(struct memory_accessor *mem, const char *buf, off_t offset, size_t count)
{
	struct vpc3s *st = container_of(mem, struct vpc3s, mem);

	return vpc3s_write(st, buf, offset, count);
}

static int vpc3s_probe(struct spi_device *spi)
{
	struct vpc3s *st ;
	int ret;

	st = kzalloc(sizeof(struct vpc3s), GFP_KERNEL);
	if (!st) {
		dev_warn(&spi->dev, "kmalloc for vpc3s structure failed!\n");
		return -ENOMEM;
	}

	mutex_init(&st->lock);
	
	/* -------------------------------------------------------------- */
	st->spi = spi_dev_get(spi);

	spi->mode = SPI_MODE_3;
	
	spi_set_drvdata(spi, st);

	ret = spi_setup(spi);
	
	if (ret < 0) {
		dev_warn(&spi->dev, "spi_setup failed with error %d\n", ret);
		return ret;
	}
	/* -------------------------------------------------------------- */
	sysfs_bin_attr_init(&st->bin);
	st->bin.attr.name = "vpc3s_mem";
	st->bin.attr.mode = (S_IRUSR | S_IWUSR);
	st->bin.read  = vpc3s_bin_read;
	st->bin.write = vpc3s_bin_write;
	st->mem.read = vpc3s_mem_read;
	st->mem.write = vpc3s_mem_write;
	st->bin.size = 2*1024 ; // 2K

	ret = sysfs_create_bin_file(&spi->dev.kobj, &st->bin);
	if (ret) {
		dev_warn(&spi->dev, "sysfs_create_bin_file failed with error %d\n", ret);
		return ret;
	}
	
	return ret ;
}

static int vpc3s_remove(struct spi_device *spi)
{
	struct vpc3s	*st;

	st = spi_get_drvdata(spi);
	
	sysfs_remove_bin_file(&spi->dev.kobj, &st->bin);

	return 0;
}

static const struct of_device_id vpc3s_of_match[] = {
	{ .compatible = "vpc3s", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, vpc3s_of_match);

static const struct spi_device_id vpc3s_id[] = {
	{ "vpc3s", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, vpc3s_id);

static struct spi_driver vpc3s_driver = {
	.driver = {
		.name = "vpc3s",
		.of_match_table = of_match_ptr(vpc3s_of_match),
	},
	.probe = vpc3s_probe,
	.remove = vpc3s_remove,
	.id_table = vpc3s_id,
};
module_spi_driver(vpc3s_driver);

MODULE_AUTHOR("Chase Chang <chasechang@retronix.com.tw>");
MODULE_DESCRIPTION("vpc3s");
MODULE_LICENSE("GPL v2");
