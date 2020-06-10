/*
 * gpio_counter.c
 *
 * (c) 2017 Paweł Knioła <pawel.kn@gmail.com>
 *
 * Generic GPIO impulse counter. Counts impulses using GPIO interrupts.
 * See file: Documentation/input/misc/gpio-counter.txt for more information.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/time.h>

#include <linux/uaccess.h>

#define DRV_NAME "gpio-counter"

struct gpio_counter_platform_data {
    int gpio;
    bool inverted;
    u32 debounce_us;
};

struct gpio_counter {
    const struct gpio_counter_platform_data *pdata;
    struct miscdevice miscdev;    
    struct delayed_work work;

    int tire_speed;
    int tire_width;
    int tire_ratio;
    int tire_diameter;
    int irq;
    s64 tire_pp;
    s64 tire_circle_pp;
    s64 tire_pptime;
    s64 count;
    s64 p1_count;
    s64 last_ns;
    s64 p1time_ns;
    s64 pptime_ns;
    s64 pptime_limit;
    bool last_state;
    bool last_stable_state;
};

static ssize_t gpio_counter_read(struct file *file, char __user * userbuf, size_t count, loff_t * ppos)
{
    struct miscdevice *miscdev = file->private_data;
    struct device *dev = miscdev->parent;
    struct gpio_counter *counter = dev_get_drvdata(dev);

    char buf[22];
    int len = sprintf(buf, "%llu\n", counter->count);

    if ((len < 0) || (len > count))
        return -EINVAL;

    if (*ppos != 0)
        return 0;

    if (copy_to_user(userbuf, buf, len))
        return -EINVAL;

    //printk("last_stable_state=%d,last_state=%d\n",counter->last_stable_state,counter->last_state);

    *ppos = len;
    return len;
}

static ssize_t gpio_counter_write(struct file *file, const char __user * userbuf, size_t count, loff_t * ppos)
{
    struct miscdevice *miscdev = file->private_data;
    struct device *dev = miscdev->parent;
    struct gpio_counter *counter = dev_get_drvdata(dev);

    if (kstrtoull_from_user(userbuf, count, 0, &counter->count))
        return -EINVAL;

    return count;
}

static struct file_operations gpio_counter_fops = {
    .owner = THIS_MODULE,
    .read = gpio_counter_read,
    .write = gpio_counter_write,
};

static bool gpio_counter_get_state(const struct gpio_counter_platform_data *pdata)
{
    bool state = gpio_get_value(pdata->gpio);
    if (pdata->inverted)
        state = !state;

    return state;
}

static s64 gpio_counter_get_time_nsec(void)
{
    struct timespec ts;
    getnstimeofday (&ts);
    return timespec_to_ns (&ts);
}

static void gpio_counter_process_state_change(struct gpio_counter *counter)
{
    s64 current_ns;
    s64 delta_ns = 0;
    s64 debounce_ns;

    current_ns = gpio_counter_get_time_nsec();
    delta_ns = current_ns - counter->last_ns;
    debounce_ns = (s64)counter->pdata->debounce_us * NSEC_PER_USEC;

    if (delta_ns > debounce_ns) {
	counter->count++;
	if ((counter->count - counter->p1_count) >= counter->tire_pp){
	    counter->tire_pptime = current_ns - counter->p1time_ns;
	    counter->p1time_ns = current_ns;
	    counter->p1_count = counter->count;

	}
	counter->pptime_ns = delta_ns;
    }

    counter->last_ns = current_ns;
}

static void gpio_counter_delayed_work(struct work_struct *work)
{
    struct gpio_counter *counter = container_of(work, struct gpio_counter, work.work);
    	counter->tire_pptime=counter->pptime_limit*1000;
}

static irqreturn_t gpio_counter_irq(int irq, void *dev_id)
{
    struct gpio_counter *counter = dev_id;
    gpio_counter_process_state_change (counter);

    if (delayed_work_pending (&counter->work))
        cancel_delayed_work (&counter->work);

    schedule_delayed_work (&counter->work,
        usecs_to_jiffies (counter->pptime_limit));

    return IRQ_HANDLED;
}

static const struct of_device_id gpio_counter_of_match[] = {
    { .compatible = "gpio-counter", },
    { },
};
MODULE_DEVICE_TABLE(of, gpio_counter_of_match);

static struct gpio_counter_platform_data *gpio_counter_parse_dt(struct device *dev)
{
    const struct of_device_id *of_id =
                of_match_device(gpio_counter_of_match, dev);
    struct device_node *np = dev->of_node;
    struct gpio_counter_platform_data *pdata;
    enum of_gpio_flags flags;
    int err;

    if (!of_id || !np)
        return NULL;

    pdata = kzalloc(sizeof(struct gpio_counter_platform_data), GFP_KERNEL);
    if (!pdata)
        return ERR_PTR(-ENOMEM);

    pdata->gpio = of_get_gpio_flags(np, 0, &flags);
    pdata->inverted = flags & OF_GPIO_ACTIVE_LOW;

    err = of_property_read_u32(np, "debounce-delay-us", &pdata->debounce_us);
    if (err)
        pdata->debounce_us = 0;

    return pdata;
}

static ssize_t tire_pptime_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", counter->tire_pptime);
}

static ssize_t tire_pp_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", counter->tire_pp);
}

static ssize_t tire_pp_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);
    int pp=1,pp_h=128,pp_l=1;

    pp=simple_strtol(buf,NULL,0);

    if ( pp > pp_h){
		printk("Cannot set pp number of times over %d(%d) \n",pp_h,pp);
		return -EINVAL;
	}
    if ( pp < pp_l)	return -EINVAL;

    printk("Set pp number of times = %d \n",pp);
    counter->tire_pp = pp;

    return count;
 }

static ssize_t tire_circle_pp_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n", counter->tire_circle_pp);
}

static ssize_t tire_circle_pp_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);
    int circle=4,circle_h=128,circle_l=4;

    circle=simple_strtol(buf,NULL,0);

    if ( circle > circle_h){
		printk("Cannot set pp number over %d(%d) in a circle. \n",circle_h,circle);
		return -EINVAL;
	}else if ( circle < circle_l){
		printk("Cannot set pp number too low %d(%d) in a circle. \n",circle_h,circle);
		return -EINVAL;
	}
    printk("Set pp number in a circle = %d \n",circle);
    counter->tire_circle_pp = circle;

    return count;
 }

static ssize_t tire_speed_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

    int speed_h=180;
    long length,time;
    int length1,length2;

	if(counter->tire_pptime < 50000)	
		counter->tire_pptime= 50000;

	if(counter->tire_pptime >= counter->pptime_limit*1000){
		counter->tire_speed= 0;
	}else{
		length1=counter->tire_width*counter->tire_ratio*2;	//outer diameter 0.01mm
		length2=counter->tire_diameter*2540;			//inner diameter 0.01mm
		length = (length1+length2);				//diameter 0.01mm
		time = counter->tire_pptime*counter->tire_circle_pp;	//Circumference time nsec
		counter->tire_speed= (length*113112)/time;		//speed km/h 3.142
	}

	if ( counter->tire_speed > speed_h) {
		printk("width/ratio/diameter=%d/%d/%d\n",counter->tire_width,counter->tire_ratio,counter->tire_diameter);
		printk("circle length (%ld) /circle time(%ld)nsec\n",length,time);
		printk("tire_speed over %d(%d)km/hour \n",speed_h,counter->tire_speed);
		counter->tire_speed=speed_h;
	}else if(counter->tire_pptime == counter->pptime_limit*1000){
		counter->tire_speed= 0;
	}else if(counter->tire_speed < 0){
		printk("width/ratio/diameter=%d/%d/%d\n",counter->tire_width,counter->tire_ratio,counter->tire_diameter);
		printk("tire_speed error km/hour \n");
		counter->tire_speed= 0;
	}

	return sprintf(buf, "%d\n", counter->tire_speed);
}

static ssize_t tire_width_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", counter->tire_width);
}

static ssize_t tire_width_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);
    int width=200,width_h=315,width_l=200;

    width=simple_strtol(buf,NULL,0);

    if ( width > width_h){
		printk("Cannot set tire_width over %d(%d) \n",width_h,width);
		return -EINVAL;
	}
    if ( width < width_l){
		printk("Cannot set tire_width too low %d(%d) \n",width_l,width);
		return -EINVAL;
	}
    printk("Set tire_width = %d \n",width);
    counter->tire_width = width;

    return count;
 }

static ssize_t tire_ratio_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", counter->tire_ratio);
}

static ssize_t tire_ratio_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);
    int ratio=60,ratio_h=80,ratio_l=40;

    ratio=simple_strtol(buf,NULL,0);

    if ( ratio > ratio_h){
		printk("Cannot set tire_ratio over %d(%d) \n",ratio_h,ratio);
		return -EINVAL;
	}
    if ( ratio < ratio_l){
		printk("Cannot set tire_ratio too low %d(%d) \n",ratio_l,ratio);
		return -EINVAL;
	}
    printk("Set tire_ratio = %d \n",ratio);
    counter->tire_ratio = ratio;

    return count;
 }

static ssize_t tire_diameter_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", counter->tire_diameter);
}

static ssize_t tire_diameter_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);
    int diameter=18,diameter_h=23,diameter_l=15;

    diameter=simple_strtol(buf,NULL,0);

    if ( diameter > diameter_h){
		printk("Cannot set tire_diameter over %d(%d) \n",diameter_h,diameter);
		return -EINVAL;
	}
    if ( diameter < diameter_l){
		printk("Cannot set tire_diameter too low %d(%d) \n",diameter_l,diameter);
		return -EINVAL;
	}
    printk("Set tire_diameter = %d \n",diameter);
    counter->tire_diameter = diameter;

    return count;
 }
static DEVICE_ATTR(tire_pptime, S_IWUSR | S_IRUGO,
		   tire_pptime_show,NULL);
static DEVICE_ATTR(tire_pp, S_IWUSR | S_IRUGO,
		   tire_pp_show,tire_pp_store);
static DEVICE_ATTR(tire_circle_pp, S_IWUSR | S_IRUGO,
		   tire_circle_pp_show,tire_circle_pp_store);
static DEVICE_ATTR(tire_speed, S_IWUSR | S_IRUGO,
		   tire_speed_show,NULL);
static DEVICE_ATTR(tire_width, S_IWUSR | S_IRUGO,
		   tire_width_show,tire_width_store);
static DEVICE_ATTR(tire_ratio, S_IWUSR | S_IRUGO,
		   tire_ratio_show,tire_ratio_store);
static DEVICE_ATTR(tire_diameter, S_IWUSR | S_IRUGO,
		   tire_diameter_show,tire_diameter_store);

static struct attribute *tire_input_attr_group[] = {
	&dev_attr_tire_pptime.attr,
	&dev_attr_tire_pp.attr,
	&dev_attr_tire_circle_pp.attr,
	&dev_attr_tire_speed.attr,
	&dev_attr_tire_width.attr,
	&dev_attr_tire_ratio.attr,
	&dev_attr_tire_diameter.attr,
	NULL
};

static const struct attribute_group counter_input_attr_group = {
	.attrs = tire_input_attr_group,
};

static int gpio_counter_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    const struct gpio_counter_platform_data *pdata = dev_get_platdata(dev);
    struct gpio_counter *counter;
    int err;

    if (!pdata) {
        pdata = gpio_counter_parse_dt(dev);
        if (IS_ERR(pdata))
            return PTR_ERR(pdata);

        if (!pdata) {
            dev_err(dev, "missing platform data\n");
            return -EINVAL;
        }
    }

    counter = kzalloc(sizeof(struct gpio_counter), GFP_KERNEL);
    if (!counter) {
        err = -ENOMEM;
        goto exit_free_mem;
    }

    counter->pdata = pdata;
    counter->count = 0;
    counter->p1_count = 0;
    counter->p1time_ns = 1;
    counter->pptime_ns = 1;
    counter->pptime_limit = 550000;
    counter->tire_pptime = 550000000;
    counter->tire_pp = 1;
    counter->tire_circle_pp = 4;
    counter->tire_speed = 0;
    counter->tire_width = 280;
    counter->tire_ratio = 70;
    counter->tire_diameter = 20;

    err = gpio_request_one(pdata->gpio, GPIOF_IN, dev_name(dev));

    if (err) {
        dev_err(dev, "unable to request GPIO %d\n", pdata->gpio);
        goto exit_free_mem;
    }

    counter->last_stable_state = gpio_counter_get_state (pdata);
    counter->last_state = counter->last_stable_state;
    counter->irq = gpio_to_irq (pdata->gpio);
    counter->last_ns = gpio_counter_get_time_nsec();
    //printk(">>>>pdata->gpio=%d,last_stable_state=%d,last_state=%d,last_ns=%lld\n", pdata->gpio,counter->last_stable_state,counter->last_state,counter->last_ns);

    err = request_irq(counter->irq, &gpio_counter_irq,
                      IRQF_TRIGGER_RISING,
                      DRV_NAME, counter);
    if (err) {
        dev_err(dev, "unable to request IRQ %d\n", counter->irq);
        goto exit_free_gpio;
    }

    counter->miscdev.minor  = MISC_DYNAMIC_MINOR;
    counter->miscdev.name   = dev_name(dev);
    counter->miscdev.fops   = &gpio_counter_fops;
    counter->miscdev.parent = dev;

    INIT_DELAYED_WORK(&counter->work, gpio_counter_delayed_work);

    err = misc_register(&counter->miscdev);
    if (err) {
        dev_err(dev, "failed to register misc device\n");
        goto exit_free_irq;
    }

    dev_set_drvdata(dev, counter);
    dev_info(dev, "registered new misc device %s\n", counter->miscdev.name );

    /* register sysfs for detector */
    err = sysfs_create_group(&counter->miscdev.parent->kobj,&counter_input_attr_group);
    if (err)
	goto exit_free_irq;

    return 0;

