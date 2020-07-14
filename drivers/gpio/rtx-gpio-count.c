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
#define ARRAY_VALUE 10
struct rtx_pulsecounter_data {
	int gpio_num;
	int irq;
	int trigger_edge;//0(none);1(rising);2(falling);3(both)
	int isworking;
	struct timeval now_time;
	struct timeval pre_time;
	int interval_ms;
	int min_count;
	int max_count;
	int cur_heart_counts;
	int ave_heart_counts;
	int pulse_counts;
	int pulse_counts_array[ARRAY_VALUE];//chg content from pulse count to pulse interval
	int array_index;
};
int irq_table[4]={IRQF_TRIGGER_NONE, IRQF_TRIGGER_RISING, IRQF_TRIGGER_FALLING, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING};

/**********irq information***********/
#define CLASS_NAME "heartbeat"
#define DEVICE_NAME "information"
static struct class * heartbeat_class = NULL;
static struct device * heartbeat_dev  = NULL;
static int heartbeat_major;
struct file_operations FOPS = {
//    .open = WMT_open,
//    .release = WMT_close,
//    .read = WMT_read,
//    .write = WMT_write,
//    .ioctl = WMT_ioctl,
//    .unlocked_ioctl = WMT_unlocked_ioctl,
//    .poll = WMT_poll,
};
/**********timer information***********/
#if 0
static struct hrtimer htimer;
static ktime_t kt_period;
#else
#define TIMEOUT_VALUE 1*HZ
static struct timer_list rtx_timer;
#endif
/*********************************************************
 **********rtx_pulsecounter_handler***********************
 *********************************************************/
static irqreturn_t rtx_pulsecounter_handler(int irq, void *dev_id)
{
	struct rtx_pulsecounter_data *counterdata = dev_id;
	int ret = 1;
	int interval_ms = 0;
	int cur_heart_counts = 0;
	 
	//printk(KERN_ERR "------%s------\n",__func__);

	counterdata->pulse_counts += 1;
	do_gettimeofday(&(counterdata->now_time));//tv_sec is accumulate from 1970/01/01/00:00

	if( counterdata->isworking == 0 )
	{
		counterdata->isworking = 1;
		ret = 0;
	}
	else
	{
		interval_ms = (counterdata->now_time.tv_sec  - counterdata->pre_time.tv_sec)*1000 +
								   (counterdata->now_time.tv_usec - counterdata->pre_time.tv_usec)/1000 ;	

		if( interval_ms > 0 )
		{
			cur_heart_counts = 60000 / interval_ms;//60sec*1000 / interval_ms
			if( (cur_heart_counts > counterdata->max_count) || 
				(cur_heart_counts < counterdata->min_count) )
				ret = 0;
		}
		else
		{
			ret = 0;
		}

		if(ret == 0)
		{
			counterdata->isworking = 0;
			
		}
	}
//	printk(KERN_ERR "------pre=(%d-%d),now=(%d-%d),interval=%d,counts=%d\n",
//						counterdata->pre_time.tv_sec,counterdata->pre_time.tv_usec,
//						counterdata->now_time.tv_sec,counterdata->now_time.tv_usec,
//						interval_ms,cur_heart_counts);

	
	counterdata->pre_time.tv_sec = counterdata->now_time.tv_sec;
	counterdata->pre_time.tv_usec = counterdata->now_time.tv_usec;
	
	if( ret  && counterdata->isworking )
	{//do update to device node
		counterdata->cur_heart_counts = cur_heart_counts;
		counterdata->interval_ms = interval_ms;
	}
	else
	{
		counterdata->cur_heart_counts = 0;
		counterdata->interval_ms = 0;
	}
//	printk(KERN_ERR "------result: ret=%d,isworking=%d,count=%d\n",ret,counterdata->isworking,cur_heart_counts);

    return IRQ_HANDLED;
}
/*********************************************************
 *******************rtx_request_irq***********************
 *********************************************************/
