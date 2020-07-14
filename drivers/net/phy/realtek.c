/*
 * drivers/net/phy/realtek.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Johnson Leung <r58129@freescale.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/phy.h>
#include <linux/module.h>

#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13

#define	RTL8211E_INER_LINK_STATUS	0x400

#define RTL8211E_EPAGSR		0x1e
#define RTL8211E_EPAGSR_EXT44	0x2c
#define RTL8211E_PAGSEL		0x1f
#define RTL8211E_PAGSEL_P0	0x0
#define RTL8211E_PAGSEL_P5	0x5
#define RTL8211E_PAGSEL_EXTPAGE	0x7
#define RTL8211E_LACR		0x1a /* LED Action Control Register */
#define RTL8211E_LACR_LED0_BLINKING	BIT(4)
#define RTL8211E_LACR_LED1_BLINKING	BIT(5)
#define RTL8211E_LACR_LED2_BLINKING	BIT(6)
#define RTL8211E_LCR		0x1c /* LED Control Register */
#define RTL8211E_LCR_LED0_ACTIVE_SPEED_ALL	(BIT(0) | BIT(1) | BIT(2))
#define RTL8211E_LCR_LED0_ACTIVE_SPEED_10M	BIT(0)
#define RTL8211E_LCR_LED0_ACTIVE_SPEED_100M	BIT(1)
#define RTL8211E_LCR_LED0_ACTIVE_SPEED_1000M	BIT(2)
#define RTL8211E_LCR_LED1_ACTIVE_SPEED_ALL	(BIT(4) | BIT(5) | BIT(6))
#define RTL8211E_LCR_LED1_ACTIVE_SPEED_10M	BIT(4)
#define RTL8211E_LCR_LED1_ACTIVE_SPEED_100M	BIT(5)
#define RTL8211E_LCR_LED1_ACTIVE_SPEED_1000M	BIT(6)
#define RTL8211E_LCR_LED2_ACTIVE_SPEED_ALL	(BIT(8) | BIT(9) | BIT(10))
#define RTL8211E_LCR_LED2_ACTIVE_SPEED_10M	BIT(8)
#define RTL8211E_LCR_LED2_ACTIVE_SPEED_100M	BIT(9)
#define RTL8211E_LCR_LED2_ACTIVE_SPEED_1000M	BIT(10)
#define RTL8211E_P05_R05	0x5  /* EEE LED control Reg.5 in Page 5 */
#define RTL8211E_P05_R05_EEE_LED_DISABLED	0x8b82
#define RTL8211E_P05_R06	0x6  /* EEE LED control Reg.6 in Page 5 */
#define RTL8211E_P05_R06_EEE_LED_DISABLED	0x052b

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");

static void rtl8211e_setup_led(struct phy_device *phydev)
{
	//printk(KERN_INFO "===%s===%d===\n", __func__, __LINE__);
	
	/* By default the EEE LED mode is enabled, that is
	 * blinking as 400ms on and 2s off. And we want to
	 * disable EEE LED mode.
	 */
	phy_write(phydev, RTL8211E_PAGSEL, RTL8211E_PAGSEL_P5);
	phy_write(phydev, RTL8211E_P05_R05, RTL8211E_P05_R05_EEE_LED_DISABLED);
	phy_write(phydev, RTL8211E_P05_R06, RTL8211E_P05_R06_EEE_LED_DISABLED);

	phy_write(phydev, RTL8211E_PAGSEL, RTL8211E_PAGSEL_EXTPAGE);
	phy_write(phydev, RTL8211E_EPAGSR, RTL8211E_EPAGSR_EXT44);
	phy_write(phydev, RTL8211E_LCR,
		  RTL8211E_LCR_LED1_ACTIVE_SPEED_100M |
		  RTL8211E_LCR_LED0_ACTIVE_SPEED_1000M |
		  RTL8211E_LCR_LED2_ACTIVE_SPEED_ALL);
	/* The LED2 blinking, the other two are in steady mode */
	phy_write(phydev, RTL8211E_LACR, RTL8211E_LACR_LED2_BLINKING);
	/* restore to default page 0 */
	phy_write(phydev, RTL8211E_PAGSEL, RTL8211E_PAGSEL_P0);
}

