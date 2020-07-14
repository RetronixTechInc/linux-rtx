/*
 * Copyright (C) 2011-2012 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2014 Boundary Devices
 */

/*
 * Modifyed by: Edison Fernández <edison.fernandez@ridgerun.com>
 * Added support to use it with Nitrogen6x
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#define DEBUG 1
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/fsl_devices.h>
#include <linux/mutex.h>
#include <linux/mipi_csi2.h>
#include <media/v4l2-chip-ident.h>
#include "v4l2-int-device.h"
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-dapm.h>
#include <asm/mach-types.h>
#include "../../../../../sound/soc/fsl/imx-audmux.h"
#include "../../../../../sound/soc/fsl/fsl_ssi.h"
#include <linux/slab.h>
#include "mxc_v4l2_capture.h"


#define DEFAULT_FPS 30
#define DRIVER_NAME     "BT1120"

/*!
 * Maintains the information on the current state of the sensor.
 */
struct tc_data {
	struct sensor_data sensor;
    int select_gpio;
	u32 fps;

};

static struct tc_data *g_td;


static int bt1120_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int bt1120_remove(struct i2c_client *client);

static const struct i2c_device_id bt1120_id[] = {
	{DRIVER_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, bt1120_id);

static struct i2c_driver bt1120_i2c_driver = {
	.driver = {
		  .owner = THIS_MODULE,
		  .name  = DRIVER_NAME,
		  },
	.probe  = bt1120_probe,
	.remove = bt1120_remove,
	.id_table = bt1120_id,
};

/*!
 * ioctl_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	//printk(KERN_ERR "tom===%s===%d\n" , __func__, __LINE__);
	pr_debug("%s type: %x\n", __func__, a->type);
	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}
	pr_debug("%s done %d\n", __func__, ret);
	return ret;
}

/*!
 * ioctl_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	int ret = 0;

	pr_debug("%s\n", __func__);
    pr_debug("   type is not V4L2_BUF_TYPE_VIDEO_CAPTURE but %d\n",	a->type);
		
	return ret;
}

/*!
 * ioctl_g_ctrl - V4L2 sensor interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_G_CTRL ioctl structure
 *
 * If the requested control is supported, returns the control's current
 * value from the video_control[] array.  Otherwise, returns -EINVAL
 * if the control is not supported.
 */
static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	int ret = 0;

	//printk(KERN_ERR "tom===%s===%d\n" , __func__, __LINE__);
	pr_debug("%s\n", __func__);
	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		vc->value = sensor->brightness;
		break;
	case V4L2_CID_HUE:
		vc->value = sensor->hue;
		break;
	case V4L2_CID_CONTRAST:
		vc->value = sensor->contrast;
		break;
	case V4L2_CID_SATURATION:
		vc->value = sensor->saturation;
		break;
	case V4L2_CID_RED_BALANCE:
		vc->value = sensor->red;
		break;
	case V4L2_CID_BLUE_BALANCE:
		vc->value = sensor->blue;
		break;
	case V4L2_CID_EXPOSURE:
		vc->value = sensor->ae_mode;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/*!
 * ioctl_s_ctrl - V4L2 sensor interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_S_CTRL ioctl structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW (and updates the video_control[] array).  Otherwise,
 * returns -EINVAL if the control is not supported.
 */
static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int retval = 0;

	//printk(KERN_ERR "tom===%s===%d\n" , __func__, __LINE__);
	pr_debug("In bt1120:ioctl_s_ctrl %d\n",
		 vc->id);

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HUE:
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		break;
	case V4L2_CID_RED_BALANCE:
		break;
	case V4L2_CID_BLUE_BALANCE:
		break;
	case V4L2_CID_GAMMA:
		break;
	case V4L2_CID_EXPOSURE:
		break;
	case V4L2_CID_AUTOGAIN:
		break;
	case V4L2_CID_GAIN:
		break;
	case V4L2_CID_HFLIP:
		break;
	case V4L2_CID_VFLIP:
		break;
	default:
		retval = -EPERM;
		break;
	}

	return retval;
}

