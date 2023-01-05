/*
 * rtc-efm32.c - RTC driver for Ramtron FM3130 I2C chip.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/rtc/efm32_bishop.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#define EFM32_RTC_SETTIME	(0xE0)
#define EFM32_RTC_SETALARM	(0xE1)
#define EFM32_RTC_GETTIME	(0xE2)
#define EFM32_RTC_GETALARM	(0xE3)
#define EFM32_RESET			(0xE4)
#define EFM32_CRYPT_SET		(0xE5)
#define EFM32_CRYPT_GET		(0xE6)
#define EFM32_BWVER_GET		(0xE7)
/* main mode : return 'w' + mcu version[3]
 * boot mode : return 'b' + update FW size[3] => waitting for MCU coding
 */
#define EFM32_WRITE_DATA	(0xE8)
#define EFM32_STATUS_SET	(0xE9)
/* 0: power off at main mode
 * 1: power reset at main mode
 * 2: remember power status at main mode
 * 3: ignore power status at main mode
 * 4: update FW transfer complete, request the MCU to Decryption and Write to main flash at boot mode => waitting for MCU coding
 * 5: OS startup finished at main mode => waitting for MCU coding
 */
#define EFM32_STATUS_GET	(0xEA)
/* 0:
 * 1:
 * 2: get power status at main mode
      00: ignore
      01: remember
 * 3:
 * 4: Return Status at boot mode => waitting for MCU coding
      00 First write
      01 Flash writing
      02 16bytes write is complete(write to temp flash)
      03 Total data write is complete(write to main flash)
 * 5:
 */

struct efm32_read_data {
	uint8_t		years;
	uint8_t		month;
	uint8_t		day;
	uint8_t		hours;
	uint8_t		minutes;
	uint8_t		seconds;
	uint8_t		batterysts;
	uint8_t		checksum;
};

struct efm32_write_data {
	uint8_t		years;
	uint8_t		month;
	uint8_t		day;
	uint8_t		hours;
	uint8_t		minutes;
	uint8_t		seconds;
	uint8_t		checksum;
};

struct efm32_bishop_data {
	struct i2c_client *client;
	struct rtc_device *rtc;
	struct mutex lock;
	
	int rtc_status ;
	
	int gpio_power ;
	int gpio_boot ;
	int gpio_reset ;
	int cr2032 ;
};

static struct i2c_client *efm32_i2c_client = 0 ;

static void efm32_bishop_keeppower( struct efm32_bishop_data *efm32data , int enable)
{
	if ( efm32data )
	{
		if ( efm32data->gpio_power != -1 )
		{
			gpio_set_value( efm32data->gpio_power , enable ? 1 : 0 ) ;
		}
	}
}

static void efm32_bishop_boot( struct efm32_bishop_data *efm32data , int enable)
{
	if ( efm32data )
	{
		if ( efm32data->gpio_boot != -1 )
		{
			gpio_set_value( efm32data->gpio_boot , enable ? 1 : 0 ) ;
		}
	}
}

static void efm32_bishop_reset( struct efm32_bishop_data *efm32data , int enable)
{
	if ( efm32data )
	{
		if ( efm32data->gpio_reset != -1 )
		{
			gpio_set_value( efm32data->gpio_reset , enable ? 1 : 0 ) ;
		}
	}
}

/* for EFM32 Read/Write throw the I2C path */
static int efm32_bishop_read(struct device *dev, void *data, uint8_t rlen,
				uint8_t off, void *wdata, uint8_t wlen)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct efm32_bishop_data *efm32data;
	uint8_t wbuf[wlen + 1];

	
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = wlen + 1,
			.buf = wbuf,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = rlen,
			.buf = data,
		}
	};
	
	efm32data = i2c_get_clientdata( client ) ;
	
	mutex_lock(&efm32data->lock);
	
	wbuf[0] = off;
	if(wlen != 0)
	{
		memcpy(&wbuf[1], wdata, wlen);
	}

	if (i2c_transfer(client->adapter, msgs, 2) == 2)
	{
		mutex_unlock(&efm32data->lock);
		return 0;
	}
	
	mutex_unlock(&efm32data->lock);
	
	return -EIO;
}