exit_free_irq:
    free_irq(counter->irq, counter);
exit_free_gpio:
    gpio_free(pdata->gpio);
exit_free_mem:
    kfree(counter);
    if (!dev_get_platdata(&pdev->dev))
        kfree(pdata);

    return err;
}

static int gpio_counter_remove(struct platform_device *pdev)
{
    struct gpio_counter *counter = platform_get_drvdata(pdev);
    const struct gpio_counter_platform_data *pdata = counter->pdata;

    device_init_wakeup(&pdev->dev, false);

    if (delayed_work_pending(&counter->work))
        cancel_delayed_work(&counter->work);

    misc_deregister(&counter->miscdev);
    free_irq(counter->irq, counter);
    gpio_free(pdata->gpio);
    kfree(counter);
    if (!dev_get_platdata(&pdev->dev))
        kfree(pdata);

    return 0;
}

static int gpio_counter_suspend(struct device *dev)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

    if (device_may_wakeup(dev))
        disable_irq_wake(counter->irq);

    return 0;
}

static int gpio_counter_resume(struct device *dev)
{
    struct gpio_counter *counter = dev_get_drvdata(dev);

    if (device_may_wakeup(dev))
        enable_irq_wake(counter->irq);

    return 0;
}

static SIMPLE_DEV_PM_OPS(gpio_counter_pm_ops,
         gpio_counter_suspend, gpio_counter_resume);

static struct platform_driver gpio_counter_driver = {
    .probe		= gpio_counter_probe,
    .remove		= gpio_counter_remove,
    .driver		= {
        .name	= DRV_NAME,
        .pm	= &gpio_counter_pm_ops,
        .of_match_table = of_match_ptr(gpio_counter_of_match),
    }
};
module_platform_driver(gpio_counter_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Generic GPIO counter driver");
MODULE_AUTHOR("3110");
