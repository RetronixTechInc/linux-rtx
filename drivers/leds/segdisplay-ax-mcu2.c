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
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/irq.h>

//#define DEBUG_ENABLE
#define SMBUS_RDWR

#ifdef DEBUG_ENABLE
#define print_ax(fmt, ...) printk(KERN_ERR fmt ,##__VA_ARGS__)
#else
#define print_ax(fmt, ...)
#endif
/******define command******/
#define SET_DISPLAY_NUM		0x01
#define GET_DISPLAY_NUM		0x02
#define DOWN_BUTTON_STATUS	0x03
#define UP_BUTTON_STATUS	0x04

#define TIMEOUT_VALUE (HZ>>1) //500ms

//#define DIGI_SIZE		3
//#define NUM_SIZE		10
#define SEGBUF_SIZE		4
#define GPIO_NUM		2

static struct timer_list segdisplay_timer;
struct segdisplay_data {

	struct i2c_client *client;
	struct mutex i2c_lock;
	
	unsigned char old_data[SEGBUF_SIZE];
	unsigned char new_data[SEGBUF_SIZE];
	unsigned char display_data[SEGBUF_SIZE];
	
	int gpio_array[GPIO_NUM];
	int trigger_edge;//0(none);1(rising);2(falling)
	int irq_array[GPIO_NUM];
	
	int mcu_wrt_flag;
	int mcu_read_flag;//0(none);1(read incline);2(read speed)
	int work_isrunning;
	struct work_struct mcu_work;
};
static struct workqueue_struct *mcu_wq;
/**********class information***********/
#define CLASS_NAME "segdisplay2"
#define DEVICE_NAME "information"
static struct class * segdisplay_class = NULL;
static struct device * segdisplay_dev  = NULL;
static int segdisplay_major;
static struct file_operations SEG_FOPS = {
//    .open = WMT_open,
//    .release = WMT_close,
//    .read = WMT_read,
//    .write = WMT_write,
//    .ioctl = WMT_ioctl,
//    .unlocked_ioctl = WMT_unlocked_ioctl,
//    .poll = WMT_poll,
};

static int segdisplay_irq_table[4]={IRQF_TRIGGER_NONE, IRQF_TRIGGER_RISING, IRQF_TRIGGER_FALLING, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING};

/*********************************************************
 **************rtx_transfer_data*************
 *********************************************************/
static int rtx_transfer_data_s2i(struct segdisplay_data *segmentdata)
{
	int i,value;
	unsigned char tmp_buf[SEGBUF_SIZE]={0};
	
	for ( i = 0 ; i < 3 ; i ++ )
	{
		tmp_buf[i] = segmentdata->new_data[i] - '0' ;
	}
	
	value = tmp_buf[0]*100 + tmp_buf[1]*10 + tmp_buf[2];
	*((int*)segmentdata->display_data) = value;

	print_ax("=====%s: value=%d, display=%d(%x-%x)\n",__func__,value, 
					*((int *)segmentdata->display_data),segmentdata->display_data[1],segmentdata->display_data[0]);

	return 0;
}
static int rtx_transfer_data_i2s(struct segdisplay_data *segmentdata , int val)
{
	int i;
	
	segmentdata->new_data[3] = 0;
	for ( i = 0 ; i < 3 ; i ++ )
	{
		segmentdata->new_data[2-i] = val % 10 + '0';
		val = val/10;
	}
	print_ax("=====%s: val=%d, new=%s, old=%s\n",__func__,val, segmentdata->new_data,segmentdata->old_data);
			
	return 0;
}
/****************************************************
 *****************ax_write_regs*****************
 *****************ax_read_regs*****************
 ****************************************************/
