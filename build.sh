#! /bin/bash

set -e

#### Target OS #########################################################
TARGET_OS="Ubuntu-18.04hf"

#### Target Board ######################################################
TARGET_BOARD="IMX8MM_EVK"

#### Default Define ####################################################
KERNEL_DEFAULT_CONFIG=defconfig
BUILD_GPU_VIV_DRIVER_MODULE="no"

TOP=`pwd`
OUT=${TOP}/out

########################################################################


case "${TARGET_OS}" in
	"Ubuntu-18.04hf")
		# Ubuntu 18.04
		echo "Build Kernel for Ubuntu 18.04 hf"
		export ARCH=arm64
		export CROSS_COMPILE=/opt/cross/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
		;;
	*)
		echo "Please set the target OS."
		exit 1
		;;
esac

case "${TARGET_BOARD}" in
	"IMX8MM_EVK" )
		TARGET_SOC=imx8mm
#		KERNEL_PROJECT_CONFIG=arch/arm64/configs/defconfig
		KERNEL_LOADADDR=0x10008000
		KERNEL_DTB=fsl-imx8mm-evk
		;;
	*)
		echo "Please set the target board."
		exit 1
		;;
esac

KERNEL_VERSION=`make kernelversion`

function build_dir()
{
	for dir_item in out rtxconfig
	do
		if [ ! -d ${dir_item} ] ; then
			mkdir -p ${dir_item}
		fi
	done
}

function configure()
{
	unset LDFLAGS
	if [ -z "${KERNEL_DEFAULT_CONFIG}" ] ; then
		echo "Please set the default configure file."
		exit 1
	fi
	
	if [ ! -z "${1}" ] ; then
		echo "Use the currently configure file (${KERNEL_DEFAULT_CONFIG})."
		make distclean
		make ${1}
	else
		if [ ! -f .config ] ; then
			if [ -z "${KERNEL_PROJECT_CONFIG}" ] ; then
				echo "The project configure file (${KERNEL_PROJECT_CONFIG}) do not set."
				echo "Use the default configure file (${KERNEL_DEFAULT_CONFIG})."
				make ${KERNEL_DEFAULT_CONFIG}
			else
				if [ -f ${KERNEL_PROJECT_CONFIG} ] ; then
					echo "Use the project configure file (${KERNEL_PROJECT_CONFIG})."
					cp ${KERNEL_PROJECT_CONFIG} .config
				else
					echo "The project configure file (${KERNEL_PROJECT_CONFIG}) is not exist."
					echo "Use the default configure file (${KERNEL_DEFAULT_CONFIG})."
					make ${KERNEL_DEFAULT_CONFIG}
				fi
			fi
		fi
	fi
}

function build_kernel()
{
	if [ -z "${KERNEL_LOADADDR}" ] ; then
		make -j8
	else
		make -j8 
	fi
	
	if [ -f arch/arm64/boot/Image ] ; then
		cp -f arch/arm64/boot/Image out/.
		cp arch/arm64/boot/dts/freescale/$KERNEL_DTB.dtb out/.
	fi
}

function build_dtb()
{
	if [ ! -z "${KERNEL_DTB}" ] ; then
		cp arch/arm64/boot/dts/freescale/$KERNEL_DTB.dtb out/.
	fi
}

