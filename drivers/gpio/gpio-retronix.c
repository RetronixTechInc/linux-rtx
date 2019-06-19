#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
//#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

/**********data information***********/
struct rtx_gpio_data {
    struct pinctrl *pinctrl;
    
    int             spi_mode ;
	struct pinctrl_state *pins_spi;
	struct pinctrl_state *pins_spi_gpio;
    
    int             can_mode ;
	struct pinctrl_state *pins_can;
	struct pinctrl_state *pins_can_gpio;
    
    int             uart1_mode ;
	struct pinctrl_state *pins_uart1;
	struct pinctrl_state *pins_uart1_gpio;
 
};

/**********irq information***********/
#define CLASS_NAME "rtxgpio"
#define DEVICE_NAME "info"
static struct class * rtxgpio_class = NULL;
static struct device * rtxgpio_dev  = NULL;
static int rtxgpio_major;
static struct file_operations FOPS = {

};

/*********************************************************/
static ssize_t rtx_show_spi_mode(struct device* dev,struct device_attribute *attr, char *buf)
{
	struct rtx_gpio_data *gpiodata;
	
	gpiodata = (struct rtx_gpio_data*)dev_get_drvdata(dev);
    
    if ( gpiodata )
	{
        switch( gpiodata->spi_mode )
        {
            case 0 : return sprintf(buf, "spi");
            case 1 : return sprintf(buf, "gpio");
            default :
                gpiodata->spi_mode = 0 ;
                return sprintf(buf, "spi");
        }
    }

	return sprintf(buf, "spi");
	
}

static ssize_t rtx_store_spi_mode(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rtx_gpio_data *gpiodata;
    gpiodata = (struct rtx_gpio_data*)dev_get_drvdata(dev);
    
	if ( gpiodata )
	{
		if (sysfs_streq(buf, "spi"))
		{
			gpiodata->spi_mode = 0 ;
            if ( gpiodata->pinctrl && gpiodata->pins_spi )
            {
                pinctrl_select_state(gpiodata->pinctrl, gpiodata->pins_spi);
            }
		}
		if (sysfs_streq(buf, "gpio"))
		{
			gpiodata->spi_mode = 1 ;
            if ( gpiodata->pinctrl && gpiodata->pins_spi_gpio )
            {
                pinctrl_select_state(gpiodata->pinctrl, gpiodata->pins_spi_gpio);
            }
		}
	}
	
	return count ;
}

static ssize_t rtx_show_can_mode(struct device* dev,struct device_attribute *attr, char *buf)
{
	struct rtx_gpio_data *gpiodata;
	
	gpiodata = (struct rtx_gpio_data*)dev_get_drvdata(dev);
    
    if ( gpiodata )
	{
        switch( gpiodata->can_mode )
        {
            case 0 : return sprintf(buf, "can");
            case 1 : return sprintf(buf, "gpio");
            default :
                gpiodata->can_mode = 0 ;
                return sprintf(buf, "can");
        }
    }

	return sprintf(buf, "can");
	
}

static ssize_t rtx_store_can_mode(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rtx_gpio_data *gpiodata;
    gpiodata = (struct rtx_gpio_data*)dev_get_drvdata(dev);
    
	if ( gpiodata )
	{
		if (sysfs_streq(buf, "can"))
		{
			gpiodata->can_mode = 0 ;
            if ( gpiodata->pinctrl && gpiodata->pins_can )
            {
                pinctrl_select_state(gpiodata->pinctrl, gpiodata->pins_can);
            }
		}
		if (sysfs_streq(buf, "gpio"))
		{
			gpiodata->can_mode = 1 ;
            if ( gpiodata->pinctrl && gpiodata->pins_can_gpio )
            {
                pinctrl_select_state(gpiodata->pinctrl, gpiodata->pins_can_gpio);
            }
		}
	}
	
	return count ;
}

static ssize_t rtx_show_uart1_mode(struct device* dev,struct device_attribute *attr, char *buf)
{
	struct rtx_gpio_data *gpiodata;
	
	gpiodata = (struct rtx_gpio_data*)dev_get_drvdata(dev);
    
    if ( gpiodata )
	{
        switch( gpiodata->uart1_mode )
        {
            case 0 : return sprintf(buf, "uart1");
            case 1 : return sprintf(buf, "gpio");
            default :
                gpiodata->uart1_mode = 0 ;
                return sprintf(buf, "uart1");
        }
    }

	return sprintf(buf, "uart1");
	
}

