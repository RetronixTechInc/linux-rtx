/*
 * rtc-efm32.c - RTC driver for EFM32 I2C chip.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/i2c.h>
//#include <linux/bcd.h>
//#include <linux/gpio.h>

//#define dev_info_msg
#ifdef dev_info_msg
	#define pr_info_efm32(format, arg...)	printk(KERN_INFO format , ## arg)
#else
	#define pr_info_efm32(format, arg...)
#endif

#define EFM32_RTC_SETTIME				(0x80)
#define EFM32_RTC_SETALARM				(0x81)
#define EFM32_RTC_GETTIME				(0x82)
#define EFM32_RTC_GETALARM				(0x83)
#define EFM32_RESET						(0x84)
#define EFM32_CRYPT_SET					(0x85)
#define EFM32_CRYPT_GET					(0x86)
#define EFM32_BWVER_GET					(0x87)
#define EFM32_STATUS_SET				(0x88)
/* 0: power off at main mode
 * 1: power reset at main mode
 * 2: remember power status at main mode
 * 3: ignore power status at main mode
 */
#define EFM32_STATUS_GET				(0x89)
/* 0:
 * 1:
 * 2: get power status at main mode
      00: ignore
      01: remember
 * 3:
  */
#define EFM32_UPDATE_START				(0x8A)
#define EFM32_UPDATE_DATA				(0x8B)
#define EFM32_UPDATE_FINISH				(0x8C)
#define EFM32_GET_WHORUN				(0x8D)
#define EFM32_MASTER_SET_WDOG			(0x8E)
#define EFM32_MASTER_SET_WDOG_RETRY		(0x8F)
#define EFM32_MASTER_GET_WDOG_STATUS	(0x90)
#define EFM32_MASTER_GET_IR		   		(0x91)

#define DEF_EFM32CMD_SLAVE_ACK			0xA0
#define DEF_EFM32CMD_SLAVE_NACK			0xA1
#define DEF_EFM32CMD_SLAVE_SENT_DATA	0xA2

struct MaxLength{
	unsigned char WriteLen;
	unsigned char ReadLen;
};

struct MaxLength u8EFM32CmdvMaxLen[] = {
	{	9     , 	3	}, // EFM32_SET_CLOCK
	{	9     , 	3	}, // EFM32_SET_ALARM
	{	3     , 	10	}, // EFM32_GET_CLOCK
	{	3     , 	10	}, // EFM32_GET_ALARM
	{	3     , 	3	}, // EFM32_RESET
	{	19     , 	3	}, // EFM32_SENT_AESDATA
	{	3     , 	19	}, // EFM32_GET_AESDATA
	{	3     , 	7	}, // EFM32_GET_VERSION
	{	4     , 	3	}, // EFM32_SET_POWER_CTL
	{	3     , 	4	}, // EFM32_GET_POWER_CTL
	{	3     , 	4	}, // EFM32_UPDATE_START
	{	19     , 	4	}, // EFM32_UPDATE_DATA
	{	15     , 	3	}, // EFM32_UPDATE_FINISH
	{	3     , 	4	}, // EFM32_GET_WHORUN
	{	5     , 	3	}, // EFM32_SET_WDOG
	{	4     , 	3	}, // EFM32_SET_WDOG_RETRY
	{	3     , 	7	}, // EFM32_GET_WDOG_STATUS
	{	3     , 	7	}, // EFM32_GET_IR
} ;


struct efm32_read_data {
	uint8_t		len;
	uint8_t		cmd;
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
	uint8_t		len;
	uint8_t		cmd;
	uint8_t		years;
	uint8_t		month;
	uint8_t		day;
	uint8_t		hours;
	uint8_t		minutes;
	uint8_t		seconds;
	uint8_t		checksum;
};

#define IR_TYPE_KEYBOARD	(0x01)
#define IR_TYPE_MOUSE		(0x02)

struct efm32_a6_data {
	struct i2c_client *client;
	struct rtc_device *rtc;

	struct mutex lock;
	
	int rtc_status ;
	
	int gpio_power ;
	int gpio_boot ;
	int gpio_reset ;
	