static int rtx_request_irq(struct rtx_pulsecounter_data *counterdata)
{
	int ret = -1;
	
	counterdata->irq = gpio_to_irq(counterdata->gpio_num);
    ret  = request_irq(counterdata->irq, 
                       rtx_pulsecounter_handler,
                       irq_table[counterdata->trigger_edge],
                       "rtx-pulser-counter",
                       counterdata);
//    printk(KERN_ERR "------request_irq------gpio=%d, irq=%d,table=0x%x,ret=%d\n",
//    				,counterdata->gpio_num,counterdata->irq,irq_table[counterdata->trigger_edge],ret);
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
 **************rtx_request_timer_handler******************
 **************rtx_count_ave_timer_handler*************
 *********************************************************/
#if 0
static enum hrtimer_restart rtx_count_ave_timer_handler(struct hrtimer *timer)
{
	printk(KERN_ERR "------%s------\n",__func__);
	//htimer.data.pulse_counts;
    hrtimer_forward_now(&htimer, ktime_set(1, 0));
    return HRTIMER_RESTART;
}

static int rtx_request_timer_handler(void)
{
	kt_period = ktime_set(1,0);//sec,nsec
	hrtimer_init(&htimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	htimer.function = rtx_count_ave_timer_handler;
	hrtimer_start(&htimer, kt_period, HRTIMER_MODE_REL);
}
#else
/* store data into array each sec and caculate as sum*60/10=n次/min  */
static void rtx_count_ave_timer_handler(unsigned long data)
{
	struct rtx_pulsecounter_data *counterdata = (struct rtx_pulsecounter_data *)data;
	int i = 0;
	int sum = 0;
	int valid_cnt = 0;
//	printk(KERN_ERR "------%s------,counts=%d\n",__func__,counterdata->pulse_counts);
	if( counterdata->interval_ms == 0 )//if there is no gpio irq event, we will clean here 
	{
		counterdata->cur_heart_counts = 0;
	}
	counterdata->pulse_counts_array[counterdata->array_index] = counterdata->interval_ms;
	counterdata->interval_ms = 0;
	counterdata->pulse_counts = 0;
	counterdata->array_index += 1;
	if(counterdata->array_index == ARRAY_VALUE)
		counterdata->array_index = 0;
	
	for( i=0 ; i<ARRAY_VALUE ; i++)
	{
		if( counterdata->pulse_counts_array[i]>0 )
		{
			valid_cnt += 1;
			sum += counterdata->pulse_counts_array[i];
		}
	}	
	//sum = sum*60/ARRAY_VALUE;//this is for count pulse
	if( valid_cnt == ARRAY_VALUE )
	{
		counterdata->ave_heart_counts = 60000 * ARRAY_VALUE / sum; // 60sec*1000 / (sum_interval_ms/10)
	}
	else
	{
		counterdata->ave_heart_counts = 0;
	}
//	printk(KERN_ERR "------%s------,(%d,%d,%d,%d,%d,%d,%d,%d,%d,%d),ave=%d,idx=%d\n",__func__,counterdata->pulse_counts_array[0],
//			counterdata->pulse_counts_array[1],counterdata->pulse_counts_array[2],counterdata->pulse_counts_array[3],
//			counterdata->pulse_counts_array[4],counterdata->pulse_counts_array[5],counterdata->pulse_counts_array[6],
//			counterdata->pulse_counts_array[7],counterdata->pulse_counts_array[8],counterdata->pulse_counts_array[9],
//			counterdata->ave_heart_counts,counterdata->array_index);
	rtx_timer.expires = jiffies + TIMEOUT_VALUE;
	add_timer(&rtx_timer);
}
static int rtx_request_timer_handler(struct rtx_pulsecounter_data *counterdata)
{
	init_timer(&rtx_timer);
	rtx_timer.function = rtx_count_ave_timer_handler;
	rtx_timer.data = (unsigned long)counterdata;
	rtx_timer.expires = jiffies + TIMEOUT_VALUE;
	add_timer(&rtx_timer);
	
	return 0;
}
#endif
/*********************************************************
 *************rtx_show_cur_counts*************************
 *************rtx_show_ave_counts*************************
 *************rtx_show/store_min_counts*******************
 *************rtx_show/store_max_counts*******************
 *********************************************************/
static ssize_t rtx_show_cur_counts(struct device* dev,struct device_attribute *attr, char *buf)
{
	ssize_t res;
	struct rtx_pulsecounter_data *counterdata;
	
	counterdata = (struct rtx_pulsecounter_data*)dev_get_drvdata(dev);
	res = sprintf(buf, "%d\n", counterdata->cur_heart_counts);
	//printk(KERN_ERR "------%s------count=%d, buf=%s, res=%d\n",__func__,counterdata->cur_heart_counts, buf, res);
	
	return res;
}

static ssize_t rtx_show_ave_counts(struct device* dev,struct device_attribute *attr, char *buf)
{
	ssize_t res;
	struct rtx_pulsecounter_data *counterdata;
	
	counterdata = (struct rtx_pulsecounter_data*)dev_get_drvdata(dev);
	res = sprintf(buf, "%d\n", counterdata->ave_heart_counts);
	//printk(KERN_ERR "------%s------count=%d, buf=%s, res=%d\n",__func__,counterdata->ave_heart_counts, buf,res);
	
	return res;
}

static ssize_t rtx_show_limit_min(struct device* dev,struct device_attribute *attr, char *buf)
{
	ssize_t res;
	struct rtx_pulsecounter_data *counterdata;
	
	counterdata = (struct rtx_pulsecounter_data*)dev_get_drvdata(dev);
	res = sprintf(buf, "%d\n", counterdata->min_count);
	//printk(KERN_ERR "------%s------count=%d, buf=%s\n",__func__,counterdata->cur_heart_counts, buf);
	
	return res;
}
static ssize_t rtx_show_limit_max(struct device* dev,struct device_attribute *attr, char *buf)
{
	ssize_t res;
	struct rtx_pulsecounter_data *counterdata;
	
	counterdata = (struct rtx_pulsecounter_data*)dev_get_drvdata(dev);
	res = sprintf(buf, "%d\n", counterdata->max_count);
	//printk(KERN_ERR "------%s------count=%d, buf=%s\n",__func__,counterdata->cur_heart_counts, buf);
	
	return res;
}
static ssize_t rtx_store_limit_min(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rtx_pulsecounter_data *counterdata;
    char *endptr;
    
    counterdata = (struct rtx_pulsecounter_data*)dev_get_drvdata(dev);
    if( count > 1 )
    	counterdata->min_count = simple_strtol(buf,&endptr,10);
    
    //printk(KERN_ERR "------%s------count=%d,buf=%s,ptr=%s,min=%d\n",__func__,count,buf,endptr,counterdata->min_count);
    return count;
}
static ssize_t rtx_store_limit_max(struct device* dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rtx_pulsecounter_data *counterdata;
    char *endptr;
    
    counterdata = (struct rtx_pulsecounter_data*)dev_get_drvdata(dev);
    if( count > 1 )
    	counterdata->max_count = simple_strtol(buf,&endptr,10);
    
    //printk(KERN_ERR "------%s------count=%d,buf=%s,ptr=%s,max=%d\n",__func__,count,buf,endptr,counterdata->max_count);
    return count;
}
/*********************************************************
 ******************DEVICE_ATTR****************************
 **************rtx_create_class_device********************
 *********************************************************/
static DEVICE_ATTR(cur_counts, 0444, rtx_show_cur_counts, NULL);
static DEVICE_ATTR(ave_counts, 0444, rtx_show_ave_counts, NULL);
static DEVICE_ATTR(limit_min,  0664, rtx_show_limit_min,  rtx_store_limit_min);
static DEVICE_ATTR(limit_max,  0664, rtx_show_limit_max,  rtx_store_limit_max);
static struct device_attribute *rtx_heartbeat_attr[]={
	&dev_attr_cur_counts,
	&dev_attr_ave_counts,
	&dev_attr_limit_min,
	&dev_attr_limit_max,
};

static int rtx_create_class_device(struct rtx_pulsecounter_data *counterdata)
{
	int i;
	int num = (int)(sizeof(rtx_heartbeat_attr)/sizeof(rtx_heartbeat_attr[0]));
	
	heartbeat_major = register_chrdev(0, DEVICE_NAME, &FOPS);
	heartbeat_class = class_create(THIS_MODULE,CLASS_NAME);
	heartbeat_dev = device_create(heartbeat_class, NULL, MKDEV(heartbeat_major,0), counterdata, DEVICE_NAME);
	for( i=0 ; i<num ;i ++)
		device_create_file(heartbeat_dev, rtx_heartbeat_attr[i]);
	
	return 0;
}
/*********************************************************
 ***********************dt_ids[]**************************
 ********************MODULE_DEVICE_TABLE******************
 *******************rtx_pulsecount_probe******************
 *******************rtx_pulsecount_init*******************
 *********************************************************/
static int rtx_pulsecount_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rtx_pulsecounter_data *counterdata;

	
	counterdata = kzalloc( sizeof(*counterdata), GFP_KERNEL );
	counterdata->gpio_num = of_get_named_gpio(np, "gpio-num", 0);
	if (!gpio_is_valid(counterdata->gpio_num)) {
		printk(KERN_ERR "------pulsecount_probe------ get gpio error!!\n");
		return -ENODEV;
	}
	of_property_read_u32(np, "trigger-edge", &(counterdata->trigger_edge));
	of_property_read_u32(np, "min-count", &(counterdata->min_count));
	of_property_read_u32(np, "max-count", &(counterdata->max_count));
	printk(KERN_ERR "------pulsecount_probe------gpio=%d, edge=%d, mincnout=%d, maxcnout=%d\n",
			counterdata->gpio_num,counterdata->trigger_edge,counterdata->min_count,counterdata->max_count);

	counterdata->cur_heart_counts = 0;
	counterdata->isworking = 0;
	counterdata->array_index = 0;
	
	rtx_request_irq(counterdata);
	rtx_request_timer_handler(counterdata);

	rtx_create_class_device(counterdata);	


	return 0;
}

static const struct of_device_id rtx_pulsecount_dt_ids[] = {
	{ .compatible = "rtx,pulsecounter", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtx_pulsecounter_dt_ids);

static struct platform_driver rtx_pulsecounter_driver = {
	.driver		= {
		.name		= "rtx-pulsecounter",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(rtx_pulsecount_dt_ids),
	},
};



static int __init rtx_pulsecount_init(void)
{
	printk(KERN_ERR "------pulsecount_init------\n");
	return platform_driver_probe(&rtx_pulsecounter_driver, rtx_pulsecount_probe);
}
device_initcall(rtx_pulsecount_init);