static ssize_t rtx_store_uart1_mode(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rtx_gpio_data *gpiodata;
    gpiodata = (struct rtx_gpio_data*)dev_get_drvdata(dev);
    
	if ( gpiodata )
	{
		if (sysfs_streq(buf, "uart1"))
		{
			gpiodata->uart1_mode = 0 ;
            if ( gpiodata->pinctrl && gpiodata->pins_uart1 )
            {
                pinctrl_select_state(gpiodata->pinctrl, gpiodata->pins_uart1);
            }
		}
		if (sysfs_streq(buf, "gpio"))
		{
			gpiodata->uart1_mode = 1 ;
            if ( gpiodata->pinctrl && gpiodata->pins_uart1_gpio )
            {
                pinctrl_select_state(gpiodata->pinctrl, gpiodata->pins_uart1_gpio);
            }
		}
	}
	
	return count ;
}

/*********************************************************
 ******************DEVICE_ATTR****************************
 *********************************************************/
static DEVICE_ATTR(spi_mode,  0664, rtx_show_spi_mode,  rtx_store_spi_mode);
static DEVICE_ATTR(can_mode,  0664, rtx_show_can_mode,  rtx_store_can_mode);
static DEVICE_ATTR(uart1_mode,  0664, rtx_show_uart1_mode,  rtx_store_uart1_mode);

static struct device_attribute *rtx_rtxgpio_attr[]={
	&dev_attr_spi_mode,
    &dev_attr_can_mode,
    //&dev_attr_uart1_mode,
};

static int rtx_create_class_device(struct rtx_gpio_data *gpiodata)
{
	int i;
	int num = (int)(sizeof(rtx_rtxgpio_attr)/sizeof(rtx_rtxgpio_attr[0]));
	
	rtxgpio_major = register_chrdev(0, DEVICE_NAME, &FOPS);
	rtxgpio_class = class_create(THIS_MODULE,CLASS_NAME);
	rtxgpio_dev = device_create(rtxgpio_class, NULL, MKDEV(rtxgpio_major,0), gpiodata, DEVICE_NAME);
	for( i=0 ; i<num ;i ++)
		device_create_file(rtxgpio_dev, rtx_rtxgpio_attr[i]);
	
	return 0;
}
/*********************************************************
 ***********************dt_ids[]**************************
 ********************MODULE_DEVICE_TABLE******************
 *******************rtx_gpio_probe******************
 *******************rtx_gpio_init*******************
 *********************************************************/
static int rtx_gpio_probe(struct platform_device *pdev)
{
	struct rtx_gpio_data *gpiodata;

	gpiodata = kzalloc( sizeof(*gpiodata), GFP_KERNEL );
    printk(KERN_INFO "------%s------\n", __func__);
	
    gpiodata->pinctrl = devm_pinctrl_get(&pdev->dev);
    gpiodata->pins_spi = pinctrl_lookup_state(gpiodata->pinctrl,	"spi");
    gpiodata->pins_spi_gpio = pinctrl_lookup_state(gpiodata->pinctrl,	"spi_gpio");
    gpiodata->pins_can = pinctrl_lookup_state(gpiodata->pinctrl,	"can");
    gpiodata->pins_can_gpio = pinctrl_lookup_state(gpiodata->pinctrl,	"can_gpio");
    gpiodata->pins_uart1 = pinctrl_lookup_state(gpiodata->pinctrl,	"uart1");
    gpiodata->pins_uart1_gpio = pinctrl_lookup_state(gpiodata->pinctrl,	"uart1_gpio");
    printk(KERN_INFO "------%s------\n", __func__);
	    
	rtx_create_class_device(gpiodata);	

	return 0;
}

static const struct of_device_id rtx_gpio_dt_ids[] = {
	{ .compatible = "rtx,rtxgpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtx_esper_dt_ids);

static struct platform_driver rtx_gpio_driver = {
	.driver		= {
		.name		= "rtx-rtxgpio",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(rtx_gpio_dt_ids),
	},
};



static int __init rtx_gpio_init(void)
{
	printk(KERN_INFO "------%s------\n", __func__);
	return platform_driver_probe(&rtx_gpio_driver, rtx_gpio_probe);
}
device_initcall(rtx_gpio_init);