	int cr2032 ;
};
/*
static u16 IR_codemapping[0x5F] = {
		KEY_DELETE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
		KEY_8, KEY_9, KEY_POWER, KEY_UP, KEY_BACK, KEY_SELECT, KEY_DOWN, KEY_MUTE,
		KEY_LEFT, KEY_RIGHT, KEY_RESERVED, KEY_PLAYCD, KEY_STOPCD, KEY_VOLUMEUP, KEY_RESERVED, KEY_RESERVED,
		KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_PAUSECD, KEY_VOLUMEDOWN, KEY_HOME, KEY_RESERVED, KEY_RESERVED,
		KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
		KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
		KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
		KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
		KEY_MENU, BTN_MISC, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_0, KEY_RESERVED, KEY_RESERVED,
		KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
		KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_PREVIOUSSONG, KEY_FASTFORWARD, KEY_RESERVED, KEY_RESERVED, KEY_REWIND,
		KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_NEXTSONG, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
};
*/
static struct i2c_client *efm32_i2c_client = 0 ;
/* save now time when use ioctl functiopn */
static struct rtc_time    efm32_tm_save ;

/* -------------------------------------------------------------------------------------------------------------------------------------- */
/* Keep power for SOC */
static void efm32_a6_keeppower( struct efm32_a6_data *efm32data , int enable )
{
	if ( efm32data )
	{
		if ( efm32data->gpio_power != -1 )
		{
			gpio_set_value( efm32data->gpio_power , enable ? 1 : 0 ) ;
		}
	}
}

/**/
static void efm32_a6_boot( struct efm32_a6_data *efm32data , int enable )
{
	if ( efm32data )
	{
		if ( efm32data->gpio_boot != -1 )
		{
			gpio_set_value( efm32data->gpio_boot , enable ? 1 : 0 ) ;
		}
	}
}

/* Reset MCU */
static void efm32_a6_reset( struct efm32_a6_data *efm32data , int enable )
{
	if ( efm32data )
	{
		if ( efm32data->gpio_reset != -1 )
		{
			gpio_set_value( efm32data->gpio_reset , enable ? 1 : 0 ) ;
		}
	}
}

/* -------------------------------------------------------------------------------------------------------------------------------------- */
// Calculation The checksum
static int efm32_a6_check_checksum( unsigned char *data )
{
	int index , max_index ;
	unsigned char checksum = 0 ;

	max_index = data[0] ;
	if ( max_index )
	{
		for ( index = 0 ; index < max_index - 1 ; index ++ )
		{
			checksum += data[index] ;
		}
		if ( checksum == data[max_index-1] )
		{
			return ( 1 ) ;
		}
		pr_info_efm32("%s ;CheckSum Error %02X != %02X \n",__func__, data[max_index-1] , checksum );
	}
	else
	{
		pr_info_efm32("%s ;data length error \n",__func__ );
	}
	return ( 0 ) ;
}


/* for EFM32 Read/Write throw the I2C path */
static int efm32_a6_read( struct device *dev , void *wdata , uint8_t wlen , void *rdata , uint8_t rlen )
{
	struct i2c_client *client = to_i2c_client( dev ) ;
	struct efm32_a6_data *efm32data ;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = wlen,
			.buf = wdata,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = rlen,
			.buf = rdata,
		}
	};

	efm32data = i2c_get_clientdata( client ) ;
	
	mutex_lock( &efm32data->lock ) ;

	if ( i2c_transfer( client->adapter , msgs , 2 ) == 2 )
	{
		mutex_unlock( &efm32data->lock ) ;
		return 0 ;
	}

	mutex_unlock( &efm32data->lock ) ;
	return -EIO;

}