static int efm32_bishop_boot_read(struct device *dev, void *data, uint8_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct efm32_bishop_data *efm32data;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data,
		}
	};
	efm32data = i2c_get_clientdata( client ) ;
	
	mutex_lock(&efm32data->lock);
	
	if (i2c_transfer(client->adapter, msgs, 1) == 1)
	{
		mutex_unlock(&efm32data->lock);
		return 0;
	}
	
	mutex_unlock(&efm32data->lock);
	return -EIO;
}

static int efm32_bishop_write(struct device *dev, void *data, uint8_t off, uint8_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct efm32_bishop_data *efm32data;
	uint8_t buffer[len + 1];
	
	efm32data = i2c_get_clientdata( client ) ;
	
	mutex_lock(&efm32data->lock);
	
	buffer[0] = off;
	if(len != 0)
	{
		memcpy(&buffer[1], data, len);
	}

	if (i2c_master_send(client, buffer, len + 1) == len + 1)
	{
		mutex_unlock(&efm32data->lock);
		return 0;
	}
	
	mutex_unlock(&efm32data->lock);
	return -EIO;
}

/* EFM32 set/get status */
static int efm32_bishop_status_set(struct device *dev , uint8_t val)
{
	int error;
	
	dev_info(dev ,"%s\n",__func__);
	
	error = efm32_bishop_write(dev, &val, EFM32_STATUS_SET, sizeof(val));
	
	return error;
}

static uint8_t efm32_bishop_status_get(struct device *dev,uint8_t val)
{
	int error;
	uint8_t data;
	
	dev_info(dev ,"%s\n",__func__);
	
	error = efm32_bishop_read(dev, &data, sizeof(data), EFM32_STATUS_GET, &val, sizeof(val));

	return data;
}

static ssize_t mcu_version_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int error = 0;
	uint8_t vers[4]={0};

	error = efm32_bishop_read(dev, vers, sizeof(vers), EFM32_BWVER_GET, &error, 0);
	return sprintf(buf, "version:%02d%02d%02d\n", vers[1], vers[2], vers[3]);
}
static DEVICE_ATTR_RO( mcu_version ) ;
//static DEVICE_ATTR(mcu_version, 0444, mcu_version_show, NULL);

/* Open EFM32 device */
static int efm32_bishop_rtc_open(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct efm32_bishop_data *efm32data;
	
	efm32data = i2c_get_clientdata( client ) ;
	
	if (test_and_set_bit(1, (void *)&efm32data->rtc_status))
		return -EBUSY;

	return 0;
}

/* Release EFM32 device */
static void efm32_bishop_rtc_release(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct efm32_bishop_data *efm32data;
	
	efm32data = i2c_get_clientdata( client ) ;
	
	clear_bit( 1 , (void *)&efm32data->rtc_status ) ;
}

