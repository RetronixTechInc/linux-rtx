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
struct rtx_esp_data {
	int gpio_num;
	int irq;
	int trigger_edge;//0(none);1(rising);2(falling);3(both)
	int esp_state;
};
static int irq_table[4]={IRQF_TRIGGER_NONE, IRQF_TRIGGER_RISING, IRQF_TRIGGER_FALLING, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING};

/**********irq information***********/
#define CLASS_NAME "safetyesp"
#define DEVICE_NAME "information"
static struct class * safetyesp_class = NULL;
static struct device * safetyesp_dev  = NULL;
static int safetyesp_major;
static struct file_operations FOPS = {
//    .open = WMT_open,
//    .release = WMT_close,
//    .read = WMT_read,
//    .write = WMT_write,
//    .ioctl = WMT_ioctl,
//    .unlocked_ioctl = WMT_unlocked_ioctl,
//    .poll = WMT_poll,
};
/*********************************************************
 **********rtx_esp_handler***********************
 *********************************************************/
static irqreturn_t rtx_esp_handler(int irq, void *dev_id)
{
	struct rtx_esp_data *espdata = dev_id;
	 
	printk(KERN_DEBUG "------%s------\n", __func__);
	
	espdata->esp_state += 1;
	if(espdata->esp_state == 0)
	{
		espdata->esp_state += 1;
	}

    return IRQ_HANDLED;
}
/*********************************************************
 *******************rtx_request_irq***********************
 *********************************************************/
static int rtx_request_irq(struct rtx_esp_data *espdata)
{
	int ret = -1;
	
	espdata->irq = gpio_to_irq(espdata->gpio_num);
    ret  = request_irq(espdata->irq, 
                       rtx_esp_handler,
                       irq_table[espdata->trigger_edge],
                       "rtx-safetyesp",
                       espdata);

    return ret;
    
}
/*********************************************************
 *************rtx_show_cur_counts*************************
 *************rtx_show_ave_counts*************************
 *********************************************************/
static ssize_t rtx_show_safetyesp(struct device* dev,struct device_attribute *attr, char *buf)
{
	ssize_t res;
	struct rtx_esp_data *espdata;
	
	espdata = (struct rtx_esp_data*)dev_get_drvdata(dev);
	res = sprintf(buf, "%d\n", espdata->esp_state);
	
	espdata->esp_state = 0;
	
	return res;
}

static ssize_t rtx_store_safetyesp(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rtx_esp_data *espdata;
    int ret, val;
    
    ret = sscanf(buf, "%d", &val);
    
    
    if( ret == 1 )
    {
		espdata = (struct rtx_esp_data*)dev_get_drvdata(dev);
		espdata->esp_state = val;
	}
	
	printk(KERN_INFO "val=%d\n", val);
	
    return count;
}

/*********************************************************
 ******************DEVICE_ATTR****************************
 **************rtx_create_class_device********************
 *********************************************************/
static DEVICE_ATTR(safetyesp,  0664, rtx_show_safetyesp,  rtx_store_safetyesp);
static struct device_attribute *rtx_safetyesp_attr[]={
	&dev_attr_safetyesp,
};

static int rtx_create_class_device(struct rtx_esp_data *espdata)
{
	int i;
	int num = (int)(sizeof(rtx_safetyesp_attr)/sizeof(rtx_safetyesp_attr[0]));
	
	safetyesp_major = register_chrdev(0, DEVICE_NAME, &FOPS);
	safetyesp_class = class_create(THIS_MODULE,CLASS_NAME);
	safetyesp_dev = device_create(safetyesp_class, NULL, MKDEV(safetyesp_major,0), espdata, DEVICE_NAME);
	for( i=0 ; i<num ;i ++)
		device_create_file(safetyesp_dev, rtx_safetyesp_attr[i]);
	
	return 0;
}
/*********************************************************
 ***********************dt_ids[]**************************
 ********************MODULE_DEVICE_TABLE******************
 *******************rtx_esp_probe******************
 *******************rtx_esp_init*******************
 *********************************************************/
static int rtx_esp_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rtx_esp_data *espdata;

	
	espdata = kzalloc( sizeof(*espdata), GFP_KERNEL );
	espdata->gpio_num = of_get_named_gpio(np, "gpio-num", 0);
	if (!gpio_is_valid(espdata->gpio_num)) {
		printk(KERN_ERR "------%s------ get gpio error!!\n", __func__);
		return -ENODEV;
	}
	of_property_read_u32(np, "trigger-edge", &(espdata->trigger_edge));
	printk(KERN_INFO "------%s------gpio=%d, edge=%d\n", __func__, espdata->gpio_num,espdata->trigger_edge);
	
	rtx_request_irq(espdata);
	rtx_create_class_device(espdata);	

	return 0;
}

static const struct of_device_id rtx_esp_dt_ids[] = {
	{ .compatible = "rtx,safetyesp", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtx_esper_dt_ids);

static struct platform_driver rtx_esp_driver = {
	.driver		= {
		.name		= "rtx-safetyesp",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(rtx_esp_dt_ids),
	},
};



static int __init rtx_esp_init(void)
{
	printk(KERN_INFO "------%s------\n", __func__);
	return platform_driver_probe(&rtx_esp_driver, rtx_esp_probe);
}
device_initcall(rtx_esp_init);