/*!
 * ioctl_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *fsize)
{
	struct tc_data *td = s->priv;
    struct sensor_data *sensor = &td->sensor;

    if (fsize->index > 1)
		return -EINVAL;

	//printk(KERN_ERR "tom===%s===%d\n" , __func__, __LINE__);
	fsize->pixel_format = sensor->pix.pixelformat;
	fsize->discrete.width = 1280;
	fsize->discrete.height = 720;
	pr_debug("%s %d:%d format: %x\n", __func__, fsize->discrete.width, fsize->discrete.height, fsize->pixel_format);
    
	return 0;
}

/*!
 * ioctl_g_chip_ident - V4L2 sensor interface handler for
 *			VIDIOC_DBG_G_CHIP_IDENT ioctl
 * @s: pointer to standard V4L2 device structure
 * @id: pointer to int
 *
 * Return 0.
 */
static int ioctl_g_chip_ident(struct v4l2_int_device *s, int *id)
{
	//printk(KERN_ERR "tom===%s===%d\n" , __func__, __LINE__);
    pr_debug("%s\n", __func__);
    ((struct v4l2_dbg_chip_ident *)id)->match.type = V4L2_CHIP_MATCH_I2C_DRIVER;
	strcpy(((struct v4l2_dbg_chip_ident *)id)->match.name,"BT1120");
	return 0;
}

/*!
 * ioctl_init - V4L2 sensor interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 */
static int ioctl_init(struct v4l2_int_device *s)
{
	pr_debug("%s\n", __func__);
	return 0;
}

/*!
 * ioctl_enum_fmt_cap - V4L2 sensor interface handler for VIDIOC_ENUM_FMT
 * @s: pointer to standard V4L2 device structure
 * @fmt: pointer to standard V4L2 fmt description structure
 *
 * Return 0.
 */
static int ioctl_enum_fmt_cap(struct v4l2_int_device *s,
			      struct v4l2_fmtdesc *fmt)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;

	fmt->pixelformat = sensor->pix.pixelformat;

	pr_debug("%s: format: %x\n", __func__, fmt->pixelformat);
	return 0;
}

/*!
 * ioctl_g_fmt_cap - V4L2 sensor interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the sensor's current pixel format in the v4l2_format
 * parameter.
 */
static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	
	sensor->pix.pixelformat = V4L2_PIX_FMT_YUYV;
	sensor->pix.width = 1280;
	sensor->pix.height = 720;
	sensor->spix.swidth = sensor->pix.width;
	sensor->spix.sheight = sensor->pix.height;
    
	//printk(KERN_ERR "tom===%s===%d===f->type=%x\n" , __func__, __LINE__, f->type);
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		f->fmt.pix = sensor->pix;
		pr_debug("%s: %dx%d\n", __func__, sensor->pix.width, sensor->pix.height);
		break;

	case V4L2_BUF_TYPE_SENSOR:
		pr_debug("%s: left=%d, top=%d, %dx%d\n", __func__,
			sensor->spix.left, sensor->spix.top,
			sensor->spix.swidth, sensor->spix.sheight);
            f->fmt.spix = sensor->spix;
		break;

	case V4L2_BUF_TYPE_PRIVATE:
		pr_debug("%s: private\n", __func__);
		break;

	default:
		f->fmt.pix = sensor->pix;
		pr_debug("%s: type=%d, %dx%d\n", __func__, f->type, sensor->pix.width, sensor->pix.height);
		break;
	}
	return 0;
}

/*!
 * ioctl_dev_init - V4L2 sensor interface handler for vidioc_int_dev_init_num
 * @s: pointer to standard V4L2 device structure
 *
 * Initialise the device when slave attaches to the master.
 */
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	pr_debug("%s\n", __func__);
	return 0;
}