#ifdef SMBUS_RDWR
static int ax_write_regs(struct segdisplay_data *segmentdata, int reg)
{
	int ret = 0;
	unsigned char val[2] = {0x00, 0x00};
	
	rtx_transfer_data_s2i(segmentdata);
	
	/* set output value */
	val[0] = segmentdata->display_data[0];
	val[1] = segmentdata->display_data[1];
	
	print_ax("======%s : addr=%x, reg=%x, data=%x\n",__func__,segmentdata->client->addr, reg, *((u16 *)val));
	ret = i2c_smbus_write_word_data(segmentdata->client, reg , *((u16 *)val));
	if (ret < 0) {
		print_ax("======%s : failed writing register\n",__func__);
		memcpy(segmentdata->new_data, segmentdata->old_data, SEGBUF_SIZE);
		return ret;
	}

	return 0;
}
static int ax_read_regs(struct segdisplay_data *segmentdata, int reg)
{
	int ret = 0;

	print_ax("======%s : addr=%x, reg=%x\n",__func__,segmentdata->client->addr, reg);
	ret = i2c_smbus_read_word_data(segmentdata->client, reg);
	if (ret < 0) {
		print_ax("======%s : failed reading register\n",__func__);
		return ret;
	}
	else
	{
		rtx_transfer_data_i2s(segmentdata, ret);
	}

	return 0;	
}
#else
static int ax_write_regs(struct segdisplay_data *segmentdata, int reg)
{
	unsigned char val[3] = {0x00, 0x00, 0x00};
	struct i2c_client *client = segmentdata->client;
	rtx_transfer_data_s2i(segmentdata);
	/* set output value */
	val[0] = SET_DISPLAY_NUM;
	val[1] = segmentdata->display_data[0];
	val[2] = segmentdata->display_data[1];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.len = 3,
			.buf = val
		},
	};

	if ((i2c_transfer(client->adapter, msg, 1)) != 1)
		return -EIO;

	return 0;
}
#endif
/*********************************************************
 ***************segment_mcu_access***********************
 *********************************************************/
static void segment_mcu_access(struct work_struct *work)
{
	struct segdisplay_data *segmentdata = container_of(work, struct segdisplay_data, mcu_work);
	
	segmentdata->work_isrunning = 1;

	if(segmentdata->mcu_wrt_flag)
	{
		print_ax("======%s : wrt\n",__func__);
		ax_write_regs(segmentdata, SET_DISPLAY_NUM);
		segmentdata->mcu_wrt_flag = 0;
		print_ax("======%s : wrt end\n",__func__);
	}
	if(segmentdata->mcu_read_flag)
	{
		print_ax("======%s : read\n",__func__);
		ax_read_regs(segmentdata, GET_DISPLAY_NUM);
		if(segmentdata->mcu_read_flag > 0)
		segmentdata->mcu_read_flag -= 1;
		print_ax("======%s : read end, flag=%d\n",__func__,segmentdata->mcu_read_flag);
	}
	segmentdata->work_isrunning = 0;
}
/*********************************************************
 **************rtx_create_workqueue******************
 *********************************************************/
static int rtx_create_workqueue(struct segdisplay_data *segmentdata)
{
	mcu_wq = create_singlethread_workqueue("mcu_wq");
	if (!mcu_wq)
    {
        print_ax("=====%s: Creat workqueue failed\n",__func__);
		return -1;        
    }
    INIT_WORK(&segmentdata->mcu_work, segment_mcu_access);

    return 0;
}
/*********************************************************
 **************check_data_vaild*************
 *********************************************************/
static int check_data_vaild(struct segdisplay_data *segmentdata)
{
	int invalid_flag = 0;
	int i;
	for ( i = 0 ; i < 3 ; i ++ )
	{
		if ( segmentdata->new_data[i] < '0' || segmentdata->new_data[i] > '9' )
		{
			print_ax("=====%s: new_data[%d]=%x is invalid\n",__func__,i,segmentdata->new_data[i]);
			invalid_flag = 1;
			break;
		}
	}
	
	if( invalid_flag )
	{
		memcpy(segmentdata->new_data, segmentdata->old_data, SEGBUF_SIZE);
		return -1;
	}
	else
		return 0;
}
/*********************************************************
 **************segdisplay_timer_handler*************
 *********************************************************/