/* -------------------------------------------------------------------------------------------------------------------------------------- */
/* EFM32 set status */
static int efm32_a6_status_set( struct device *dev , uint8_t val )
{
	unsigned char ucWrite[64] ;
	unsigned char ucRead[64]  ;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_STATUS_SET&0x7F)].WriteLen ;
	unsigned char ReadLength  = u8EFM32CmdvMaxLen[(EFM32_STATUS_SET&0x7F)].ReadLen  ;
	int error, i;
	
	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;

	pr_info_efm32( "%s start\n" , __func__ ) ;
	
	//i2c write command setting
	ucWrite[0] = WriteLength ;
	ucWrite[1] = EFM32_STATUS_SET ;
	ucWrite[2] = ( val & 0xFF ) ;
	
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}
	
	for( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if ( error )
	{
		return error ;
	}

	if( ucRead[0] != ReadLength )
	{
		return -EINVAL ;
	}
	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( ucRead[1] ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
				pr_info_efm32("%s success\n" , __func__ ) ;
				return ( 0 ) ;
			case DEF_EFM32CMD_SLAVE_NACK :
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	
	return -ENOIOCTLCMD ;
}

/* EFM32 get status */
static int efm32_a6_status_get( struct device *dev , uint8_t *val )
{
	unsigned char ucWrite[64] ;
	unsigned char ucRead[64]  ;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_STATUS_GET&0x7F)].WriteLen ;
	unsigned char ReadLength  = u8EFM32CmdvMaxLen[(EFM32_STATUS_GET&0x7F)].ReadLen  ;
	int error, i;

	*val = 0 ;
	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;

	pr_info_efm32( "%s start\n" , __func__ ) ;
	
	//i2c write command setting
	ucWrite[0] = WriteLength      ;
	ucWrite[1] = EFM32_STATUS_GET ;
	
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}
	for( i = 0 ; i < ucWrite[0] - 1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if ( error )
	{
		return error;
	}

	if ( ucRead[0] != ReadLength )
	{
		return -EINVAL;
	}
	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( ucRead[1] ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
			case DEF_EFM32CMD_SLAVE_NACK :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				*val = ucRead[2];
				return ( 0 ) ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	return -ENOIOCTLCMD;
}

/* EFM32 set WDOG time */
int efm32_a6_wdog_set( struct device *dev , uint32_t val )
{
	unsigned char ucWrite[64] ;
	unsigned char ucRead[64]  ;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_SET_WDOG&0x7F)].WriteLen ;
	unsigned char ReadLength  = u8EFM32CmdvMaxLen[(EFM32_MASTER_SET_WDOG&0x7F)].ReadLen  ;
	int error, i;

	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;

	pr_info_efm32( "%s start\n" , __func__ ) ;
	//i2c write command setting
	ucWrite[0] = WriteLength ;
	ucWrite[1] = EFM32_MASTER_SET_WDOG ;
	ucWrite[2] = (uint8_t)( ( val >> 8 ) & 0xFF ) ;
	ucWrite[3] = (uint8_t)( val & 0xFF ) ;
	
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}
	
	for( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i] ;
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if (error)
	{
		return error;
	}

	if( ucRead[0] != ReadLength )
	{
		return -EINVAL ;
	}
	
	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( ucRead[1] ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
				pr_info_efm32("%s success\n" , __func__ ) ;
				return ( 0 ) ;
			case DEF_EFM32CMD_SLAVE_NACK :
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	return -ENOIOCTLCMD;
}

/* EFM32 set WDOG retry times */
int efm32_a6_wdog_set_times( struct device *dev , uint8_t val )
{
	unsigned char ucWrite[64];
	unsigned char ucRead[64];
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_SET_WDOG_RETRY&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_SET_WDOG_RETRY&0x7F)].ReadLen ;
	int error, i;
	
	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;

	pr_info_efm32( "%s start\n" , __func__ ) ;
	//i2c write command setting
	ucWrite[0] = WriteLength ;
	ucWrite[1] = EFM32_MASTER_SET_WDOG_RETRY ;
	ucWrite[2] = ( val & 0xFF ) ;
	
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}
		
	for( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i] ;
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if ( error )
	{
		return error;
	}

	if (ucRead[0] != ReadLength)
	{
		return -EINVAL;
	}
	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( ucRead[1] ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
				pr_info_efm32("%s success\n" , __func__ ) ;
				return ( 0 ) ;
			case DEF_EFM32CMD_SLAVE_NACK :
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	return -ENOIOCTLCMD;
}

/* EFM32 get WDOG status */
static int efm32_a6_wdog_get( struct device *dev , unsigned char* val )
{
	unsigned char ucWrite[64];
	unsigned char ucRead[64];
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_GET_WDOG_STATUS&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_GET_WDOG_STATUS&0x7F)].ReadLen ;
	int error, i;
	

	*val = 0;
	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;

	pr_info_efm32( "%s start\n" , __func__ ) ;
	//i2c write command setting
	ucWrite[0] = WriteLength ;
	ucWrite[1] = EFM32_MASTER_GET_WDOG_STATUS ;
	
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}
	
	for( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i] ;
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if (error)
	{
		return error ;
	}

	if( ucRead[0] != ReadLength )
	{
		return -EINVAL;
	}
	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( ucRead[1] ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
			case DEF_EFM32CMD_SLAVE_NACK :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				for(i=0; i<ucRead[0]-3; i++)
				*(val+i) = ucRead[i+2];
				return ( 0 ) ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	return -ENOIOCTLCMD;
}

