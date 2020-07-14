/*
 * rtc-efm32.c - RTC driver for Ramtron FM3130 I2C chip.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fsl_devices.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#define dev_info_msg
#ifdef dev_info_msg
#define pr_info_efm32(format, arg...)	printk(KERN_INFO format , ## arg)
#else
#define pr_info_efm32(format, arg...)
#endif

#define EFM32_RTC_SETTIME	(0x80)
#define EFM32_RTC_SETALARM	(0x81)
#define EFM32_RTC_GETTIME	(0x82)
#define EFM32_RTC_GETALARM	(0x83)
#define EFM32_RESET			(0x84)
#define EFM32_CRYPT_SET		(0x85)
#define EFM32_CRYPT_GET		(0x86)
#define EFM32_BWVER_GET		(0x87)
#define EFM32_STATUS_SET	(0x88)
/* 0: power off at main mode
 * 1: power reset at main mode
 * 2: remember power status at main mode
 * 3: ignore power status at main mode
 */
#define EFM32_STATUS_GET	(0x89)
/* 0:
 * 1:
 * 2: get power status at main mode
      00: ignore
      01: remember
 * 3:
  */
#define EFM32_UPDATE_START	 	(0x8A)
#define EFM32_UPDATE_DATA	 	(0x8B)
#define EFM32_UPDATE_FINISH	 	(0x8C)
#define EFM32_GET_WHORUN		(0x8D)
#define EFM32_MASTER_SET_WDOG 	 	    (0x8E)
#define EFM32_MASTER_SET_WDOG_RETRY    (0x8F)
#define EFM32_MASTER_GET_WDOG_STATUS   (0x90)
#define EFM32_MASTER_GET_IR		   (0x91)

#define DEF_EFM32CMD_SLAVE_ACK					0xA0
#define DEF_EFM32CMD_SLAVE_NACK					0xA1
#define DEF_EFM32CMD_SLAVE_SENT_DATA			0xA2

struct MaxLength{
	unsigned char WriteLen;
	unsigned char ReadLen;
};

struct MaxLength u8EFM32CmdvMaxLen[] = {
	{	7     , 	3	}, // EFM32_SET_CLOCK
	{	7     , 	3	}, // EFM32_SET_ALARM
	{	3     , 	8	}, // EFM32_GET_CLOCK
	{	3     , 	8	}, // EFM32_GET_ALARM
	
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
	{	3     , 	7	}, // EFM32_GET_WDOG_STATUS
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

static int efm32_cr2032 = 0;
static unsigned long rtc_status;
static struct efm32_data {
	struct platform_device *pdev;
	struct i2c_client *client;

	struct delayed_work det_work;
	struct delayed_work det_gpio_work;
	struct input_dev *input;
	struct input_dev *input_mouse;
	uint32_t IR_code;
	int IR_enable;
	int Vendor_Key;
	int IR_type;
	uint8_t maxcode;
	u16 *matrix;
	int audio_mute ;

	int gpio_power ;
	int gpio_boot ;
	int gpio_reset ;

