TOP=`pwd`
export ARCH=arm

# =====================================
# Cross compile tools path define 
# default path is /opt/cross if not define CROSS_COMPILE_PATH
# =====================================
CROSS_COMPILE_PATH=/media/tom/ext2t/freescale/cross-compile/arm-linux-androideabi-4.9/bin
if [ ! -d $CROSS_COMPILE_PATH ];then
echo "The CROSS_COMPILE_PATH is not exist!!!"
fi

# =====================================
# Cross compile tools version
# =====================================
export CROSS_COMPILE=${CROSS_COMPILE_PATH}/arm-linux-androideabi-


# =====================================
# Configue file select
# =====================================
TARGET_VENDER="rtx"
TARGET_SOC="imx6q"
TARGET_BOARD="treadmill"

KERNEL_DEFAULT_CONFIG=imx_v7_android_defconfig
KERNEL_LOADADDR=0x10008000
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

KERNEL_PROJECT_CONFIG=${KERNEL_PROJECT_CONFIG}-android

KERNEL_DTB=${KERNEL_PROJECT_CONFIG}
KERNEL_PROJECT_CONFIG=rtx/configs/${KERNEL_PROJECT_CONFIG}_defconfig

cd ${TOP}/arch/arm/boot/dts/
KERNEL_DTB_DTS=${KERNEL_DTB}.dts
KERNEL_DTB_DTSI=${KERNEL_DTB}-iomux.dtsi
KERNEL_SOC_DTSI=${TARGET_VENDER}-${TARGET_SOC}-soc.dtsi
KERNEL_ALLSOC_DTSI=rtx-imx6qdl-soc.dtsi

if [ ! -f "${KERNEL_DTB_DTS}" ] ; then
	ln -s ../../../../rtx/dts/${KERNEL_DTB_DTS} ${KERNEL_DTB_DTS}
fi

if [ ! -f "${KERNEL_DTB_DTSI}" ] ; then
	ln -s ../../../../rtx/dts/${KERNEL_DTB_DTSI} ${KERNEL_DTB_DTSI}
fi

if [ ! -f "${KERNEL_SOC_DTSI}" ] ; then
	ln -s ../../../../rtx/dts/${KERNEL_SOC_DTSI} ${KERNEL_SOC_DTSI}
fi

if [ ! -f "${KERNEL_ALLSOC_DTSI}" ] ; then
	ln -s ../../../../rtx/dts/${KERNEL_ALLSOC_DTSI} ${KERNEL_ALLSOC_DTSI}
fi

cd ${TOP}


# Get Host Number of CPUs
CPUS=`cat /proc/cpuinfo | grep processor | wc -l`

if [ ! -d out ] ; then
	mkdir -p out
fi

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
		make uImage -j${CPUS}
	else
		make uImage -j${CPUS} LOADADDR=${KERNEL_LOADADDR}
	fi

	if [ -f arch/arm/boot/uImage ] ; then
		cp -f arch/arm/boot/uImage out/.
		if [ ! -z "${KERNEL_VERSION}" ] ; then
			cp -f arch/arm/boot/uImage out/uImage-${KERNEL_VERSION}-${NOW_DATE}
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
		fi
	fi
}

case "${1}" in
	"all")
		rm -rf out
		mkdir -p out

		configure
		build_kernel
		build_dtb
		;;
	"uImage")
		configure
		build_kernel
		;;
	"dtb")
		configure
		build_dtb
		;;
	"config")
		configure
		;;
	"menuconfig")
		configure
		make menuconfig
		;;
	"clean")
		make clean
		;;
	"distclean")
		make distclean
		rm -rf out
		;;
	*) 
		echo "${0} [all/uImage/dtb/config/menuconfig/clean/distclean]"
		exit 1
		;;
esac