static int efm32_a6_get_time( struct device *dev , struct rtc_time *tm )
{
	unsigned char ucWrite[64] ;
	unsigned char ucRead[64]  ;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_RTC_GETTIME&0x7F)].WriteLen ;
	unsigned char ReadLength  = u8EFM32CmdvMaxLen[(EFM32_RTC_GETTIME&0x7F)].ReadLen  ;
	int error, i;
	struct efm32_read_data *read_regs;
	struct i2c_client *i2cclient = to_i2c_client( dev ) ;
	struct efm32_a6_data *efm32data = i2c_get_clientdata( i2cclient ) ;
	
	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;

	pr_info_efm32("%s start\n",__func__);

	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_RTC_GETTIME;

	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}
	
	for( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i] ;
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if ( error )
	{
		return error ;
	}

	read_regs = (struct efm32_read_data *)ucRead ;
	
	if ( read_regs->len != ReadLength )
	{
		return -EINVAL ;
	}

	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( read_regs->cmd ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
			case DEF_EFM32CMD_SLAVE_NACK :
				break ;
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				{
					int days;
					tm->tm_sec  = read_regs->seconds;
					tm->tm_min  = read_regs->minutes;
					tm->tm_hour = read_regs->hours;
					tm->tm_mday = read_regs->day;
					tm->tm_mon  = read_regs->month - 1;
					tm->tm_year = read_regs->years + 70;
					efm32data->cr2032  = (int) read_regs->batterysts;

					tm->tm_yday = rtc_year_days( tm->tm_mday + 1 , tm->tm_mon , tm->tm_year ) ;

					days = (tm->tm_year - 1) * 365
						+ (tm->tm_year - 1)/4
						- (tm->tm_year - 1)/100
						+ (tm->tm_year - 1)/400
						+ tm->tm_yday;

					tm->tm_wday = (days + 1) % 7;
				}
				return ( 0 ) ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	
	pr_info_efm32("%s: %02x-%02x-%02x %02x-%02x-%02x ;batt:%02x ;checksum: %02x\n",__func__,
			read_regs->years, read_regs->month, read_regs->day, read_regs->hours, read_regs->minutes, read_regs->seconds,
			read_regs->batterysts, read_regs->checksum);

	return -ENOIOCTLCMD;
}

static int efm32_a6_set_time( struct device *dev, struct rtc_time *tm )
{
	unsigned char ucWrite[64] ;
	unsigned char ucRead[64]  ;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_RTC_SETTIME&0x7F)].WriteLen ;
	unsigned char ReadLength  = u8EFM32CmdvMaxLen[(EFM32_RTC_SETTIME&0x7F)].ReadLen  ;
	int error, i;
	
	if( rtc_hctosys_ret )
	{
		printk(KERN_ERR "%s,　rtc_hctosys_ret error : %d \n", __FUNCTION__,rtc_hctosys_ret);
		return -1;
	}

	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ; 

	pr_info_efm32( "%s start\n" , __func__ ) ;

	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_RTC_SETTIME;
	ucWrite[2] = tm->tm_year - 70;
	ucWrite[3] = tm->tm_mon + 1;
	ucWrite[4] = tm->tm_mday;
	ucWrite[5] = tm->tm_hour;
	ucWrite[6] = tm->tm_min;
	ucWrite[7] = tm->tm_sec;
	
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}	
	
	for( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i] ;
	}

	error = efm32_a6_read(dev, ucWrite, WriteLength, ucRead, ReadLength);
	if ( error )
	{
		return error ;
	}

	if( ucRead[0] != ReadLength )
	{
		return -EINVAL;
	}
	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( ucRead[1] ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
				pr_info_efm32("%s success\n" , __func__ ) ;
				return ( 0 ) ;
			case DEF_EFM32CMD_SLAVE_NACK :
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	return -ENOIOCTLCMD;
}

