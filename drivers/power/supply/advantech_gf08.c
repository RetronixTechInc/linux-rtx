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

#include <linux/reboot.h>
#include <asm/system_misc.h>

//#define dev_info_msg
#ifdef dev_info_msg
	#define pr_info_message(format, arg...)	printk(KERN_INFO format , ## arg)
#else
	#define pr_info_message(format, arg...)
#endif

#define CAN2_TX		(196)
#define CAN2_RX		(197)

struct advantech_rom7420_data {
	int gpio_power ;
};

struct advantech_rom7420_data advantech_data ;
	
/* -------------------------------------------------------------------------------------------------------------------------------------- */
static void gf08_poweroff(void)//1KHz
{
	int i = 0;
	
	pr_emerg( KERN_INFO "Advantech gf08_poweroff.\n" ) ;
	for(i=0;; i++){
            gpio_set_value(CAN2_TX, 0);
            udelay(500);
            gpio_set_value(CAN2_TX, 1);
            udelay(500);
        }

}

/* -------------------------------------------------------------------------------------------------------------------------------------- */
extern void mxc_restart(enum reboot_mode, const char *);
static void gf08_reset(enum reboot_mode mode, const char *cmd)//2.7~3KHz
{
    //yixuan modify for 8051,2.7k-3K
    int i = 0;
    int j = 0;
    
    pr_emerg(KERN_INFO "Advantech gf08_reset.\n");
    for(i=0;; i++){
        if(j == 50){
            mxc_restart(mode, cmd);
            j = 0;
        }
        j++;
        gpio_set_value(CAN2_TX, 0);
        udelay(166);
        gpio_set_value(CAN2_TX, 1);
        udelay(166);
    }
}


/* -------------------------------------------------------------------------------------------------------------------------------------- */
static void gf08_usb_on(void)//10KHz
{
    int i = 0;
    printk( KERN_INFO "Advantech gf08_usb_on.\n" ) ;
    
    if(advantech_data.gpio_power != -1)
    {
        for(i=0; i<100 ; i++){
            gpio_set_value(advantech_data.gpio_power, 0);
            udelay(50);
            gpio_set_value(advantech_data.gpio_power, 1);
            udelay(50);
        }
    }
}

/* -------------------------------------------------------------------------------------------------------------------------------------- */
/*
//1KHz : poweroff
//2KHz : on uboot
//2.7~3KHz : reboot
//10KHz : usb power on
*/
static int gf08_probe(struct platform_device *pdev)
{
	//struct device *dev = &client->dev;
	struct device_node *np = pdev->dev.of_node;
	int gpio;
	
	printk(KERN_INFO "Advantech GF08 probe start\n");
	
	if ( np )
	{
		/* gpio for power on/off */
		gpio = of_get_named_gpio( np , "gpio-power" , 0 ) ;
		if ( gpio_is_valid( gpio ) )
		{
			advantech_data.gpio_power = gpio ;
			if ( devm_gpio_request( &pdev->dev , advantech_data.gpio_power , "advantech power" ) == 0 )
			{
				gpio_set_value( advantech_data.gpio_power , 1 ) ;
			} 
			else 
			{
				dev_err( &pdev->dev , "cannot request gpio for power\n" ) ;
				advantech_data.gpio_power = -1 ;
			}
		} 
		else if (gpio != -ENOENT) 
		{
			dev_err( &pdev->dev , "invalid gpio\n" ) ;
			return -EINVAL ;
		}
	}
    
	gf08_usb_on();
	gpio_set_value(CAN2_RX, 0);
			
	pm_power_off = gf08_poweroff ;
	arm_pm_restart = gf08_reset;

	printk( KERN_INFO "Advantech GF08 probe succeed\n" ) ;
	
	return 0 ;
}

static const struct of_device_id advantech_dt_ids[] = {
	{ .compatible = "advantech,gf08-rom7420", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, advantech_dt_ids);

static struct platform_driver GF08_driver = {
	.driver = {
		.name	= "advantech-gf08",
		.owner	= THIS_MODULE,
		.of_match_table = advantech_dt_ids,
	},
	.probe		= gf08_probe,
};

module_platform_driver(GF08_driver);

MODULE_DESCRIPTION("GF08 driver for ADVANTECH");
MODULE_AUTHOR("Tom Wang <townwang@retronix.com.tw>");
MODULE_LICENSE("GPL");