	int gpio[10];
	int gpio_val[10];
} efm32_i2c;

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

/* -------------------------------------------------------------------------------------------------------------------------------------- */
/* Keep power for SOC */
static void efm32_keeppower( int enable )
{
	if ( efm32_i2c.gpio_power != -1 )
	{
		gpio_set_value( efm32_i2c.gpio_power , enable ? 1 : 0 ) ;
	}
}

/**/
static void efm32_boot( int enable )
{
	if ( efm32_i2c.gpio_boot != -1 )
	{
		gpio_set_value( efm32_i2c.gpio_boot , enable ? 1 : 0 ) ;
	}
}

/* Reset MCU */
static void efm32_reset( int enable )
{
	if ( efm32_i2c.gpio_reset != -1 )
	{
		gpio_set_value( efm32_i2c.gpio_reset , enable ? 1 : 0 ) ;
	}
}

// Calculation The checksum
static int iEFM32Cmd_CheckSumIsOK( unsigned char *ubData )
{
	int iLoop , iMaxLoop ;
	unsigned char ubCheckSum = 0 ;

	iMaxLoop = ubData[0] ;
	for ( iLoop = 0 ; iLoop < iMaxLoop-1 ; iLoop ++ )
	{
		ubCheckSum += ubData[iLoop] ;
	}
	if ( ubCheckSum == ubData[iMaxLoop-1] )
	{
		return ( 1 ) ;
	}
	pr_info_efm32("%s ;CheckSum Error %02X != %02X \n",__func__, ubData[iMaxLoop-1] , ubCheckSum );
	return ( 0 ) ;
}

/* for EFM32 Read/Write throw the I2C path */
static int efm32_read(struct device *dev, void *wdata, uint8_t wlen,
						void *rdata, uint8_t rlen)
{
	struct i2c_client *client = to_i2c_client(dev);

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

	if (i2c_transfer(client->adapter, msgs, 2) == 2)
		return 0;

	return -EIO;
}

/* EFM32 set status */
static int efm32_status_set(uint8_t val)
{
	int error, i;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_STATUS_SET&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_STATUS_SET&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];

	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);
	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_STATUS_SET;
	ucWrite[2] = (val&0xFF) ;
	for(i=0; i<ucWrite[0]-1; i++){
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_read(&efm32_i2c.client->dev, ucWrite, WriteLength, ucRead, ReadLength);
	if (error)
		return error;

	if(ucRead[0] != ReadLength) return -EINVAL;
	if ( iEFM32Cmd_CheckSumIsOK( ucRead ) ){
		switch ( ucRead[1] ) {
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

/* EFM32 get status */
static int efm32_status_get(unsigned char* val)
{
	int error, i;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_STATUS_GET&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_STATUS_GET&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];

	*val = 0;
	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);
	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_STATUS_GET;
	for(i=0; i<ucWrite[0]-1; i++){
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_read(&efm32_i2c.client->dev, ucWrite, WriteLength, ucRead, ReadLength);
	if (error)
		return error;

	if(ucRead[0] != ReadLength) return -EINVAL;
	if ( iEFM32Cmd_CheckSumIsOK( ucRead ) ){
		switch ( ucRead[1] ) {
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
int efm32_wdog_set(uint32_t val)
{
	int error, i;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_SET_WDOG&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_SET_WDOG&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];

	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);
	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_MASTER_SET_WDOG;
	ucWrite[2] = (uint8_t)((val >> 8)&0xFF) ;
	ucWrite[3] = (uint8_t)(val&0xFF) ;
	for(i=0; i<ucWrite[0]-1; i++){
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_read(&efm32_i2c.client->dev, ucWrite, WriteLength, ucRead, ReadLength);
	if (error)
		return error;

	if(ucRead[0] != ReadLength) return -EINVAL;
	if ( iEFM32Cmd_CheckSumIsOK( ucRead ) ){
		switch ( ucRead[1] ) {
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
int efm32_wdog_set_times(uint8_t val)
{
	int error, i;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_SET_WDOG_RETRY&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_MASTER_SET_WDOG_RETRY&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];

	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);
	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_MASTER_SET_WDOG_RETRY;
	ucWrite[2] = (val&0xFF) ;
	for(i=0; i<ucWrite[0]-1; i++){
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_read(&efm32_i2c.client->dev, ucWrite, WriteLength, ucRead, ReadLength);
	if (error)
		return error;

	if(ucRead[0] != ReadLength) return -EINVAL;
	if ( iEFM32Cmd_CheckSumIsOK( ucRead ) ){
		switch ( ucRead[1] ) {
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
static int efm32_wdog_get(unsigned char cmd, unsigned char* val)
{
	int error, i;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(cmd&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(cmd&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];

	*val = 0;
	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);
	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = cmd;
	for(i=0; i<ucWrite[0]-1; i++){
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_read(&efm32_i2c.client->dev, ucWrite, WriteLength, ucRead, ReadLength);
	if (error)
		return error;

	if(ucRead[0] != ReadLength) return -EINVAL;
	if ( iEFM32Cmd_CheckSumIsOK( ucRead ) ){
		switch ( ucRead[1] ) {
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

static ssize_t show_version(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int error, i;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_BWVER_GET&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_BWVER_GET&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];

	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);

	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_BWVER_GET;
	for(i=0; i<ucWrite[0]-1; i++){
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_read(&efm32_i2c.client->dev, ucWrite, WriteLength, ucRead, ReadLength);
	if (error)
		return error;
	if(ucRead[0] != ReadLength) return -EINVAL;
	if ( iEFM32Cmd_CheckSumIsOK( ucRead ) ){
		switch ( ucRead[1] ) {
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
static DEVICE_ATTR(mcu_version, 0444, show_version, NULL);

static ssize_t set_ac_plug(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	efm32_reset( 1 );
	return sprintf(buf, "set_ac_plug:high\n");
}
static DEVICE_ATTR(ac_plug_high, 0444, set_ac_plug, NULL);

void efm32_poweroff(void)
{
	int error, i;
	pr_info_efm32("%s\n",__func__);
	for(i = 0;;i++){
		error = efm32_status_set(0);
		printk(KERN_INFO "power off; count: %d; status: %d\n",i, error);
		mdelay(1000);
	}
	return;
}
EXPORT_SYMBOL_GPL(efm32_poweroff);

//extern void sgtl5000_mute(int imode);
static void det_worker(struct work_struct *work)
{
	unsigned char *ircode ;
	int retry = 0 ;
	int vendor_code ;
	struct input_dev *input;
	int error ;
	char event_string[32];
	char *envp[] = { event_string, NULL };

	ircode = (unsigned char*)&efm32_i2c.IR_code;
	efm32_wdog_get(EFM32_MASTER_GET_IR, ircode);
	pr_info_efm32("efm32_i2c.IR_code : 0x%08x\n", efm32_i2c.IR_code);
	vendor_code = ircode[0] | ircode[1] << 8 ;
	ircode += 2 ;


	if(efm32_i2c.input != NULL && vendor_code == efm32_i2c.Vendor_Key && *ircode <= efm32_i2c.maxcode){
		pr_info_efm32("key code : %d\n", efm32_i2c.matrix[*ircode]);

		if(efm32_i2c.matrix[*ircode] == BTN_MISC){
			if(efm32_i2c.input_mouse != NULL && (efm32_i2c.IR_type & IR_TYPE_MOUSE) == IR_TYPE_MOUSE)
			{
				input_unregister_device(efm32_i2c.input_mouse);
				efm32_i2c.input_mouse = NULL;
				efm32_i2c.IR_type &= ~IR_TYPE_MOUSE ;
			}
			else
			{
				input = input_allocate_device();

				if(efm32_i2c.input_mouse == NULL){
					//input->id.vendor  = 0x0000;
					//input->id.product = 0x0000;
					//input->id.version = 0x0000;
                    //input->phys = "rtx/input0";
					input->name = "Virtual Mouse";
					input->id.bustype = BUS_VIRTUAL;

					input_set_capability(input, EV_REL, REL_X);
					input_set_capability(input, EV_REL, REL_Y);
					input_set_capability(input, EV_KEY, BTN_LEFT);
					//input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
					//input->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
					//input->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

					error = input_register_device(input);
					if (error < 0) {
						pr_info_efm32("failed to register input device\n");
						input_free_device(input);
						return;
					}

					efm32_i2c.input_mouse = input;
					efm32_i2c.IR_type |= IR_TYPE_MOUSE ;
				}
			}
		}
#ifdef IR_POWER
		else if(efm32_i2c.matrix[*ircode] == KEY_POWER)
		{
			efm32_wdog_set(60) ;
			efm32_poweroff() ;
		}
#endif
		else if(efm32_i2c.matrix[*ircode] == KEY_MUTE)
		{
			if(efm32_i2c.audio_mute == 1)
			{
				efm32_i2c.audio_mute = 0;
			}
			else
			{
				efm32_i2c.audio_mute = 1;
			}
			//sgtl5000_mute(efm32_i2c.audio_mute);
		}
		else
		{
			if((efm32_i2c.IR_type & IR_TYPE_MOUSE) == IR_TYPE_MOUSE
					&& (efm32_i2c.matrix[*ircode] == KEY_UP
						|| efm32_i2c.matrix[*ircode] == KEY_UP
						|| efm32_i2c.matrix[*ircode] == KEY_DOWN
						|| efm32_i2c.matrix[*ircode] == KEY_LEFT
						|| efm32_i2c.matrix[*ircode] == KEY_RIGHT
						|| efm32_i2c.matrix[*ircode] == KEY_SELECT))
			{
				int ix = 0;
				int iy = 0;
				if(efm32_i2c.matrix[*ircode] == KEY_UP)
				{
					iy = -15;
				}else if(efm32_i2c.matrix[*ircode] == KEY_DOWN)
				{
					iy = 15;
				}else if(efm32_i2c.matrix[*ircode] == KEY_LEFT)
				{
					ix = -20;
				}else if(efm32_i2c.matrix[*ircode] == KEY_RIGHT)
				{
					ix = 20;
				}

				if(efm32_i2c.matrix[*ircode] == KEY_SELECT)
				{
					input_report_key(efm32_i2c.input_mouse, BTN_LEFT, 1);
					input_sync(efm32_i2c.input_mouse);
					input_report_key(efm32_i2c.input_mouse, BTN_LEFT, 0);
				}
				else
				{
					input_report_rel(efm32_i2c.input_mouse, REL_X, ix);
					input_report_rel(efm32_i2c.input_mouse, REL_Y, iy);
				}
				input_sync(efm32_i2c.input_mouse);
			}
			else
			{
				input_report_key(efm32_i2c.input,efm32_i2c.matrix[*ircode],1);
				input_sync(efm32_i2c.input);
				input_report_key(efm32_i2c.input,efm32_i2c.matrix[*ircode],0);
				input_sync(efm32_i2c.input);
			}

			sprintf(event_string, "EVENT=%02x", *ircode );
			kobject_uevent_env(&efm32_i2c.pdev->dev.kobj, KOBJ_CHANGE, envp);

		}

	}

}

static irqreturn_t efm32_i2c_read_handler(int irq, void *data)
{
	if (efm32_i2c.IR_enable && efm32_i2c.IR_type)
		schedule_delayed_work(&(efm32_i2c.det_work), msecs_to_jiffies(10));
	return IRQ_HANDLED;
}

static void det_gpio_worker(struct work_struct *work)
{
	char event_string[32];
	char *envp[] = { event_string, NULL };
	int ival ;

	ival = gpio_get_value(efm32_i2c.gpio[0]);
	if(ival == efm32_i2c.gpio_val[0])
	{
		sprintf(event_string, "EVENT=gpio_1" );
		kobject_uevent_env(&efm32_i2c.pdev->dev.kobj, KOBJ_CHANGE, envp);
	}

}

static irqreturn_t efm32_i2c_gpio_handler(int irq, void *data)
{
	schedule_delayed_work(&(efm32_i2c.det_gpio_work), msecs_to_jiffies(1000));
	return IRQ_HANDLED;
}

static ssize_t set_startup_finish(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int i , error ;
	struct input_dev *input;
	int icode;

	//echo finish > /sys/devices/platform/mcu_efm.0/startup_finish
	if (strstr(buf, "finish") != NULL){
		for(i=0; i<5; i++){
			if(efm32_wdog_set(0) == 0){
				printk(KERN_INFO "startup_finish: %d\n", count);
				break ;
			}
		}

		input = input_allocate_device();

		if(efm32_i2c.input == NULL && input && efm32_i2c.matrix && efm32_i2c.IR_enable){
			input->keycode = (void *)efm32_i2c.matrix;
			input->keycodesize = sizeof(efm32_i2c.matrix[0]);
			input->keycodemax = efm32_i2c.maxcode;
			input->name = "IR_code";
			input->id.bustype = BUS_HOST;
			__set_bit(EV_KEY, input->evbit);
			for (icode = 0; icode <= input->keycodemax; icode++)
				__set_bit(efm32_i2c.matrix[icode], input->keybit);

			error = input_register_device(input);
			if (error < 0) {
				pr_info_efm32("failed to register input device\n");
				input_free_device(input);
				goto err1;
			}

			if (efm32_i2c.gpio_boot != -1) {
				request_irq(gpio_to_irq(efm32_i2c.gpio_boot), efm32_i2c_read_handler, IRQF_TRIGGER_RISING, "efm32_irq", &efm32_i2c);
				INIT_DELAYED_WORK(&(efm32_i2c.det_work), det_worker);
			}

			efm32_i2c.input = input;
			efm32_i2c.IR_type |= IR_TYPE_KEYBOARD ;
		}
		if (efm32_i2c.gpio[0] > 0) {
			request_irq(gpio_to_irq(efm32_i2c.gpio[0]), efm32_i2c_gpio_handler, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "efm32_gpio_irq", &efm32_i2c);
			INIT_DELAYED_WORK(&(efm32_i2c.det_gpio_work), det_gpio_worker);
		}

	}

err1:
	return count;
}
static DEVICE_ATTR(startup_finish, 0222, NULL, set_startup_finish);

/* Open EFM32 device */
static int efm32_rtc_open(struct device *dev)
{
	pr_info_efm32("%s\n",__func__);
	if (test_and_set_bit(1, &rtc_status))
		return -EBUSY;

	return 0;
}

/* Release EFM32 device */
static void efm32_rtc_release(struct device *dev)
{
	pr_info_efm32("%s\n",__func__);
	clear_bit(1, &rtc_status);
}

static int efm32_get_time(struct device *dev, struct rtc_time *tm)
{
	int error, i;
	int days;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_RTC_GETTIME&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_RTC_GETTIME&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];
	unsigned long iRTCsec = 0 ;

	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);

	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_RTC_GETTIME;
	for(i=0; i<ucWrite[0]-1; i++){
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_read(dev, ucWrite, WriteLength, ucRead, ReadLength);
	if (error)
		return error;

	if(ucRead[0] != ReadLength) return -EINVAL;

	error = -ENOIOCTLCMD;
	if ( iEFM32Cmd_CheckSumIsOK( ucRead ) ){
		switch ( ucRead[1]) {
			case DEF_EFM32CMD_SLAVE_ACK :
			case DEF_EFM32CMD_SLAVE_NACK :
				break ;
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				for( i = 0; i < sizeof(unsigned long); i++)
				{
					iRTCsec |= ucRead[2 + i] << (i * 8) ;
				}
				pr_info_efm32("%s =======iRTCsec=%d\n",__func__,iRTCsec);
				rtc_time_to_tm(iRTCsec, tm) ;
				error = rtc_valid_tm(tm);
				if (error)
					return error;
					
				efm32_cr2032 = (int) ucRead[2 + sizeof(unsigned long)];
				pr_info_efm32("%s: %02d-%02d-%02d %02d-%02d-%02d ;batt:%02x\n",__func__,
						tm->tm_year, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, efm32_cr2032);
				break ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}

	return error ;
}

static int efm32_set_time(struct device *dev, struct rtc_time *tm)
{
	int error, i;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_RTC_SETTIME&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_RTC_SETTIME&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];
	unsigned long iRTCsec = 0 ;

	if(rtc_hctosys_ret){
		printk(KERN_ERR "%s,　rtc_hctosys_ret error : %d \n", __FUNCTION__,rtc_hctosys_ret);
		return -1;
	}

	error = rtc_tm_to_time(tm, &iRTCsec);
	if(error)
		return error ;

	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);

	//i2c write command setting
	ucWrite[0] = WriteLength;
	ucWrite[1] = EFM32_RTC_SETTIME;
	for ( i = 0 ; i < sizeof(iRTCsec) ; i ++ )
	{
		ucWrite[2 + i] = (unsigned char)(iRTCsec >> (i * 8));
	}
	
	for(i=0; i<ucWrite[0]-1; i++){
		ucWrite[ucWrite[0]-1] += ucWrite[i];
	}

	error = efm32_read(dev, ucWrite, WriteLength, ucRead, ReadLength);
	if (error)
		return error;

	if(ucRead[0] != ReadLength) return -EINVAL;
	error = -ENOIOCTLCMD ;
	if ( iEFM32Cmd_CheckSumIsOK( ucRead ) ){
		switch ( ucRead[1] ) {
			case DEF_EFM32CMD_SLAVE_ACK :
				pr_info_efm32("%s success\n" , __func__ ) ;
				error = 0 ;
				break ;
			case DEF_EFM32CMD_SLAVE_NACK :
			case DEF_EFM32CMD_SLAVE_SENT_DATA :
				printk(KERN_INFO "%s Command is error\n" , __func__ ) ;
				break ;
			default :
				printk(KERN_INFO "%s Unknow command\n" , __func__  ) ;
				break ;
		}
	}
	
	return error;
}

static int efm32_get_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	int error, i;
	int days;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_RTC_GETALARM&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_RTC_GETALARM&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];
	struct rtc_time *tm = &alrm->time;

	pr_info_efm32("%s start\n",__func__);

	return 0;
}

static int efm32_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	int error, i;
	unsigned char WriteLength = u8EFM32CmdvMaxLen[(EFM32_RTC_SETALARM&0x7F)].WriteLen ;
	unsigned char ReadLength = u8EFM32CmdvMaxLen[(EFM32_RTC_SETALARM&0x7F)].ReadLen ;
	unsigned char ucWrite[WriteLength];
	unsigned char ucRead[ReadLength];
	struct rtc_time *tm = &alrm->time;

	memset(ucWrite, 0, WriteLength);
	memset(ucRead, 0, ReadLength);

	pr_info_efm32("%s start\n",__func__);


	return 0;
}

static int efm32_rtc_ioctl(struct device *dev, unsigned int cmd,
			 unsigned long arg)
{
	int error, i ;
	unsigned char ucWrite[32] = {0};
	unsigned char ucRead[32] = {0};
	unsigned char WriteLength;
	unsigned char ReadLength;
	unsigned char *ret = NULL;
	static struct rtc_time tm_save;

    pr_info_efm32("%s : cmd %d\n",__func__, cmd);
    error = copy_from_user(ucWrite, (unsigned char *)arg, sizeof(ucWrite));
    if(error != 0){
    	printk(KERN_INFO "%s: copy from user failed \n", __func__);
    	return(-EFAULT);
    }

	switch ((ucWrite[1]&0xF0)) {
    case (EFM32_RTC_SETTIME&0xF0):
    	WriteLength = u8EFM32CmdvMaxLen[(ucWrite[1]&0x7F)].WriteLen ;
    	ReadLength = u8EFM32CmdvMaxLen[(ucWrite[1]&0x7F)].ReadLen ;

#ifdef dev_info_msg
    	printk("ucWrite =");
    	for(i = 0; i < ucWrite[0]; ++i)printk("0x%02X ",ucWrite[i]);
    	printk("\n");
#endif

    	error = efm32_read(dev, ucWrite, WriteLength, ucRead, ReadLength);

    	if(error != 0){
    		printk(KERN_INFO "%s: command failed \n", __func__);
    		return(-EFAULT);
    	}

       	error = copy_to_user((unsigned char *)arg, (unsigned char *)ucRead, ReadLength);

    	return 0;
    default:
    	if(ucWrite[0] == 0x5A && ucWrite[1] == 0xA5){	//check head number
    		switch(ucWrite[2]){	//select action
    		case 0x01:			//set power on gpio high
    			efm32_keeppower( 1 ) ;
    			return 0;
    		case 0x02:			//set power on gpio low
    			efm32_keeppower( 0 );
    			return 0;
    		case 0x03:			//set power status
    			error = efm32_status_set(ucWrite[3]);
    			if(error == 0){
    				return 0;
    			}else{
    				return(-EFAULT);
    			}
    		case 0x04:			//get power status
    			error = efm32_status_get(ret);
    			if(error == 0){
    				error = copy_to_user((unsigned char *)arg, (unsigned char *)ret, sizeof(ret));
    				return 0;
    			}else{
    				return(-EFAULT);
    			}
    		case 0x05:			//save MCU RTC
    			error = efm32_get_time(&efm32_i2c.client->dev, &tm_save);
    			if(error == 0){
    				return 0;
    			}else{
    				return(-EFAULT);
    			}
    		case 0x06:			//restore MCU RTC
    			error = efm32_set_time(&efm32_i2c.client->dev, &tm_save);
    			if(error == 0){
    				return 0;
    			}else{
    				return(-EFAULT);
    			}
    		default:
    			break;
    		}
    	}
	}
	return -ENOIOCTLCMD;
}

static const struct rtc_class_ops efm32_rtc_ops = {
	.open 		= efm32_rtc_open,
	.release 	= efm32_rtc_release,
	.read_time	= efm32_get_time,
	.set_time	= efm32_set_time,
	.read_alarm	= efm32_get_alarm,
	.set_alarm	= efm32_set_alarm,
	.ioctl 		= efm32_rtc_ioctl,
};

void efm32_power_reset(void)
{
	int error, i;
	pr_info_efm32("%s\n",__func__);
	for(i = 0;;i++){
		error = efm32_status_set(1);
		printk(KERN_INFO "power reset; count: %d; status: %d\n",i, error);
		mdelay(1000);
	}
	return;
}
EXPORT_SYMBOL_GPL(efm32_power_reset);

static struct i2c_driver efm32_driver;
static int efm32_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct rtc_device *rtc;
	int error, gpio ;
	int ir_value = 0 ;
	struct mxc_efm32_platform_data *plat = client->dev.platform_data;
	int i = 0 ;
	struct device_node *np = client->dev.of_node;
	struct property *prop;
	int length;
	unsigned int code_val[128];
	
	printk(KERN_INFO "EFM32 rtc probe start\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

    device_init_wakeup(&client->dev, true);

	rtc = rtc_device_register(efm32_driver.driver.name, &client->dev, &efm32_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)){
		printk(KERN_INFO "EFM32 rtc probe Fail\n");
		return PTR_ERR(rtc);
	}

	i2c_set_clientdata(client, rtc);

	error = device_create_file(&efm32_i2c.pdev->dev, &dev_attr_mcu_version);
	if (error)
		dev_err(&client->dev, "Error %d on creating file\n", error);

	error = device_create_file(&efm32_i2c.pdev->dev, &dev_attr_ac_plug_high);
	if (error)
		dev_err(&client->dev, "Error %d on creating file\n", error);

	error = device_create_file(&efm32_i2c.pdev->dev, &dev_attr_startup_finish);
	if (error)
		dev_err(&client->dev, "Error %d on creating file\n", error);

	efm32_i2c.client = client;
	
	efm32_i2c.gpio_power = -1 ;
	efm32_i2c.gpio_boot = -1 ;
	efm32_i2c.gpio_reset = -1 ;

	if ( np )
	{
		/* gpio for power on/off */
		gpio = of_get_named_gpio( np , "gpio-power" , 0 ) ;
		if ( gpio_is_valid( gpio ) )
		{
			efm32_i2c.gpio_power = gpio ;
			if ( devm_gpio_request( &client->dev , efm32_i2c.gpio_power , "efm32-a6 power" ) == 0 )
			{
				gpio_set_value( efm32_i2c.gpio_power , 0 ) ;
			} 
			else 
			{
				dev_err( &client->dev , "cannot request gpio for power\n" ) ;
				efm32_i2c.gpio_power = -1 ;
			}
		} 

		/* gpio for boot or ir_int*/
		gpio = of_get_named_gpio( np , "gpio-boot" , 0 ) ;
		if ( gpio_is_valid( gpio ) )
		{
			efm32_i2c.gpio_boot = gpio ;
			if ( devm_gpio_request( &client->dev , efm32_i2c.gpio_boot , "efm32-a6 boot" ) == 0 )
			{
				gpio_set_value( efm32_i2c.gpio_boot , 0 ) ;
			} 
			else 
			{
				dev_err( &client->dev , "cannot request gpio for boot\n" ) ;
				efm32_i2c.gpio_boot = -1 ;
			}
		} 

		/* gpio for reset */
		gpio = of_get_named_gpio( np , "gpio-reset" , 0 ) ;
		if ( gpio_is_valid( gpio ) )
		{
			efm32_i2c.gpio_reset = gpio ;
			if ( devm_gpio_request( &client->dev , efm32_i2c.gpio_reset , "efm32-a6 reset" ) == 0 )
			{
				gpio_set_value( efm32_i2c.gpio_reset , 0 ) ;
			} 
			else 
			{
				dev_err( &client->dev , "cannot request gpio for reset\n" ) ;
				efm32_i2c.gpio_reset = -1 ;
			}
		} 

		/* IR control enable*/
		error = of_property_read_u32(np, "efm32-ir", &ir_value);
		if (error)
			ir_value = 0;

		if(ir_value)
		{
			efm32_i2c.IR_enable = ir_value;
			/*Vendor key code */
			error = of_property_read_u32(np, "efm32-ir-venkey", &ir_value);
			if (error)
			{
				efm32_i2c.Vendor_Key = 0x4040;
			}
			else
			{
				efm32_i2c.Vendor_Key = ir_value;
			}

			/* determine the number of key code */
			prop = of_find_property(np, "efm32-ir-keymap", &length);
			if (!prop)
			{
				efm32_i2c.maxcode = sizeof(IR_codemapping) / sizeof(u16);
				efm32_i2c.matrix = IR_codemapping;
			}
			else
			{
				efm32_i2c.maxcode = length / sizeof(u32);
				/* read key code from DT property */
				if (efm32_i2c.maxcode > 0) {
					size_t size = sizeof(*efm32_i2c.matrix) * efm32_i2c.maxcode;

					efm32_i2c.matrix = devm_kzalloc(&client->dev, size, GFP_KERNEL);
					if (!efm32_i2c.matrix)
					{
						efm32_i2c.maxcode = sizeof(IR_codemapping) / sizeof(u16);
						efm32_i2c.matrix = IR_codemapping;
					}

					error = of_property_read_u32_array(np, "efm32-ir-keymap", code_val, efm32_i2c.maxcode);
					if (error < 0)
					{
						efm32_i2c.maxcode = sizeof(IR_codemapping) / sizeof(u16);
						efm32_i2c.matrix = IR_codemapping;
					}
					else
					{
						for(i = 0; i <= efm32_i2c.maxcode; i++){
							efm32_i2c.matrix[i] = (u16) code_val[i];
						}
		
					}

					efm32_i2c.maxcode--;
				}
				else
				{
					efm32_i2c.maxcode = sizeof(IR_codemapping) / sizeof(u16);
					efm32_i2c.matrix = IR_codemapping;
				}
			}
			
			pr_info_efm32("%s======maxcode=%d\n", __func__, efm32_i2c.maxcode);
			for(i = 0; i <= efm32_i2c.maxcode; i++){
				pr_info_efm32("%s======keycod=%d\n", __func__, efm32_i2c.matrix[i]);
			}

			for(i = 0; i < 10; i++){
				efm32_i2c.gpio[i] = 0;
				efm32_i2c.gpio_val[i] = 0;
			}
		}
	}
	
	efm32_i2c.IR_type = 0 ;
	efm32_i2c.audio_mute = 0;

	if (!pm_power_off)
	{
		pm_power_off = efm32_poweroff;
	}
	
	printk(KERN_INFO "EFM32 rtc probe succeed\n");
	return 0;
}

static int efm32_remove(struct i2c_client *client)
{
	struct rtc_device *rtc = i2c_get_clientdata(client);

	rtc_device_unregister(rtc);
	return 0;
}

static const struct i2c_device_id efm32_id[] = {
	{ "efm32-i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, efm32_id);

static const struct of_device_id efm32_dt_ids[] = {
	{ .compatible = "fsl,efm32", },
	{ .compatible = "rtx,efm32-a6", },
	{ .compatible = "rtx,efm32-a6plus", },
	{ .compatible = "rtx,efm32-pitx", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, efm32_dt_ids);

static struct i2c_driver efm32_driver = {
	.driver = {
		.name	= "rtc-efm32-i2c",
		.owner	= THIS_MODULE,
		.of_match_table = efm32_dt_ids,
	},
	.probe		= efm32_probe,
	.remove		= efm32_remove,
	.id_table	= efm32_id,
};

static int __init efm32_init(void)
{
	int ret;

	memset(&efm32_i2c, 0, sizeof(efm32_i2c));

	efm32_i2c.pdev = platform_device_register_simple("mcu_efm", 0, NULL, 0);
	if (IS_ERR(efm32_i2c.pdev)) {
		printk(KERN_ERR
				"Unable to register MCU_EFM as a platform device\n");
		ret = PTR_ERR(efm32_i2c.pdev);
		goto err;
	}

	return i2c_add_driver(&efm32_driver);
err:
	return ret;
}

static void __exit efm32_exit(void)
{
	i2c_del_driver(&efm32_driver);
	platform_device_unregister(efm32_i2c.pdev);
}

subsys_initcall_sync(efm32_init);
module_exit(efm32_exit);

MODULE_DESCRIPTION("RTC driver for EFM32");
MODULE_AUTHOR("Tom Wang <townwang@retronix.com.tw>");
MODULE_LICENSE("GPL");