/*!
 * ioctl_s_power - V4L2 sensor interface handler for VIDIOC_S_POWER ioctl
 * @s: pointer to standard V4L2 device structure
 * @on: indicates power mode (on or off)
 *
 * Turns the power on or off, depending on the value of on and returns the
 * appropriate error code.
 */
static int ioctl_s_power(struct v4l2_int_device *s, int on)
{
	pr_debug("-> In function %s\n", __func__);

	return 0;
}

/*!
 * ioctl_g_ifparm - V4L2 sensor interface handler for vidioc_int_g_ifparm_num
 * s: pointer to standard V4L2 device structure
 * p: pointer to standard V4L2 vidioc_int_g_ifparm_num ioctl structure
 *
 * Gets slave interface parameters.
 * Calculates the required xclk value to support the requested
 * clock parameters in p.  This value is returned in the p
 * parameter.
 *
 * vidioc_int_g_ifparm returns platform-specific information about the
 * interface settings used by the sensor.
 *
 * Called on open.
 */
static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	struct tc_data *gs = s->priv;
	struct sensor_data *sensor = &gs->sensor;

	pr_debug("-> In function %s\n", __func__);

	/* Initialize structure to 0s then set any non-0 values. */
	memset(p, 0, sizeof(*p));
	p->u.bt656.clock_curr = sensor->mclk;
    p->if_type = V4L2_IF_TYPE_BT1120_PROGRESSIVE_SDR;	/* This is the only possibility. */
    p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_BT_8BIT;
    p->u.bt656.bt_sync_correct = 0;
    p->u.bt656.nobt_vs_inv = 0;
    p->u.bt656.nobt_hs_inv = 0;

	p->u.bt656.latch_clk_inv = 0;	/* pixel clk polarity */
	p->u.bt656.clock_min = 6000000;
	p->u.bt656.clock_max = 180000000;
	return 0;
}

/*!
 * This structure defines all the ioctls for this module and links them to the
 * enumeration.
 */
static struct v4l2_int_ioctl_desc bt1120_ioctl_desc[] = {
	{vidioc_int_dev_init_num, (v4l2_int_ioctl_func*) ioctl_dev_init},
    {vidioc_int_s_power_num, (v4l2_int_ioctl_func *) ioctl_s_power},
	{vidioc_int_g_ifparm_num, (v4l2_int_ioctl_func *) ioctl_g_ifparm},
    {vidioc_int_init_num, (v4l2_int_ioctl_func*) ioctl_init},
	
	/*!
	 * VIDIOC_ENUM_FMT ioctl for the CAPTURE buffer type.
	 */
	{vidioc_int_enum_fmt_cap_num,(v4l2_int_ioctl_func *) ioctl_enum_fmt_cap},
	
	{vidioc_int_g_fmt_cap_num, (v4l2_int_ioctl_func *) ioctl_g_fmt_cap},
	{vidioc_int_g_parm_num, (v4l2_int_ioctl_func *) ioctl_g_parm},
	{vidioc_int_s_parm_num, (v4l2_int_ioctl_func *) ioctl_s_parm},
	{vidioc_int_g_ctrl_num, (v4l2_int_ioctl_func *) ioctl_g_ctrl},
	{vidioc_int_s_ctrl_num, (v4l2_int_ioctl_func *) ioctl_s_ctrl},
	{vidioc_int_enum_framesizes_num,(v4l2_int_ioctl_func *) ioctl_enum_framesizes},
	{vidioc_int_g_chip_ident_num,(v4l2_int_ioctl_func *) ioctl_g_chip_ident},
    
};

static struct v4l2_int_slave bt1120_slave = {
	.ioctls = bt1120_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(bt1120_ioctl_desc),
};

static struct v4l2_int_device bt1120_int_device = {
	.module = THIS_MODULE,
	.name = DRIVER_NAME,
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &bt1120_slave,
	},
};