static int efm32_a6_get_alarm( struct device *dev , struct rtc_wkalrm *alrm )
{
	unsigned char ucWrite[64] ;
	unsigned char ucRead[64]  ;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_RTC_GETALARM&0x7F)].WriteLen ;
	unsigned char ReadLength  = u8EFM32CmdvMaxLen[(EFM32_RTC_GETALARM&0x7F)].ReadLen ;
	int error, i ;
	struct rtc_time *tm = &alrm->time ;
	struct efm32_read_data *read_regs ;
	struct i2c_client *i2cclient = to_i2c_client( dev ) ;
	struct efm32_a6_data *efm32data = i2c_get_clientdata( i2cclient ) ;
	
	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;

	pr_info_efm32( "%s start\n" , __func__ ) ;

	//i2c write command setting
	ucWrite[0] = WriteLength ;
	ucWrite[1] = EFM32_RTC_GETALARM ;
	
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}	
	
	for ( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i] ;
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if ( error )
	{
		return error ;
	}

	read_regs = (struct efm32_read_data *) ucRead ;
	if( read_regs->len != ReadLength )
	{
		return -EINVAL ;
	}

	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( read_regs->cmd ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
			case DEF_EFM32CMD_SLAVE_NACK :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				tm->tm_sec  = read_regs->seconds;
				tm->tm_min  = read_regs->minutes;
				tm->tm_hour = read_regs->hours;
				tm->tm_mday = read_regs->day;
				tm->tm_mon  = read_regs->month - 1;
				tm->tm_year = read_regs->years + 70;
				efm32data->cr2032 = (int) read_regs->batterysts;
				return ( 0 ) ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	
	pr_info_efm32("%s: %02x-%02x-%02x %02x-%02x-%02x ;batt:%02x ;checksum: %02x\n",__func__,
			read_regs->years, read_regs->month, read_regs->day, read_regs->hours, read_regs->minutes, read_regs->seconds,
			read_regs->batterysts, read_regs->checksum);

	return -ENOIOCTLCMD;
}

static int efm32_a6_set_alarm( struct device *dev , struct rtc_wkalrm *alrm )
{
	unsigned char ucWrite[64] ;
	unsigned char ucRead[64]  ;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_RTC_SETALARM&0x7F)].WriteLen ;
	unsigned char ReadLength  = u8EFM32CmdvMaxLen[(EFM32_RTC_SETALARM&0x7F)].ReadLen  ;
	int error, i;
	struct rtc_time *tm = &alrm->time ;

	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64  ) ;

	pr_info_efm32( "%s start\n" , __func__ ) ;

	//i2c write command setting
	ucWrite[0] = WriteLength        ;
	ucWrite[1] = EFM32_RTC_SETALARM ;
	ucWrite[2] = tm->tm_year - 70   ;
	ucWrite[3] = tm->tm_mon + 1     ;
	ucWrite[4] = tm->tm_mday        ;
	ucWrite[5] = tm->tm_hour        ;
	ucWrite[6] = tm->tm_min         ;
	ucWrite[7] = tm->tm_sec         ;
	
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}
	
	for( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i] ;
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if ( error )
	{
		return error ;
	}

	if( ucRead[0] != ReadLength )
	{
		return -EINVAL;
	}
	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( ucRead[1] ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
				pr_info_efm32( "%s success\n" , __func__ ) ;
				return ( 0 ) ;
			case DEF_EFM32CMD_SLAVE_NACK :
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	return -ENOIOCTLCMD ;
}

