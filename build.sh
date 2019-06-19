#! /bin/bash

set -e

TOP=`pwd`
OUT=${TOP}/out

#### Cross compiler tool path ####################################################
if [ -z $CROSS_COMPILE_PATH ];then
    [ -d /opt/freescale/usr/local ] && CROSS_COMPILE_PATH=/opt/freescale/usr/local
fi

if [ -z $CROSS_COMPILE_PATH ];then
    [ -d /home/artie/JOB-Area/Android ] && CROSS_COMPILE_PATH=/home/artie/JOB-Area/Android
fi

if [ -z $CROSS_COMPILE_PATH ];then
    [ -d /media/tom/ext2t/freescale/cross-compile ] && CROSS_COMPILE_PATH=/media/tom/ext2t/freescale/cross-compile
fi

if [ -z $CROSS_COMPILE_PATH ];then
	[ -d /opt/cross ] && CROSS_COMPILE_PATH=/opt/cross
fi

#### Define the CROSS COMPILE TOOL #########################################################
#CROSS_COMPILE_TOOL=rtx-gcc-4.9.3-glibc-2.19-hf-32bits/bin
#CROSS_COMPILE_TOOL=rtx-gcc-4.9.3-glibc-2.19-hf-64bits/arm-rtx-linux-gnueabihf/bin
#CROSS_COMPILE_TOOL=rtx-gcc-5.3.0-glibc-2.23-hf/arm-rtx-linux-gnueabihf/bin
CROSS_COMPILE_TOOL=rtx-gcc-6.3.0-glibc-2.25-hf-32bits/bin

CROSS_COMPILE_GCC=arm-rtx-linux-gnueabihf-

#Check CROSS_COMPILE_PATH
if [ -z "${CROSS_COMPILE_PATH}" ] ; then
    echo "Please set the cross compiler path."
    exit 1
fi

#Check CROSS_COMPILE_TOOL
if [ -z $CROSS_COMPILE_TOOL ];then
    echo "Please set the CROSS_COMPILE_TOOL."
    exit 1
fi

if [ ! -d ${CROSS_COMPILE_PATH}/${CROSS_COMPILE_TOOL} ];then
    echo "The ${CROSS_COMPILE_PATH}/${CROSS_COMPILE_TOOL} fold is not exist!"
    exit 1
fi

CROSS_COMPILE=${CROSS_COMPILE_PATH}/${CROSS_COMPILE_TOOL}/${CROSS_COMPILE_GCC}

########################################################################
export ARCH=arm
export CROSS_COMPILE=${CROSS_COMPILE}

########################################################################

#### Default Define ####################################################
IS_ANDROID_BUILD="no"
BUILD_GPU_VIV_DRIVER_MODULE="no"

if [ "${IS_ANDROID_BUILD}" == "yes" ] ; then
    BUILD_GPU_VIV_DRIVER_MODULE="no"
fi

#### Target Customer Project ###########################################
#TARGET_CUSTOMER="RTX-A6"
#TARGET_CUSTOMER="RTX-A6Plus"
#TARGET_CUSTOMER="RTX-Q7"
#TARGET_CUSTOMER="RTX-PITX-B10"
TARGET_CUSTOMER="RTX-PITX-B21"
#TARGET_CUSTOMER="ADLINK-ABB"
#TARGET_CUSTOMER="AcBel-VPP"
#TARGET_CUSTOMER="PITX-AOPEN"
#TARGET_CUSTOMER="ROM-7420"
#TARGET_CUSTOMER="PITX-CSE-JP"
#TARGET_CUSTOMER="PITX-OHGA-JP"

########################################################################
case "${TARGET_CUSTOMER}" in
	"RTX-A6")
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="a6"
		TARGET_SUBBOARD=""
		;;
	"RTX-A6Plus")
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="a6plus"
		TARGET_SUBBOARD=""
		;;
	"RTX-Q7")
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="q7"
		TARGET_SUBBOARD=""
		;;
	"RTX-PITX-B10")
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="pitx-b10"
		TARGET_SUBBOARD=""
		;;
	"RTX-PITX-B21")
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="pitx-b21"
		TARGET_SUBBOARD=""
		;;
	"ADLINK-ABB")
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6dl"
		TARGET_BOARD="adlink"
		TARGET_SUBBOARD="abb"
		;;
	"AcBel-VPP")
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="pitx-b10"
		TARGET_SUBBOARD="acbel-vpp"
		;;
	"PITX-AOPEN" )
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="pitx-b21"
		TARGET_SUBBOARD="aopen"
		;;
	"ROM-7420" )
		TARGET_VENDER="advantech"
		TARGET_SOC="imx6q"
		TARGET_BOARD="rom7420"
		TARGET_SUBBOARD=""
		;;
	"PITX-CSE-JP" )
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="pitx-b21"
		TARGET_SUBBOARD="cse-jp"
		;;
	"PITX-OHGA-JP" )
		TARGET_VENDER="rtx"
		TARGET_SOC="imx6q"
		TARGET_BOARD="pitx-b21"
		TARGET_SUBBOARD="ohga-jp"
		;;
    *)
		echo "Please set the target customer."
		exit 1
		;;
