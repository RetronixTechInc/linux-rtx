/*
 * Copyright (C) 2010 RTX CO. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file efm32_rtc.h
 *
 * @brief EFM32 IOCTL definitions
 *
 * @ingroup RTC
 */


#define EFM_SEND			_IO('p', 0x11)  /* EFM32 send data			*/
#define EFM32_VER_GET		_IO('p', 0x12)  /* EFM32 get boot/main and version*/
#define EFM_UPDATE_START	_IO('p', 0x13)  /* EFM32 update start		*/
#define EFM_UPDATE_FINISHED	_IO('p', 0x14)  /* EFM32 update finished	*/
#define EFM_SEND_ALARM		_IO('p', 0x15)  /* EFM32 send Alarm time	*/
#define EFM_GET_ALARM		_IO('p', 0x16)  /* EFM32 get Alarm time		*/
#define EFM_UPDATE_SAVETIME	_IO('p', 0x17)  /* EFM32 get save time		*/
#define EFM_UPDATE_STATUS	_IO('p', 0x18)  /* EFM32 set power status	*/