function build_imx_firmware()
{
	case "${TARGET_SOC}" in
		"imx6q")
			if [ ! -d rtx/imx6-libs ] ; then
				break ;
			fi
			if [ ! -f rtx/imx6-libs/firmware-imx-5.4.bin ] ; then
				break ;
			fi
			if [ -d .tmp_build ] ; then
				rm -rf .tmp_build
			fi
			mkdir -p .tmp_build
			
			cp rtx/imx6-libs/firmware-imx-5.4.bin .tmp_build/.
			cd .tmp_build
			chmod +x firmware-imx-5.4.bin
			./firmware-imx-5.4.bin --auto-accept --force
			cd ${TOP}
			mkdir -p out/lib/firmware/imx
			cp -rfv .tmp_build/firmware-imx-5.4/firmware/* out/lib/firmware/
			mv out/lib/firmware/epdc/ out/lib/firmware/imx/epdc/
			mv out/lib/firmware/imx/epdc/epdc_ED060XH2C1.fw.nonrestricted out/lib/firmware/imx/epdc/epdc_ED060XH2C1.fw

			#find out/lib/firmware -type f -exec chmod 644 '{}' ';'
			#find out/lib/firmware -type f -exec chown root:root '{}' ';'

			# Remove files not going to be installed
			find out/lib/firmware/ -name '*.mk' -exec rm '{}' ';'
			rm -rf .tmp_build
		;;
	esac
}

function build_gpu_viv_module()
{
	if [ ${BUILD_GPU_VIV_DRIVER_MODULE} == "yes" ] ; then
		case "${TARGET_SOC}" in
			"imx6q")
				cd ${TOP}
				
				if [ ! -d rtx/imx6-libs ] ; then
					break ;
				fi
				if [ ! -f rtx/imx6-libs/kernel-module-imx-gpu-viv-6.2.2.p0.tar.gz ] ; then
					break ;
				fi
				if [ -d .tmp_build ] ; then
					rm -rf .tmp_build
				fi
				mkdir -p .tmp_build
				cd .tmp_build
				
				if [ ! -f .extract ] ; then
					tar xzvf ${TOP}/rtx/imx6-libs/kernel-module-imx-gpu-viv-6.2.2.p0.tar.gz
					touch .extract
				fi
				cd kernel-module-imx-gpu-viv-6.2.2.p0
				KERNEL_SRC=${TOP} make
				KERNEL_SRC=${TOP} make DEPMOD=/bin/true INSTALL_MOD_PATH=${TOP}/out modules_install
				cd ${TOP}
				rm -rf .tmp_build
			;;
		esac
	fi
}

# Main function

# Build the necessary directions
build_dir

# 
case "${1}" in
	"all")
		rm -rf out
		mkdir -p out
		
		configure ${2}
		
		build_kernel
		
		make modules -j8
		make DEPMOD=/bin/true INSTALL_MOD_PATH=out modules_install
		make INSTALL_HDR_PATH=out headers_install	
		cd out
		tar cjvf lib.tar.bz2 lib
		cd ..
		;;
	"config")
		configure ${2}
		;;
	"menuconfig")
		configure ${2}
		make menuconfig
		;;
	"diff")
		diff .config ${KERNEL_PROJECT_CONFIG}
		;;
	"save")
		if [ -f .config ] ; then
			cp .config ${KERNEL_PROJECT_CONFIG}
		fi
		;;
	"save-as")
		if [ -f .config ] ; then
			if [ -z "${2}" ] ; then
				echo "cmd save-as [filename]"
			else
				cp .config rtxconfig/${2}
			fi
		fi
		;;
	"load")
		if [ -z "${2}" ] ; then
			echo "cmd load [filename]"
		else
			cp rtxconfig/${2} .config
		fi
		;;
	"uImage")
		configure ${2}
		build_kernel
		;;
	"dtb")
		configure ${2}
		build_dtb
		;;
	"modules")
		configure ${2}
		
		if [ ! -f arch/arm64/boot/uImage ] ; then
			build_kernel
		fi
		make modules -j8
		build_gpu_viv_module
		;;
	"install")
		if [ ! -d out ] ; then
			mkdir -p out
		fi
		
		configure ${2}
		
		if [ ! -f arch/arm64/boot/uImage ] ; then
			build_kernel
		fi
		if [ ! -f modules.builtin ] ; then
			make modules -j8
		fi
		make DEPMOD=/bin/true INSTALL_MOD_PATH=out modules_install
		make INSTALL_HDR_PATH=out headers_install
		cp -f arch/arm64/boot/uImage out/.
		;;
	"clean")
		make clean
		;;
	"distclean")
		make distclean
		rm -rf out
		;;
	"rootfs")
		if [ -z "${2}" ] ; then
			echo "${0} ${1} [/dev/sd? or /dev/mmcblk?]"
		else
			sudo dd if=out/uImage of=${2} bs=512 seek=26624
			#echo "dd if=out/uImage of=${2} bs=512 seek=26624"
			if [ -d /media/rootfs/lib/modules ] ; then
				sudo rm -rf /media/rootfs/lib/modules/*
				sudo cp -avrf out/lib/modules/* /media/rootfs/lib/modules/.
				sudo chown -R root:root /media/rootfs/lib/modules/*
			fi
			if [ -d /media/rootfs/usr/src/linux/include ] ; then
				sudo rm -rf /media/rootfs/usr/src/linux/include/*
				sudo cp -avrf out/include/* /media/rootfs/usr/src/linux/include/.
				sudo chown -R root:root /media/rootfs/usr/src/linux/include/*
			fi
		fi
		;;
	*) 
		echo "${0} [all/config/menuconfig/saveconfig/uImage/modules/install/clean/disclean/rootfs]"
		exit 1
		;;
esac