static int bt1120_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	int retval;
	struct tc_data *td;
	struct sensor_data *sensor;
    struct pinctrl *pinctrl;
    u32 val = 0;

    //printk(KERN_ERR "tom===%s===%d\n" , __func__, __LINE__);
	td = kzalloc(sizeof(*td), GFP_KERNEL);
	if (!td)
		return -ENOMEM;
	
    pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_err(dev, "setup pinctrl failed\n");
		return PTR_ERR(pinctrl);
	}
    
	sensor = &td->sensor;

	/* request select pin */
	td->select_gpio = of_get_named_gpio(dev->of_node, "select-gpios", 0);
	if (!gpio_is_valid(td->select_gpio)) {
		dev_warn(dev, "no sensor select pin available");
	}
    else
    {
        if (of_property_read_u32(dev->of_node, "select,output", &val))
        {
            val = 0;
        }
        //printk(KERN_ERR "tom===%s===%d====val=%d\n" , __func__, __LINE__, val);

        if(val == 0)
        {
            retval = devm_gpio_request_one(dev, td->select_gpio, GPIOF_OUT_INIT_LOW, "bt1120_select");
        }
        else
        {
            retval = devm_gpio_request_one(dev, td->select_gpio, GPIOF_OUT_INIT_HIGH, "bt1120_select");
        }
        
        if (retval < 0) {
            dev_warn(dev, "request of bt1120_select failed");
        }
    }

	retval = of_property_read_u32(dev->of_node, "mclk",&(sensor->mclk));
	if (retval) {
		dev_err(dev, "mclk missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "mclk_source",(u32 *) &(sensor->mclk_source));
	if (retval) {
		dev_err(dev, "mclk_source missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "ipu_id",&sensor->ipu_id);
	if (retval) {
		dev_err(dev, "ipu_id missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "csi_id",&(sensor->csi));
	if (retval) {
		dev_err(dev, "csi id missing or invalid\n");
		return retval;
	}

	if (!IS_ERR(sensor->sensor_clk))
		clk_prepare_enable(sensor->sensor_clk);

	sensor->pix.pixelformat = V4L2_PIX_FMT_YUYV;
	sensor->pix.width = 1280;
	sensor->pix.height = 720;
	sensor->streamcap.capability = V4L2_MODE_HIGHQUALITY | V4L2_CAP_TIMEPERFRAME;
	sensor->streamcap.capturemode = 0;
	sensor->streamcap.timeperframe.denominator = DEFAULT_FPS;
	sensor->streamcap.timeperframe.numerator = 1;
    sensor->pix.priv = 1;
    
    i2c_set_clientdata(client, td);
    
    bt1120_int_device.priv = td;
	if (!g_td)
		g_td = td;
        
	retval = v4l2_int_device_register(&bt1120_int_device);
	
	if (retval) {
		pr_err("%s:  v4l2_int_device_register failed, error=%d\n",
			__func__, retval);
		goto err4;
	}

	//printk(KERN_ERR "tom===%s===%d\n" , __func__, __LINE__);
	return retval;

err4:
	if (g_td == td)
		g_td = NULL;

	kfree(td);
	return retval;
}

/*!
 * bt1120 I2C detach function
 *
 * @param client	    struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int bt1120_remove(struct i2c_client *client)
{
	int i;
	struct tc_data *td = i2c_get_clientdata(client);

	
	v4l2_int_device_unregister(&bt1120_int_device);

	if (g_td == td)
		g_td = NULL;
	kfree(td);
	return 0;
}

static __init int bt1120_init(void)
{
	int err;
    pr_debug("In bt1120_init===%s===%d\n", __func__, __LINE__);
	err = i2c_add_driver(&bt1120_i2c_driver);
	if (err != 0)
		pr_err("%s:driver registration failed, error=%d\n",
			__func__, err);
	return err;
}

static void __exit bt1120_clean(void)
{
	i2c_del_driver(&bt1120_i2c_driver);
}

module_init(bt1120_init);
module_exit(bt1120_clean);

MODULE_AUTHOR("Panasonic Avionics Corp.");
MODULE_DESCRIPTION("Toshiba bt1120 HDMI-to-CSI2 Bridge MIPI Input Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