static int efm32_bishop_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct efm32_bishop_data *efm32data;
	int error;
	uint8_t data[17];
	uint8_t checkall = 0;
	uint8_t check = 2;
	uint8_t vers[4];
	int i, retry;

	efm32data = i2c_get_clientdata( client ) ;

	switch (cmd) 
	{
		case EFM_SEND:
			error = copy_from_user(data, (uint8_t *)arg, sizeof(data));
			if( error != 0 )
			{
				dev_info(dev , "%s: copy from user failed \n", __func__);
				return ( -EFAULT ) ;
			}
			for(i = 0; i < 16; ++i)
			{
				checkall = checkall + data[i];
			}
			data[16] = checkall ;
			retry = 0 ;
			do
			{
				++retry ;
				error = efm32_bishop_write(dev, data, EFM32_WRITE_DATA, sizeof(data)) ;
				if(retry >= 30)
				{
					dev_err( dev , "efm32_boot_write retry over.\n" ) ;
					return(-EAGAIN);
				}
			} while(error != 0 && retry < 30);

			do
			{
				retry = retry + 3;
				mdelay(retry); //delay 1ms wait for EFM32 write flash.
				error = efm32_bishop_boot_read(dev, &check, sizeof(check));
				dev_info(dev, "%s : error: %d check %d retry %d\n",	__func__, error, check, retry);

				if(retry >= 100)
				{
					dev_err( dev , "efm32_boot_read retry over.\n" ) ;
					return(-EAGAIN);
				}
			} while((error != 0) || (check != 0 && retry < 100));
			
			return 0;

		case EFM32_VER_GET:
			error = efm32_bishop_read(dev, vers, sizeof(vers), EFM32_BWVER_GET, &error, 0);
			error = copy_to_user((uint8_t *)arg, (uint8_t *)vers, sizeof(vers));
			dev_info(dev,"version: %02d,%02d,%02d,%02d\n", vers[0], vers[1], vers[2], vers[3]);
			return 0;

		case EFM_UPDATE_START:
			dev_info(dev,"EFM_UPDATE_START...\n");
			efm32_bishop_boot( efm32data , 1 ) ;
			efm32_bishop_keeppower( efm32data , 1 ) ;
			mdelay(100);
			checkall = EFM32_RESET;
			error = efm32_bishop_write(dev, &error, checkall, 0);
			efm32_bishop_reset( efm32data , 1 ) ;
    	   	mdelay(10);
    	   	efm32_bishop_reset( efm32data , 0 ) ;
			mdelay(500); //for EFM restart initial time.

			retry = 100;
			do{
				retry = retry - 10;
				mdelay(retry); //delay retry(ms) wait for EFM32 ready for update.
				error = efm32_bishop_read(dev, &check, sizeof(check), EFM32_BWVER_GET, &error, 0);

				if(retry <= 0)
				{
					dev_err(dev,"efm32_read retry over.\n");
					return(-EAGAIN);
				}
			} while(check != 'b');

			if(check == 'b')
			{
				return 0;
			}

			return -EAGAIN;

    case EFM_UPDATE_FINISHED:
    	dev_info(dev,"EFM32_UPDATE_FINISHED\n");
    	efm32_bishop_boot( efm32data , 0 ) ;
		return 0;
		
    case EFM_UPDATE_SAVETIME:
		return 0;

	case EFM_UPDATE_STATUS:
		error = copy_from_user(&check, (uint8_t *)arg, sizeof(check));
		if(error != 0)
		{
			dev_info(dev, "%s: copy from user failed \n", __func__);
			return(-EFAULT);
		}
		if(check == 2 || check == 3)
		{
			error = efm32_bishop_status_set(dev, check);
		}else if(check == 0x12)
		{
			error = (int) efm32_bishop_status_get(dev, check & 0x0F);
		}
		return error;
	}

	return -ENOIOCTLCMD;
}



static int efm32_bishop_get_time(struct device *dev, struct rtc_time *tm)
{
	struct efm32_read_data regs;
	int error;
	uint8_t checkall;
	int days;

	mdelay(4);
	error = efm32_bishop_read(dev, &regs, sizeof(regs), EFM32_RTC_GETTIME, &error, 0);
	if (error)
		return error;

	checkall = regs.seconds + regs.minutes + regs.hours
			+ regs.day + regs.month + regs.years + regs.batterysts;

	if( checkall != regs.checksum )
	{
		dev_info(dev,"EFM32 rtc checkall fail chackall is 0x%x, checksum is 0x%x\n", checkall, regs.checksum);
	}

	tm->tm_sec = regs.seconds;
	tm->tm_min = regs.minutes;
	tm->tm_hour = regs.hours;
	tm->tm_mday = regs.day;
	tm->tm_mon = regs.month - 1;
	tm->tm_year = regs.years + 100;

	tm->tm_yday = rtc_year_days(tm->tm_mday + 1, tm->tm_mon, tm->tm_year);

	days = (tm->tm_year - 1) * 365
		+ (tm->tm_year - 1)/4
		- (tm->tm_year - 1)/100
		+ (tm->tm_year - 1)/400
		+ tm->tm_yday;

	tm->tm_wday = (days + 1) % 7;

	return 0;
}


