
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/interrupt.h>

#define PCA953X_INPUT		0
#define PCA953X_OUTPUT		1
#define PCA953X_INVERT		2
#define PCA953X_DIRECTION	3

#define TIMEOUT_VALUE 1*HZ

#define DIGI_SIZE		3
#define NUM_SIZE		10
#define SEGBUF_SIZE		4
unsigned char digi_code[DIGI_SIZE] = { 0x03 , 0x05 , 0x06 };
unsigned char num_code[NUM_SIZE]   = { 0xC0 , 0xF9 , 0xA4 , 0xB0 , 0x99 , 0x92 , 0x82 , 0xB8 , 0x80 , 0x90 };//0~9

static struct timer_list segdisplay_timer;
struct segdisplay_data {

	struct i2c_client *client;
	struct mutex i2c_lock;
	
	unsigned char old_data[SEGBUF_SIZE];
	unsigned char new_data[SEGBUF_SIZE];
	unsigned char display_data[SEGBUF_SIZE];
};

/**********class information***********/
#define CLASS_NAME "segdisplay"
#define DEVICE_NAME "information"
static struct class * segdisplay_class = NULL;
static struct device * segdisplay_dev  = NULL;
static int segdisplay_major;
struct file_operations SEG_FOPS = {
//    .open = WMT_open,
//    .release = WMT_close,
//    .read = WMT_read,
//    .write = WMT_write,
//    .ioctl = WMT_ioctl,
//    .unlocked_ioctl = WMT_unlocked_ioctl,
//    .poll = WMT_poll,
};
/*********************************************************
 *************rtx_show/store_max_counts*******************
 *********************************************************/
static ssize_t segdisplay_show_data(struct device* dev,struct device_attribute *attr, char *buf)
{
	ssize_t res;
	struct segdisplay_data *segmentdata;
	
	segmentdata = (struct segdisplay_data*)dev_get_drvdata(dev);
	res = sprintf(buf, "%s\n", segmentdata->old_data);
	//printk(KERN_ERR "------%s------count=%d, buf=%s\n",__func__,counterdata->cur_heart_counts, buf);
	
	return res;
}
static ssize_t segdisplay_store_data(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct segdisplay_data *segmentdata;
    
    segmentdata = (struct segdisplay_data*)dev_get_drvdata(dev);
    if( count > 1 )
    	memcpy(segmentdata->new_data, buf, SEGBUF_SIZE);
    	//strncpy(segmentdata->new_data, buf, count);
    
    printk(KERN_ERR "======%s: count=%d, buf=%s, data=%s\n",__func__,count,buf,segmentdata->new_data);
    return count;
}
/*********************************************************
 ******************DEVICE_ATTR****************************
 **************rtx_create_class_device********************
 *********************************************************/
static DEVICE_ATTR(segment_data,  0664, segdisplay_show_data,  segdisplay_store_data);

static struct device_attribute *segdisplay_attr[]={
	&dev_attr_segment_data,
};

static int rtx_create_class_device(struct segdisplay_data *segmentdata)
{
	int i;
	int num = (int)(sizeof(segdisplay_attr)/sizeof(segdisplay_attr[0]));
	
	segdisplay_major = register_chrdev(0, DEVICE_NAME, &SEG_FOPS);
	segdisplay_class = class_create(THIS_MODULE,CLASS_NAME);
	segdisplay_dev = device_create(segdisplay_class, NULL, MKDEV(segdisplay_major,0), segmentdata, DEVICE_NAME);
	for( i=0 ; i<num ;i ++)
		device_create_file(segdisplay_dev, segdisplay_attr[i]);
	
	return 0;
}
/*********************************************************
 **************pca9535_write_regs*************
 *********************************************************/
static int pca9535_write_regs(struct segdisplay_data *segmentdata, int reg, u8 *val)
{
	int ret = 0;


//	ret = i2c_smbus_write_word_data(segmentdata->client, reg << 1, (u16) *val);

	if (ret < 0) {
		printk(KERN_ERR "======%s : failed writing register\n",__func__);
		return ret;
	}

	return 0;
}
/*********************************************************
 **************pca9535_init*************
 *********************************************************/
static int pca9535_init(struct segdisplay_data *segmentdata)
{
	int i;
	int ret = 0;
	unsigned char val[2] = {0x00, 0x00};
	
	/* init output value and direction=out */
	mutex_lock(&segmentdata->i2c_lock);
 	for( i=0 ; i<DIGI_SIZE ; i++ )
 	{
 		val[0] = num_code[0];
 		val[1] = digi_code[i];
 		ret = pca9535_write_regs(segmentdata, PCA953X_OUTPUT, val);
 		val[0] = 0x00;
 		val[1] = 0x00;
 		ret = pca9535_write_regs(segmentdata, PCA953X_DIRECTION, val);
 	}
	mutex_unlock(&segmentdata->i2c_lock);
	return ret;
}
/*********************************************************
 **************pca9535_refresh_display*************
 *********************************************************/
 static int pca9535_refresh_display(struct segdisplay_data *segmentdata)
 {
 	int i;
 	int ret = 0;
	unsigned char val[2] = {0x00, 0x00};

	mutex_lock(&segmentdata->i2c_lock);
	/* set output value */
 	for( i=0 ; i<DIGI_SIZE ; i++ )
 	{
  		val[0] = num_code[segmentdata->display_data[i]];
 		val[1] = digi_code[i];
 		//printk(KERN_ERR "======%s : val=%04x\n",__func__,*((unsigned short *)val));
 		ret = pca9535_write_regs(segmentdata, PCA953X_OUTPUT, val);
 	}
	mutex_unlock(&segmentdata->i2c_lock);
	return ret;

 }
