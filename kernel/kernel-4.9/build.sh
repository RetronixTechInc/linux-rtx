#! /bin/bash

set -e
#### Cross compiler ####################################################
CROSS_COMPILE_PATH=/opt/cross

#### Target OS #########################################################
TARGET_OS="nVidia_sample_OS"

#### Target Board ######################################################
TARGET_BOARD="AVA_XV"

#### Default Define ####################################################
KERNEL_DEFAULT_CONFIG=tegra_defconfig

TOP=`pwd`
OUT=${TOP}/out

case "${TARGET_OS}" in
	"nVidia_sample_OS")
		# Ubuntu 18.04
		echo "Build Kernel for nVidia_sample_OS"
		export ARCH=arm64
		export LOCALVERSION=-tegra
		##export CROSS_COMPILE=${CROSS_COMPILE_PATH}/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
		export CROSS_COMPILE=${CROSS_COMPILE_PATH}/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
		;;
	*)
		echo "Please set the target OS."
		exit 1
		;;
esac

case "${TARGET_BOARD}" in
	"AVA_XV" )
		TARGET_SOC=nvidia_xavier_agx
		KERNEL_PROJECT_CONFIG=rtx/configs/rtx-ava-xv_defconfig
		KERNEL_DTB=tegra194-p2888-0001-ava-xv1
		;;
	*)
		echo "Please set the target board."
		exit 1
		;;
esac

function build_dir()
{
	for dir_item in out
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
		make O=$OUT distclean
		make O=$OUT $KERNEL_DEFAULT_CONFIG
	else
		if [ ! -f $OUT/.config ] ; then
			if [ -z "${KERNEL_PROJECT_CONFIG}" ] ; then
				echo "The project configure file (${KERNEL_PROJECT_CONFIG}) do not set."
				echo "Use the default configure file (${KERNEL_DEFAULT_CONFIG})."
				make O=$OUT ${KERNEL_DEFAULT_CONFIG}
			else
				if [ -f ${KERNEL_PROJECT_CONFIG} ] ; then
					echo "Use the project configure file (${KERNEL_PROJECT_CONFIG})."
					cp ${KERNEL_PROJECT_CONFIG} $OUT/.config
				else
					echo "The project configure file (${KERNEL_PROJECT_CONFIG}) is not exist."
					echo "Use the default configure file (${KERNEL_DEFAULT_CONFIG})."
					make O=$OUT ${KERNEL_DEFAULT_CONFIG}
				fi
			fi
		else
			echo "Use the currently configure file (.config)"
		fi
	fi
}

function build_kernel()
{
	make O=$OUT -j8
	
	if [ -f $OUT/arch/arm64/boot/Image ] ; then
		cp -f $OUT/arch/arm64/boot/Image out/.
		cp $OUT/arch/arm64/boot/dts/$KERNEL_DTB.dtb out/.
	fi
}

function build_dtb()
{
	make O=$OUT dtbs

	if [ ! -z "$OUT/${KERNEL_DTB}" ] ; then
		cp $OUT/arch/arm64/boot/dts/$KERNEL_DTB.dtb out/.
	fi
}

# Main function

# Build the necessary directions
build_dir

# 
case "${1}" in
	"info")
		echo "ARCH		          = ${ARCH}"
		echo "CROSS_COMPILE_PATH          = ${CROSS_COMPILE_PATH}"
		echo "CROSS_COMPILE               = ${CROSS_COMPILE}"
		echo "TARGET_OS                   = ${TARGET_OS}"
		echo "TARGET_BOARD                = ${TARGET_BOARD}"
		echo "TARGET_SOC                  = ${TARGET_SOC}"
		echo "KERNEL_PROJECT_CONFIG       = ${KERNEL_PROJECT_CONFIG}"
		echo "KERNEL_VERSION              = ${KERNEL_VERSION}"
		echo "KERNEL_DEFAULT_CONFIG       = ${KERNEL_DEFAULT_CONFIG}"
		echo "KERNEL_DTB                  = ${KERNEL_DTB}"
		;;
	"all")
		configure ${2}
		
		build_kernel
		
		make O=$OUT modules -j8
		make O=$OUT modules_install INSTALL_MOD_PATH=module
		make O=$OUT headers_install INSTALL_HDR_PATH=module 	
		cd out/module
		tar --owner root --group root -cjf kernel_supplements.tbz2 lib/modules
		cd ../..
		;;
	"config")
		configure ${2}
		;;
	"menuconfig")
		configure ${2}
		make O=$OUT menuconfig
		;;
	"diff")
		diff $OUT/.config ${KERNEL_PROJECT_CONFIG}
		;;
	"save")
		if [ -f $OUT/.config ] ; then
			cp $OUT/.config ${KERNEL_PROJECT_CONFIG}
		fi
		;;
	"save-as")
		if [ -f $OUT/.config ] ; then
			if [ -z "${2}" ] ; then
				echo "cmd save-as [filename]"
			else
				cp $OUT/.config rtxconfig/${2}
			fi
		fi
		;;
	"load")
		if [ -z "${2}" ] ; then
			echo "cmd load [filename]"
		else
			cp rtxconfig/${2} $OUT/.config
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
	"clean")
		make O=$OUT clean
		;;
	"distclean")
		make O=$OUT distclean
		rm -rf out
		;;
	*) 
		echo "${0} [all/config/menuconfig/saveconfig/uImage/install/clean/distclean]"
		exit 1
		;;
esac