static void segdisplay_timer_handler(unsigned long data)
{
	struct segdisplay_data *segmentdata = (struct segdisplay_data *)data;
	int i;
	
	if(segmentdata->work_isrunning)//it may be impossible
	{
		print_ax("=====%s: work_isrunning\n",__func__);
		return;
	}
	
	check_data_vaild(segmentdata);
	
	if( memcmp(segmentdata->old_data, segmentdata->new_data, SEGBUF_SIZE) )
	{
		print_ax("=====%s: len=%d, old=%s(%x-%x-%x-%x), new=%s(%x-%x-%x-%x)\n",__func__,sizeof(segmentdata->old_data),
			segmentdata->old_data,segmentdata->old_data[0],segmentdata->old_data[1],segmentdata->old_data[2],segmentdata->old_data[3],
			segmentdata->new_data,segmentdata->new_data[0],segmentdata->new_data[1],segmentdata->new_data[2],segmentdata->new_data[3]);

		if( (segmentdata->mcu_wrt_flag == 0) && (segmentdata->mcu_read_flag == 0) )//it mean rd/wrt finish, so update old_data
			memcpy(segmentdata->old_data, segmentdata->new_data, SEGBUF_SIZE);
	}
	else
	{
		segmentdata->mcu_wrt_flag = 0;
	}
	
	for( i=0 ; i<GPIO_NUM ; i++)
	{
		if(gpio_get_value(segmentdata->gpio_array[i]))
		{
			segmentdata->mcu_read_flag = 1;
			break;
		}
	}

	if( segmentdata->mcu_wrt_flag||segmentdata->mcu_read_flag )
	{
		print_ax("=====%s: wrt_flag=%d, rd_flag=%d\n",__func__,segmentdata->mcu_wrt_flag, segmentdata->mcu_read_flag);
		queue_work(mcu_wq, &segmentdata->mcu_work);
	}
	
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
/*********************************************************
 **********rtx_segment_irq_handler***********************
 *********************************************************/
static irqreturn_t rtx_segment_irq_handler(int irq, void *dev_id)
{
	struct segdisplay_data *segmentdata = dev_id;
	int i;
	
	segmentdata->mcu_read_flag = 1;
	
	for( i=0 ; i<GPIO_NUM ; i++)
	{
		if(!gpio_get_value(segmentdata->gpio_array[i]))//falling trigger
		{
			segmentdata->mcu_read_flag = 2;
			break;
		}
	}
		
	print_ax("======%s: irq=%d, read_flag=%d\n",__func__,irq,segmentdata->mcu_read_flag);
	return IRQ_HANDLED;
}
/*********************************************************
 *******************rtx_request_irq***********************
 *********************************************************/
static int rtx_request_irq(struct segdisplay_data *segmentdata)
{
	int ret = -1;
	int i;
	
	for( i=0 ; i<GPIO_NUM ; i++)
	{	
		segmentdata->irq_array[i] = gpio_to_irq(segmentdata->gpio_array[i]);
    	ret  = request_irq(segmentdata->irq_array[i], 
                           rtx_segment_irq_handler,
                           segdisplay_irq_table[segmentdata->trigger_edge],
                           "rtx-segment-irq",
                           segmentdata);
    print_ax("======%s: gpio=%d, irq=%d,trigger=0x%x,ret=%d\n",
    				__func__,segmentdata->gpio_array[i],segmentdata->irq_array[i],segdisplay_irq_table[segmentdata->trigger_edge],ret);
    	if(ret)
    		break;
    }
    
    if (ret)
    {
        return -1;
    }
    else 
    {
        return 0;
    }
}
/*********************************************************
 *************rtx_show/store_max_counts*******************
 *********************************************************/
static ssize_t segdisplay_show_data(struct device* dev,struct device_attribute *attr, char *buf)
{
	ssize_t res;
	struct segdisplay_data *segmentdata;
	
	segmentdata = (struct segdisplay_data*)dev_get_drvdata(dev);
	res = sprintf(buf, "%s\n", segmentdata->old_data);
	
	return res;
}
static ssize_t segdisplay_store_data(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct segdisplay_data *segmentdata;
    
    segmentdata = (struct segdisplay_data*)dev_get_drvdata(dev);
    if( count == SEGBUF_SIZE )
    {
    	memcpy(segmentdata->new_data, buf, SEGBUF_SIZE);
    	segmentdata->new_data[SEGBUF_SIZE-1] = 0;
	    segmentdata->mcu_wrt_flag = 1;
	}
    
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
 **************ax_segment_probe******************
 *********************************************************/
static int ax_segment_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct segdisplay_data *segmentdata;
	const u32 *dt_property;
	int len;
	int ret = 0;

	print_ax("=====%s=====\n",__func__);
	
	segmentdata = kzalloc( sizeof(*segmentdata), GFP_KERNEL );
	segmentdata->gpio_array[0] = of_get_named_gpio(np, "speed-slow", 0);
	if (gpio_is_valid(segmentdata->gpio_array[0]))
	{
		segmentdata->gpio_array[1] = of_get_named_gpio(np, "speed-fast", 0);
	}
	else
	{
		segmentdata->gpio_array[0] = of_get_named_gpio(np, "incline-down", 0);
		segmentdata->gpio_array[1] = of_get_named_gpio(np, "incline-up", 0);
	}
	of_property_read_u32(np, "trigger-edge", &(segmentdata->trigger_edge));
	segmentdata->mcu_wrt_flag = 0;//if want use "def-data", it need set 1 
	segmentdata->mcu_read_flag = 0;
	segmentdata->work_isrunning = 0;	
	dt_property = of_get_property(np, "def-data", &len);
	memset(&segmentdata->old_data, 0, SEGBUF_SIZE);
	memset(&segmentdata->new_data, 0, SEGBUF_SIZE);
	if (dt_property)
	{
		memcpy(&segmentdata->new_data, dt_property, SEGBUF_SIZE);
		
		if(segmentdata->mcu_wrt_flag)
			memcpy(&segmentdata->old_data, "000", SEGBUF_SIZE);
		else
			memcpy(&segmentdata->old_data, dt_property, SEGBUF_SIZE);
	}
	print_ax("=====%s: old=%s,new=%s, len=%d\n",__func__,segmentdata->old_data, segmentdata->new_data, len);
	print_ax("=====%s: gpio[0]=%d, gpio[1]=%d, trigger_edge=%d\n",__func__,
					segmentdata->gpio_array[0], segmentdata->gpio_array[1], segmentdata->trigger_edge);
	
	segmentdata->client = client ;
//	mutex_init(&segmentdata->i2c_lock);
	i2c_set_clientdata(client, segmentdata);

	rtx_create_class_device(segmentdata);
	rtx_request_timer_handler(segmentdata);
	rtx_request_irq(segmentdata);
	ret = rtx_create_workqueue(segmentdata);
	if (ret)
		goto error_exit;
	
	return 0;

error_exit :
	return ret;
}
/*********************************************************
 **************ax_segment_remove******************
 *********************************************************/
static int ax_segment_remove(struct i2c_client *client)
{
	struct segdisplay_data *segmentdata;
	int i;
	
	print_ax("=====%s=====\n",__func__);
	
	segmentdata = i2c_get_clientdata(client);
	for( i=0 ; i<GPIO_NUM ; i++)
		free_irq(segmentdata->irq_array[i], segmentdata);
	
	del_timer_sync(&segdisplay_timer);
	
	return 0;
}


static const struct i2c_device_id ax_segment_id[] = {
	{ "ax-segment2", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ax_segment_id);

static const struct of_device_id ax_segment_dt_ids[] = {
	{ .compatible = "rtx,ax-segment2", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ax_segment_dt_ids);

static struct i2c_driver ax_segment_driver = {
	.driver = {
		.name = "ax-segment2",
		.owner	= THIS_MODULE,
		.of_match_table	= ax_segment_dt_ids,
	},
	.probe = ax_segment_probe,
	.remove = ax_segment_remove,
	.id_table = ax_segment_id,
};

module_i2c_driver(ax_segment_driver);

MODULE_AUTHOR("RTX");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AX 7 Segment Display Driver");