static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;
	//printk(KERN_INFO "===%s===%d===\n", __func__, __LINE__);
	
	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static int rtl8211b_config_intr(struct phy_device *phydev)
{
	int err;
	//printk(KERN_INFO "===%s===%d===\n", __func__, __LINE__);
	
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL821x_INER_INIT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211e_config_intr(struct phy_device *phydev)
{
	int err;

	/* HACK: re config the LED after s2r even the phy power is not down
	 * during the suspend. https://crosbug.com/p/57766
	 */
	//printk(KERN_INFO "===%s===%d===\n", __func__, __LINE__);
	
	rtl8211e_setup_led(phydev);
	
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211E_INER_LINK_STATUS);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

//extern int genphy_config_init(struct phy_device *phydev);
static int rtl8211e_config_init(struct phy_device *phydev)
{
	int val;
	u32 features;

	//printk(KERN_INFO "===%s===%d===\n", __func__, __LINE__);
	
	/* For now, I'll claim that the generic driver supports
	 * all possible port types */
	features = (SUPPORTED_TP | SUPPORTED_MII
			| SUPPORTED_AUI | SUPPORTED_FIBRE |
			SUPPORTED_BNC);

	/* Do we support autonegotiation? */
	val = phy_read(phydev, MII_BMSR);

	if (val < 0)
		return val;

	if (val & BMSR_ANEGCAPABLE)
		features |= SUPPORTED_Autoneg;

	if (val & BMSR_100FULL)
		features |= SUPPORTED_100baseT_Full;
	if (val & BMSR_100HALF)
		features |= SUPPORTED_100baseT_Half;
	if (val & BMSR_10FULL)
		features |= SUPPORTED_10baseT_Full;
	if (val & BMSR_10HALF)
		features |= SUPPORTED_10baseT_Half;

	if (val & BMSR_ESTATEN) {
		val = phy_read(phydev, MII_ESTATUS);

		if (val < 0)
			return val;

		if (val & ESTATUS_1000_TFULL)
			features |= SUPPORTED_1000baseT_Full;
		if (val & ESTATUS_1000_THALF)
			features |= SUPPORTED_1000baseT_Half;
	}

	phydev->supported = features;
	phydev->advertising = features;


	rtl8211e_setup_led(phydev);

	return 0;
}


static struct phy_driver rtl8211e_driver;
static int rtl8211e_match_phy_device(struct phy_device *phydev)
{
	/* Ignore the phyaddr 1 for 8211e.
	 * See issue: https://crosbug.com/p/57766
	 */
	//printk(KERN_INFO "===%s===%d===\n", __func__, __LINE__);
	
	return phydev->addr == 0 && phydev->phy_id == rtl8211e_driver.phy_id;
}

/* RTL8201CP */
static struct phy_driver rtl8201cp_driver = {
	.phy_id         = 0x00008201,
	.name           = "RTL8201CP Ethernet",
	.phy_id_mask    = 0x0000ffff,
	.features       = PHY_BASIC_FEATURES,
	.flags          = PHY_HAS_INTERRUPT,
	.config_aneg    = &genphy_config_aneg,
	.read_status    = &genphy_read_status,
	.driver         = { .owner = THIS_MODULE,},
};

/* RTL8211B */
static struct phy_driver rtl8211b_driver = {
	.phy_id		= 0x001cc912,
	.name		= "RTL8211B Gigabit Ethernet",
	.phy_id_mask	= 0x001fffff,
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.ack_interrupt	= &rtl821x_ack_interrupt,
	.config_intr	= &rtl8211b_config_intr,
	.driver		= { .owner = THIS_MODULE,},
};

/* RTL8211E */
static struct phy_driver rtl8211e_driver = {
	.phy_id		= 0x001cc915,
	.name		= "RTL8211E Gigabit Ethernet",
	.phy_id_mask	= 0x001fffff,
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.ack_interrupt	= &rtl821x_ack_interrupt,
	.config_intr	= &rtl8211e_config_intr,
	.config_init	= &rtl8211e_config_init,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
	.match_phy_device = &rtl8211e_match_phy_device,
};

static int __init realtek_init(void)
{
	int ret;

		ret = phy_driver_register(&rtl8201cp_driver);
	if (ret < 0)
		return -ENODEV;
		ret = phy_driver_register(&rtl8211b_driver);
	if (ret < 0)
		return -ENODEV;
		return phy_driver_register(&rtl8211e_driver);
}

static void __exit realtek_exit(void)
{
	phy_driver_unregister(&rtl8211b_driver);
	phy_driver_unregister(&rtl8211e_driver);
}

module_init(realtek_init);
module_exit(realtek_exit);

static struct mdio_device_id __maybe_unused realtek_tbl[] = {
	{ 0x001cc912, 0x001fffff },
	{ 0x001cc915, 0x001fffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, realtek_tbl);