esac


if [ ${IS_ANDROID_BUILD} == "yes" ] ; then
	KERNEL_DEFAULT_CONFIG=imx_v7_android_defconfig
else
	KERNEL_DEFAULT_CONFIG=imx_v7_defconfig
fi

########################################################################
if [ ! "${TARGET_VENDER}" == "" ] ; then
	KERNEL_PROJECT_CONFIG=${TARGET_VENDER}
fi

if [ ! "${TARGET_SOC}" == "" ] ; then
	KERNEL_PROJECT_CONFIG=${KERNEL_PROJECT_CONFIG}-${TARGET_SOC}
fi

if [ ! "${TARGET_BOARD}" == "" ] ; then
	KERNEL_PROJECT_CONFIG=${KERNEL_PROJECT_CONFIG}-${TARGET_BOARD}
fi

if [ ! "${TARGET_SUBBOARD}" == "" ] ; then
	KERNEL_PROJECT_CONFIG=${KERNEL_PROJECT_CONFIG}-${TARGET_SUBBOARD}
fi

if [ "${IS_ANDROID_BUILD}" == "yes" ] ; then
	KERNEL_PROJECT_CONFIG=${KERNEL_PROJECT_CONFIG}-android
fi

KERNEL_DTB=${KERNEL_PROJECT_CONFIG}
KERNEL_PROJECT_CONFIG=rtx/configs/${KERNEL_PROJECT_CONFIG}_defconfig

cd ${TOP}/arch/arm/boot/dts/
KERNEL_DTB_DTS=${KERNEL_DTB}.dts
KERNEL_DTB_DTSI=${KERNEL_DTB}-iomux.dtsi
KERNEL_SOC_DTSI=${TARGET_VENDER}-${TARGET_SOC}-soc.dtsi
if [ ! -f "${KERNEL_DTB_DTS}" ] ; then
	ln -s ../../../../rtx/dts/${KERNEL_DTB_DTS} ${KERNEL_DTB_DTS}
fi

if [ ! -f "${KERNEL_DTB_DTSI}" ] ; then
	ln -s ../../../../rtx/dts/${KERNEL_DTB_DTSI} ${KERNEL_DTB_DTSI}
fi

if [ ! -f "${KERNEL_SOC_DTSI}" ] ; then
	ln -s ../../../../rtx/dts/${KERNEL_SOC_DTSI} ${KERNEL_SOC_DTSI}
fi
cd -

########################################################################
case "${TARGET_SOC}" in
	"imx6q" )
		# imx6s imx6dl imx6q
		KERNEL_LOADADDR=0x10008000
		;;
	"imx6dl" )
		# imx6s imx6dl imx6q
		KERNEL_LOADADDR=0x10008000
		;;
	*)
		echo "Please set the target soc."
		exit 1
		;;
esac

########################################################################
KERNEL_VERSION=`make kernelversion`
NOW_DATE=`date +%Y%m%d`

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
		make uImage -j8
	else
		make uImage -j8 LOADADDR=${KERNEL_LOADADDR}
	fi

	if [ -f arch/arm/boot/uImage ] ; then
		cp -f arch/arm/boot/uImage out/.
		if [ ! -z "${KERNEL_VERSION}" ] ; then
			cp -f arch/arm/boot/uImage out/uImage-${KERNEL_VERSION}-${NOW_DATE}
			cp -f arch/arm/boot/uImage out/uImage-${KERNEL_VERSION}
			cp -f arch/arm/boot/uImage out/uImage-recovery
		fi
	fi

	if [ ! -z "${KERNEL_DTB}" ] ; then
		make ${KERNEL_DTB}.dtb
		cp arch/arm/boot/dts/${KERNEL_DTB}.dtb out/.
		if [ ! -z "${KERNEL_VERSION}" ] ; then
			cp arch/arm/boot/dts/${KERNEL_DTB}.dtb out/${KERNEL_DTB}-${KERNEL_VERSION}-${NOW_DATE}.dtb
			cp arch/arm/boot/dts/${KERNEL_DTB}.dtb out/${KERNEL_DTB}-${KERNEL_VERSION}.dtb
			cp arch/arm/boot/dts/${KERNEL_DTB}.dtb out/recovery.dtb
		fi
	fi
}