static int efm32_bishop_set_time(struct device *dev, struct rtc_time *tm)
{
	struct efm32_write_data regs;

	regs.seconds = tm->tm_sec;
	regs.minutes = tm->tm_min;
	regs.hours = tm->tm_hour;
	regs.day = tm->tm_mday;
	regs.month = tm->tm_mon + 1;
	regs.years = tm->tm_year - 100;
	regs.checksum = regs.seconds + regs.minutes + regs.hours
			+ regs.day + regs.month + regs.years;

	mdelay(4);
	return efm32_bishop_write(dev, &regs, EFM32_RTC_SETTIME, sizeof(regs));
}

static int efm32_bishop_get_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct efm32_bishop_data *efm32data;
	struct efm32_read_data regs;
	int error;
	uint8_t checkall;
	struct rtc_time *tm = &alrm->time;

	efm32data = i2c_get_clientdata( client ) ;
	
	error = efm32_bishop_read(dev, &regs, sizeof(regs), EFM32_RTC_GETALARM, &error, 0);
	if (error)
		return error;

	checkall = regs.seconds + regs.minutes + regs.hours
			+ regs.day + regs.month + regs.years + regs.batterysts;
	
	if( checkall != regs.checksum )
	{
		printk(KERN_INFO "EFM32 rtc checkall fail chackall is 0x%x, checksum is 0x%x\n", checkall, regs.checksum);
	}

	tm->tm_sec = regs.seconds;
	tm->tm_min = regs.minutes;
	tm->tm_hour = regs.hours;
	tm->tm_mday = regs.day;
	tm->tm_mon = regs.month - 1;
	tm->tm_year = regs.years + 100;
	efm32data->cr2032 = (int) regs.batterysts;

	return 0;
}

static int efm32_bishop_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	//struct i2c_client *client = to_i2c_client(dev);
	struct efm32_write_data regs;
	struct rtc_time *tm = &alrm->time;
	
	regs.seconds = tm->tm_sec;
	regs.minutes = tm->tm_min;
	regs.hours = tm->tm_hour;
	regs.day = tm->tm_mday;
	regs.month = tm->tm_mon + 1;
	regs.years = tm->tm_year - 100;
	regs.checksum = regs.seconds + regs.minutes + regs.hours
			+ regs.day + regs.month + regs.years;

	return efm32_bishop_write(dev, &regs, EFM32_RTC_SETALARM, sizeof(regs));

}

static const struct rtc_class_ops efm32_bishop_rtc_ops = {
	.open 		= efm32_bishop_rtc_open,
	.release 	= efm32_bishop_rtc_release,
	.ioctl 		= efm32_bishop_rtc_ioctl,
	.read_time	= efm32_bishop_get_time,
	.set_time	= efm32_bishop_set_time,
	/*
	.read_alarm	= efm32_bishop_get_alarm,
	.set_alarm	= efm32_bishop_set_alarm,
	*/
};

static void efm32_bishop_poweroff(void)
{
	struct efm32_bishop_data *efm32data ;
	int error;
	if ( efm32_i2c_client )
	{
		efm32data = i2c_get_clientdata( efm32_i2c_client ) ;
		efm32_bishop_boot( efm32data , 3 ) ;
		error = efm32_bishop_status_set( &efm32_i2c_client->dev , 0 ) ;
	}
}

static struct i2c_driver efm32_bishop_driver ;