/* -------------------------------------------------------------------------------------------------------------------------------------- */
static ssize_t mcu_version_show( struct device *dev , struct device_attribute *attr , char *buf )
{
	unsigned char ucWrite[64] ;
	unsigned char ucRead[64]  ;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_BWVER_GET&0x7F)].WriteLen ;
	unsigned char ReadLength  = u8EFM32CmdvMaxLen[(EFM32_BWVER_GET&0x7F)].ReadLen  ;
	int error, i;

	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;

	pr_info_efm32( "%s start\n" , __func__ ) ;

	//i2c write command setting
	ucWrite[0] = WriteLength ;
	ucWrite[1] = EFM32_BWVER_GET ;
	if ( ucWrite[0] >= 63 )
	{
		return -EINVAL ;
	}
	
	for( i = 0 ; i < ucWrite[0]-1 ; i++ )
	{
		ucWrite[ucWrite[0]-1] += ucWrite[i] ;
	}

	error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;
	if ( error )
	{
		return error;
	}
	
	if( ucRead[0] != ReadLength )
	{
		return -EINVAL;
	}
	
	if ( efm32_a6_check_checksum( ucRead ) )
	{
		switch ( ucRead[1] ) 
		{
			case DEF_EFM32CMD_SLAVE_ACK :
			case DEF_EFM32CMD_SLAVE_NACK :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				return sprintf(buf, "version:%02d%02d%02d\n", ucRead[3], ucRead[4], ucRead[5]);
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	return -ENOIOCTLCMD ;
}
static DEVICE_ATTR_RO( mcu_version ) ;
//static DEVICE_ATTR( mcu_version , S_IRUGO , mcu_version_show , NULL ) ;

static ssize_t ac_plug_high_show( struct device *dev , struct device_attribute *attr , char *buf )
{
	struct i2c_client *i2cclient = to_i2c_client( dev ) ;
	struct efm32_a6_data *efm32data = i2c_get_clientdata( i2cclient ) ;
	
	efm32_a6_reset( efm32data , 1 ) ;
	
	return sprintf(buf, "set_ac_plug:high\n");
}
static DEVICE_ATTR_RO( ac_plug_high ) ;
//static DEVICE_ATTR(ac_plug_high, S_IRUGO, ac_plug_high_show, NULL);

static ssize_t startup_finish_store( struct device *dev , struct device_attribute *attr , const char *buf , size_t count )
{
	int loop ;
	struct i2c_client *i2cclient = to_i2c_client( dev ) ;
	struct efm32_a6_data *efm32data = i2c_get_clientdata( i2cclient ) ;

	if ( strstr(buf, "finish") != NULL )
	{
		for ( loop = 0 ; loop < 5 ; loop++ )
		{
			if ( efm32_a6_wdog_set( dev , 0 ) == 0 )
			{
				printk(KERN_INFO "startup_finish: %d\n", count);
				return count;
			}
		}
	}
	return count;
}
static DEVICE_ATTR_WO(startup_finish) ;
//static DEVICE_ATTR(startup_finish, S_IRUGO|S_IWUGO, NULL, startup_finish_store);

/* -------------------------------------------------------------------------------------------------------------------------------------- */
/* Open EFM32 device */
static int efm32_a6_rtc_open( struct device *dev )
{
	struct i2c_client *i2cclient = to_i2c_client( dev ) ;
	struct efm32_a6_data *efm32data = i2c_get_clientdata( i2cclient ) ;
	
	if ( test_and_set_bit( 1 , (void *)&efm32data->rtc_status ) )
	{
		return -EBUSY ;
	}

	return 0;
}

/* Release EFM32 device */
static void efm32_a6_rtc_release( struct device *dev )
{
	struct i2c_client *i2cclient = to_i2c_client( dev ) ;
	struct efm32_a6_data *efm32data = i2c_get_clientdata( i2cclient ) ;
	
	clear_bit( 1 , (void *)&efm32data->rtc_status ) ;
}

/* -------------------------------------------------------------------------------------------------------------------------------------- */
static int efm32_a6_rtc_ioctl( struct device *dev , unsigned int cmd , unsigned long arg )
{
	struct i2c_client *i2cclient = to_i2c_client( dev ) ;
	struct efm32_a6_data *efm32data = i2c_get_clientdata( i2cclient ) ;
	unsigned char ucWrite[64] = {0} ;
	unsigned char ucRead[64]  = {0} ;
	unsigned char WriteLength ;
	unsigned char ReadLength  ;
	int error ;
	
	memset( ucWrite , 0 , 64 ) ;
	memset( ucRead  , 0 , 64 ) ;
	
    pr_info_efm32("%s : cmd %d\n",__func__, cmd);
    
    error = copy_from_user( ucWrite , (unsigned char *)arg , sizeof( ucWrite ) ) ;
    if( error != 0 )
    {
    	printk( KERN_INFO "%s: copy from user failed \n" , __func__ ) ;
    	return( -EFAULT ) ;
    }
    
	switch ( ( ucWrite[1] & 0xF0 ) )
	{
		case 0x80 :
		case 0x90 :
			WriteLength = u8EFM32CmdvMaxLen[(ucWrite[1]&0x7F)].WriteLen ;
			ReadLength  = u8EFM32CmdvMaxLen[(ucWrite[1]&0x7F)].ReadLen  ;

			#ifdef dev_info_msg
			{
				int iDebugLoop ;
				printk("ucWrite =");
				for( iDebugLoop = 0 ; iDebugLoop < ucWrite[0] ; ++iDebugLoop )
				{
					printk( "0x%02X " , ucWrite[iDebugLoop] ) ;
				}
				printk("\n");
			}
			#endif

			error = efm32_a6_read( dev , ucWrite , WriteLength , ucRead , ReadLength ) ;

			if( error != 0 )
			{
				printk( KERN_INFO "%s: command failed \n" , __func__ ) ;
				return( -EFAULT ) ;
			}

			error = copy_to_user( (unsigned char *)arg , (unsigned char *)ucRead , ReadLength ) ;

			return 0 ;
		default:
			if( ucWrite[0] == 0x5A && ucWrite[1] == 0xA5 )
			{
				//check head number
				switch( ucWrite[2] )
				{	
					//select action
					case 0x01:			//set power on gpio high
						efm32_a6_keeppower( efm32data , 1 ) ;
						return 0;
					case 0x02:			//set power on gpio low
						efm32_a6_keeppower( efm32data , 0 ) ;
						return 0;
					case 0x03:			//set power status
						error = efm32_a6_status_set( dev , ucWrite[3] ) ;
						if( error == 0 )
						{
							return 0 ;
						}
						else
						{
							return( -EFAULT ) ;
						}
					case 0x04:			//get power status
						{
							uint8_t status = 0 ;
							
							error = efm32_a6_status_get( dev , &status ) ;
							
							if( error == 0 )
							{
								error = copy_to_user( (unsigned char *)arg , (unsigned char *)&status , sizeof( status ) ) ;
								return 0 ;
							}
							else
							{
								return( -EFAULT ) ;
							}
						}
					case 0x05:			//save MCU RTC
						{
							error = efm32_a6_get_time( dev , &efm32_tm_save ) ;
							if(error == 0)
							{
								return 0;
							}
							else
							{
								return(-EFAULT);
							}
						}
					case 0x06:			//restore MCU RTC
						{
							error = efm32_a6_set_time( dev , &efm32_tm_save ) ;
							if( error == 0 )
							{
								return 0 ;
							}
							else
							{
								return(-EFAULT);
							}
						}
					default:
						break;
				}
			}
			break ;
	}

	return -ENOIOCTLCMD;
}

static const struct rtc_class_ops efm32_a6_rtc_ops = {
	.open 		= efm32_a6_rtc_open,
	.release 	= efm32_a6_rtc_release,
	.ioctl 		= efm32_a6_rtc_ioctl,
	.read_time	= efm32_a6_get_time,
	.set_time	= efm32_a6_set_time,
};

/* -------------------------------------------------------------------------------------------------------------------------------------- */
static void efm32_a6_poweroff(void)
{
	struct efm32_a6_data *efm32data ;
	int error;
	
	if ( efm32_i2c_client )
	{
		efm32data = i2c_get_clientdata( efm32_i2c_client ) ;
		efm32_a6_boot( efm32data , 3 ) ;
		error = efm32_a6_status_set( &efm32_i2c_client->dev , 0 ) ;
	}
}

/* -------------------------------------------------------------------------------------------------------------------------------------- */
static struct i2c_driver efm32_a6_driver ;

static int efm32_a6_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct efm32_a6_data *efm32data ;
	//struct device *dev = &client->dev;
	struct device_node *np = client->dev.of_node;
	int gpio;
	int error;

	printk(KERN_INFO "EFM32 rtc probe start\n");
	
	efm32data = kzalloc( sizeof(*efm32data) , GFP_KERNEL ) ;
	if ( !efm32data )
	{
		return -ENOMEM ;
	}

	efm32data->client = client ;
	mutex_init( &efm32data->lock ) ;
	
	efm32data->gpio_power = -1 ;
	efm32data->gpio_boot  = -1 ;
	efm32data->gpio_reset = -1 ;
	
	if ( np )
	{
		/* gpio for power on/off */
		gpio = of_get_named_gpio( np , "gpio-power" , 0 ) ;
		if ( gpio_is_valid( gpio ) )
		{
			efm32data->gpio_power = gpio ;
			if ( devm_gpio_request( &client->dev , efm32data->gpio_power , "efm32-a6 power" ) == 0 )
			{
				gpio_set_value( efm32data->gpio_power , 0 ) ;
			} 
			else 
			{
				dev_err( &client->dev , "cannot request gpio for power\n" ) ;
				efm32data->gpio_power = -1 ;
			}
		} 
		else if (gpio != -ENOENT) 
		{
			dev_err( &client->dev , "invalid gpio\n" ) ;
			return -EINVAL ;
		}
		
		/* gpio for boot */
		gpio = of_get_named_gpio( np , "gpio-boot" , 0 ) ;
		if ( gpio_is_valid( gpio ) )
		{
			efm32data->gpio_boot = gpio ;
			if ( devm_gpio_request( &client->dev , efm32data->gpio_boot , "efm32-a6 boot" ) == 0 )
			{
				gpio_set_value( efm32data->gpio_boot , 0 ) ;
			} 
			else 
			{
				dev_err( &client->dev , "cannot request gpio for boot\n" ) ;
				efm32data->gpio_boot = -1 ;
			}
		} 
		else if ( gpio != -ENOENT )
		{
			dev_err( &client->dev , "invalid gpio\n" ) ;
			return -EINVAL ;
		}
		
		/* gpio for reset */
		gpio = of_get_named_gpio( np , "gpio-reset" , 0 ) ;
		if ( gpio_is_valid( gpio ) )
		{
			efm32data->gpio_reset = gpio ;
			if ( devm_gpio_request( &client->dev , efm32data->gpio_reset , "efm32-a6 reset" ) == 0 )
			{
				gpio_set_value( efm32data->gpio_reset , 0 ) ;
			} 
			else 
			{
				dev_err( &client->dev , "cannot request gpio for reset\n" ) ;
				efm32data->gpio_reset = -1 ;
			}
		} 
		else if ( gpio != -ENOENT )
		{
			dev_err( &client->dev , "invalid gpio\n" ) ;
			return -EINVAL ;
		}
	}

	i2c_set_clientdata( client , efm32data ) ;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		return -ENODEV ;
	}

	efm32data->rtc = devm_rtc_device_register( &client->dev , efm32_a6_driver.driver.name , &efm32_a6_rtc_ops , THIS_MODULE ) ;
	if ( IS_ERR( efm32data->rtc ) )
	{
		dev_err( &client->dev , "EFM32 rtc probe Fail\n" ) ;
		return PTR_ERR( efm32data->rtc ) ;
	}

	error = device_create_file( &client->dev , &dev_attr_mcu_version ) ;
	if ( error )
	{
		//dev_err( &client->dev , "Error %d on creating file\n" , error ) ;
	}
	
	error = device_create_file( &client->dev , &dev_attr_ac_plug_high ) ;
	if (error)
	{
		//dev_err(&client->dev, "Error %d on creating file\n", error);
	}

	error = device_create_file( &client->dev , &dev_attr_startup_finish ) ;
	if (error)
	{
		//dev_err(&client->dev, "Error %d on creating file\n", error);
	}

	efm32_i2c_client = client ;
	pm_power_off = efm32_a6_poweroff ;
	
	printk( KERN_INFO "EFM32 rtc probe succeed\n" ) ;
	
	return 0 ;
}

static int efm32_a6_remove( struct i2c_client *client )
{
	struct efm32_a6_data *efm32data ;

	efm32data = i2c_get_clientdata( client ) ;
	
	rtc_device_unregister( efm32data->rtc ) ;

	kfree( efm32data ) ;
	return 0 ;
}

static const struct i2c_device_id efm32_a6_id[] = {
	{ "efm32-i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, efm32_a6_id);

static const struct of_device_id efm32_a6_dt_ids[] = {
	{ .compatible = "rtx,efm32-a6", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, efm32_a6_dt_ids);

static struct i2c_driver efm32_a6_driver = {
	.driver = {
		.name	= "rtc-efm32-i2c",
		.owner	= THIS_MODULE,
		.of_match_table = efm32_a6_dt_ids,
	},
	.probe		= efm32_a6_probe,
	.remove		= efm32_a6_remove,
	.id_table	= efm32_a6_id,
};

module_i2c_driver(efm32_a6_driver);

MODULE_DESCRIPTION("RTC driver for EFM32");
MODULE_AUTHOR("Tom Wang <townwang@retronix.com.tw>");
MODULE_LICENSE("GPL");