function build_dtb()
{
	if [ ! -z "${KERNEL_DTB}" ] ; then
		make ${KERNEL_DTB}.dtb
		cp arch/arm/boot/dts/${KERNEL_DTB}.dtb out/.
		if [ ! -z "${KERNEL_VERSION}" ] ; then
			cp arch/arm/boot/dts/${KERNEL_DTB}.dtb out/${KERNEL_DTB}-${KERNEL_VERSION}-${NOW_DATE}.dtb
			cp arch/arm/boot/dts/${KERNEL_DTB}.dtb out/${KERNEL_DTB}-${KERNEL_VERSION}.dtb
			cp arch/arm/boot/dts/${KERNEL_DTB}.dtb out/recovery.dtb
		fi
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
			cp -rfv .tmp_build/firmware-imx-5.4/firmware/vpu out/lib/firmware/.
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

help() {
bn=`basename $0`
cat << EOF
usage $bn build_type
  build_type   [all/config/menuconfig/saveconfig/uImage/modules/install/clean/distclean/rootfs]
example:
    $bn all
    $bn menuconfig

PS.
    Extract CROSS COMPILE TOOL and modify CROSS_COMPILE_PATH, CROSS_COMPILE_TOOL and CROSS_COMPILE_GCC for your gcc tools.

EOF

}

# Main function

# Build the necessary directions
build_dir

#
case "${1}" in
    "info")
		echo "CROSS_COMPILE_PATH          = ${CROSS_COMPILE_PATH}"
		echo "CROSS_COMPILE_TOOL          = ${CROSS_COMPILE_TOOL}"
		echo "CROSS_COMPILE_GCC           = ${CROSS_COMPILE_GCC}"
		echo "CROSS_COMPILE               = ${CROSS_COMPILE}"
		echo "TARGET_CUSTOMER             = ${TARGET_CUSTOMER}"
		echo "TARGET_VENDER               = ${TARGET_VENDER}"
		echo "TARGET_SOC                  = ${TARGET_SOC}"
		echo "TARGET_BOARD                = ${TARGET_BOARD}"
		echo "TARGET_SUBBOARD             = ${TARGET_SUBBOARD}"
		echo "KERNEL_PROJECT_CONFIG       = ${KERNEL_PROJECT_CONFIG}"
		echo "KERNEL_DTB                  = ${KERNEL_DTB}"
		echo "IS_ANDROID_BUILD            = ${IS_ANDROID_BUILD}"
		echo "BUILD_GPU_VIV_DRIVER_MODULE = ${BUILD_GPU_VIV_DRIVER_MODULE}"
		echo "KERNEL_VERSION              = ${KERNEL_VERSION}"
		echo "KERNEL_DEFAULT_CONFIG       = ${KERNEL_DEFAULT_CONFIG}"
		echo "KERNEL_LOADADDR             = ${KERNEL_LOADADDR}"
		;;
	"all")
		rm -rf out
		mkdir -p out

		configure ${2}

		build_kernel

		make modules -j8
		make DEPMOD=/bin/true INSTALL_MOD_PATH=out modules_install
		make INSTALL_FW_PATH=out/lib/firmware firmware_install
		make INSTALL_HDR_PATH=out headers_install

		build_gpu_viv_module
		build_imx_firmware

		cd out
		tar czvf lib.tar.gz lib
		cp lib.tar.gz lib-${KERNEL_VERSION}.tar.gz
		cd lib/modules
		MODULE_PATH_NAME=`ls`
		cd ${MODULE_PATH_NAME}
		rm -f source
		rm -f build
		cd ..
		tar czvf ../../modules.tar.gz *
		cp ../../modules.tar.gz ../../modules-${KERNEL_VERSION}.tar.gz
		cd ../..
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

		if [ ! -f arch/arm/boot/uImage ] ; then
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

		if [ ! -f arch/arm/boot/uImage ] ; then
			build_kernel
		fi
		if [ ! -f modules.builtin ] ; then
			make modules -j8
		fi
		make DEPMOD=/bin/true INSTALL_MOD_PATH=out modules_install
		make INSTALL_HDR_PATH=out headers_install
		cp -f arch/arm/boot/uImage out/.
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
 		help
		exit 1
		;;
esac