static int efm32_bishop_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct efm32_bishop_data *efm32data ;
	//struct device *dev = &client->dev;
	struct device_node *np = client->dev.of_node;
	int gpio;
	int error;

	printk(KERN_INFO "EFM32 rtc probe start\n");
	
	efm32data = kzalloc(sizeof(*efm32data), GFP_KERNEL);
	if (!efm32data)
		return -ENOMEM;

	efm32data->client = client ;
	mutex_init(&efm32data->lock);
	
	efm32data->gpio_power = -1;
	efm32data->gpio_boot = -1;
	efm32data->gpio_reset = -1;
	if ( np )
	{
		gpio = of_get_named_gpio(np, "gpio-power", 0);
		if ( gpio_is_valid( gpio ) )
		{
			efm32data->gpio_power = gpio ;
			if ( devm_gpio_request( &client->dev, efm32data->gpio_power, "efm32-bishop power") == 0 )
			{
				gpio_set_value( efm32data->gpio_power , 0 ) ;
			} 
			else 
			{
				dev_err(&client->dev, "cannot request gpio for power\n");
				efm32data->gpio_power = -1;
			}
		} 
		else if (gpio != -ENOENT) 
		{
			dev_err(&client->dev, "invalid gpio\n");
			return -EINVAL;
		}	
		gpio = of_get_named_gpio(np, "gpio-boot", 0);
		if ( gpio_is_valid( gpio ) )
		{
			efm32data->gpio_boot = gpio ;
			if ( devm_gpio_request( &client->dev, efm32data->gpio_boot, "efm32-bishop boot") == 0 )
			{
				gpio_set_value( efm32data->gpio_boot , 0 ) ;
			} 
			else 
			{
				dev_err(&client->dev, "cannot request gpio for power\n");
				efm32data->gpio_boot = -1;
			}
		} 
		else if (gpio != -ENOENT) 
		{
			dev_err(&client->dev, "invalid gpio\n");
			return -EINVAL;
		}	
		gpio = of_get_named_gpio(np, "gpio-reset", 0);
		if ( gpio_is_valid( gpio ) )
		{
			efm32data->gpio_reset = gpio ;
			if ( devm_gpio_request( &client->dev, efm32data->gpio_reset, "efm32-bishop reset") == 0 )
			{
				gpio_set_value( efm32data->gpio_reset , 0 ) ;
			} 
			else 
			{
				dev_err(&client->dev, "cannot request gpio for power\n");
				efm32data->gpio_reset = -1;
			}
		} 
		else if (gpio != -ENOENT) 
		{
			dev_err(&client->dev, "invalid gpio\n");
			return -EINVAL;
		}	
	}

	i2c_set_clientdata( client , efm32data ) ;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	efm32data->rtc = devm_rtc_device_register( &client->dev , efm32_bishop_driver.driver.name , &efm32_bishop_rtc_ops , THIS_MODULE ) ;
	if (IS_ERR(efm32data->rtc))
	{
		dev_err(&client->dev, "EFM32 rtc probe Fail\n");
		return PTR_ERR(efm32data->rtc) ;
	}
	
	error = device_create_file(&client->dev, &dev_attr_mcu_version);
	if (error)
	{
		dev_err(&client->dev, "Error %d on creating file\n", error);
	}
	
	efm32_i2c_client = client ;
	pm_power_off = efm32_bishop_poweroff ;
	
	printk(KERN_INFO "EFM32 rtc probe succeed\n");
	
	return 0;
}

static int efm32_bishop_remove(struct i2c_client *client)
{
	struct efm32_bishop_data *efm32data ;

	efm32data = i2c_get_clientdata(client) ;
	
	rtc_device_unregister( efm32data->rtc ) ;

	kfree(efm32data);
	return 0;
}

static const struct i2c_device_id efm32_bishop_id[] = {
	{ "efm32-i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, efm32_bishop_id);

static const struct of_device_id efm32_bishop_dt_ids[] = {
	{ .compatible = "rtx,efm32-bishop", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, efm32_bishop_dt_ids);

static struct i2c_driver efm32_bishop_driver = {
	.driver = {
		.name	= "rtc-efm32-i2c",
		.owner	= THIS_MODULE,
		.of_match_table = efm32_bishop_dt_ids,
	},
	.probe		= efm32_bishop_probe,
	.remove		= efm32_bishop_remove,
	.id_table	= efm32_bishop_id,
};

module_i2c_driver(efm32_bishop_driver);

MODULE_DESCRIPTION("RTC driver for EFM32");
MODULE_AUTHOR("Tom Wang <townwang@retronix.com.tw>");
MODULE_LICENSE("GPL");