/*********************************************************
 **************rtx_transfer_segment_data*************
 *********************************************************/
static int rtx_transfer_segment_data(struct segdisplay_data *segmentdata)
{
	int i;
	for ( i = 0 ; i < 3 ; i ++ )
	{
		if ( segmentdata->old_data[i] >= '0' && segmentdata->old_data[i] <= '9' )
		{
			segmentdata->display_data[i] = segmentdata->old_data[i] - '0' ;
		}
		else
			segmentdata->display_data[i] = 0;
	}
	
	return 0;
}
/*********************************************************
 **************segdisplay_timer_handler*************
 *********************************************************/
static void segdisplay_timer_handler(unsigned long data)
{
	struct segdisplay_data *segmentdata = (struct segdisplay_data *)data;
	
	
	
	if( memcmp(segmentdata->old_data, segmentdata->new_data, sizeof(segmentdata->old_data)) )
	{
//		printk(KERN_ERR "=====%s: len=%d, old=%s(%x-%x-%x-%x), new=%s(%x-%x-%x-%x)\n",__func__,sizeof(segmentdata->old_data),
//			segmentdata->old_data,segmentdata->old_data[0],segmentdata->old_data[1],segmentdata->old_data[2],segmentdata->old_data[3],
//			segmentdata->new_data,segmentdata->new_data[0],segmentdata->new_data[1],segmentdata->new_data[2],segmentdata->new_data[3]);

		memcpy(segmentdata->old_data, segmentdata->new_data, SEGBUF_SIZE);
		rtx_transfer_segment_data(segmentdata);
	}
//	printk(KERN_ERR "=====%s: display=(%x-%x-%x)\n",__func__,
//		segmentdata->display_data[0],segmentdata->display_data[1],segmentdata->display_data[2]);
	pca9535_refresh_display(segmentdata);
	
	segdisplay_timer.expires = jiffies + TIMEOUT_VALUE;
	add_timer(&segdisplay_timer);
}
/*********************************************************
 **************rtx_request_timer_handler******************
 *********************************************************/
static int rtx_request_timer_handler(struct segdisplay_data *segmentdata)
{
	init_timer(&segdisplay_timer);
	segdisplay_timer.function = segdisplay_timer_handler;
	segdisplay_timer.data = (unsigned long)segmentdata;
	segdisplay_timer.expires = jiffies + TIMEOUT_VALUE;
	add_timer(&segdisplay_timer);
	
	return 0;
}

static int pca9535_remove(struct i2c_client *client)
{

	return 0;
}

static int pca9535_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct segdisplay_data *segmentdata;
	const u32 *dt_property;
	int len;
	
	printk(KERN_ERR "=====%s=====\n",__func__);

	segmentdata = kzalloc( sizeof(*segmentdata), GFP_KERNEL );
	dt_property = of_get_property(np, "def-data", &len);
	if (dt_property)
	{
		memcpy(&segmentdata->old_data, dt_property, SEGBUF_SIZE);
		memcpy(&segmentdata->new_data, dt_property, SEGBUF_SIZE);
		rtx_transfer_segment_data(segmentdata);
	}
	
	segmentdata->client = client ;
	mutex_init(&segmentdata->i2c_lock);
	i2c_set_clientdata(client, segmentdata);
	
	rtx_create_class_device(segmentdata);
	rtx_request_timer_handler(segmentdata);
	pca9535_init(segmentdata);
	
	printk(KERN_ERR "=====%s: old=%s,new=%s, len=%d\n",__func__,segmentdata->old_data, segmentdata->new_data, len);
	return 0;
}

static const struct i2c_device_id pca9535_id[] = {
	{ "pca9535", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca9535_id);

static const struct of_device_id pca9535_dt_ids[] = {
	{ .compatible = "rtx,pca9535", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pca9535_dt_ids);

static struct i2c_driver pca9535_driver = {
	.driver = {
		.name = "segdisplay-pca9535",
		.owner	= THIS_MODULE,
		.of_match_table	= pca9535_dt_ids,
	},
	.probe = pca9535_probe,
	.remove = pca9535_remove,
	.id_table = pca9535_id,
};

module_i2c_driver(pca9535_driver);

MODULE_AUTHOR("RTX");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCA9535 7 Segment Display Driver");
