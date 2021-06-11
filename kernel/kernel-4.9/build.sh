#!/bin/bash

TOP=`pwd`

export ARCH=arm64
export LOCALVERSION=-tegra

[ -d /media/tom/ext2t/freescale/cross-compile ] && CROSS_COMPILE_PATH=/media/tom/ext2t/freescale/cross-compile/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu/bin/
CROSS_COMPILE_TOOL=aarch64-linux-gnu-

#Check CROSS_COMPILE_PATH
if [ -z "${CROSS_COMPILE_PATH}" ] ; then
	echo "Please download compiler tools adn set the cross compiler path."
	exit 1
fi
export CROSS_COMPILE=${CROSS_COMPILE_PATH}/${CROSS_COMPILE_TOOL}

TEGRA_KERNEL_OUT=${TOP}/out
TEGRA_KERNEL_BUILD=${TEGRA_KERNEL_OUT}/build
TEGRA_KERNEL_INSTALL=${TEGRA_KERNEL_OUT}/lib
TEGRA_KERNEL_MODULENAME=kernel_supplements.tbz2
if [ ! -d ${TEGRA_KERNEL_BUILD} ]; then
	mkdir -p ${TEGRA_KERNEL_BUILD}
	echo "create ${TEGRA_KERNEL_BUILD} fold."
fi

if [ ! -f ${TEGRA_KERNEL_BUILD}/.config ]; then
	make O=${TEGRA_KERNEL_BUILD} tegra_defconfig
	echo "create tegra_defconfig config."
fi

####################################
# help function
####################################
help() {
bn=`basename $0`
cat << EOF
usage $bn 
	make Image,dtb and modules
usage $bn menuconfig|install|replace

option:
  -h    		displays this help message
  menuconfig 	modify .config
  install		install modules
  replace		replace Image,dtb and moudles

EOF

}

case "${1}" in
	"menuconfig")
		make O=${TEGRA_KERNEL_BUILD} menuconfig
	;;
	"install")
		if [ -d ${TEGRA_KERNEL_INSTALL} ]; then
			rm -rf ${TEGRA_KERNEL_INSTALL}
		fi
		if [ -f ${TEGRA_KERNEL_OUT}/${TEGRA_KERNEL_MODULENAME} ]; then
			rm ${TEGRA_KERNEL_OUT}/${TEGRA_KERNEL_MODULENAME}
		fi
		
		make O=${TEGRA_KERNEL_BUILD} modules_install INSTALL_MOD_PATH=${TEGRA_KERNEL_OUT}
		find ${TEGRA_KERNEL_INSTALL} -name *.ko -exec aarch64-linux-gnu-strip --strip-unneeded {} \;
		#~ find ${TEGRA_KERNEL_INSTALL} -name *.ko -exec aarch64-linux-gnu-strip -d {} \;
		cd ${TEGRA_KERNEL_OUT}
		tar --owner root --group root -cjf ${TEGRA_KERNEL_MODULENAME} lib/modules
		cd -
		
		cp ${TEGRA_KERNEL_BUILD}/arch/arm64/boot/Image ${TEGRA_KERNEL_INSTALL}/
		cp ${TEGRA_KERNEL_BUILD}/arch/arm64/boot/dts/tegra194-p2888-* ${TEGRA_KERNEL_INSTALL}/

	;;
	"replace")
		if [ -f ${TEGRA_KERNEL_INSTALL}/Image ]; then
			cp ${TEGRA_KERNEL_INSTALL}/Image ${TOP}/../../../kernel/
			cp ${TEGRA_KERNEL_INSTALL}/tegra194-p2888-* ${TOP}/../../../kernel/dtb/
		else
			echo "${TEGRA_KERNEL_INSTALL}/Image is not exist!!!"
		fi
		
		if [ -f ${TEGRA_KERNEL_OUT}/${TEGRA_KERNEL_MODULENAME} ]; then
			cp ${TEGRA_KERNEL_OUT}/${TEGRA_KERNEL_MODULENAME} ${TOP}/../../../kernel/
		else
			echo "${TEGRA_KERNEL_OUT}/${TEGRA_KERNEL_MODULENAME} is not exist!!!"
		fi
	;;
	"-h")
	help
	;;
	*)
		echo "TEGRA_KERNEL_OUT = ${TEGRA_KERNEL_OUT}"
		make O=${TEGRA_KERNEL_BUILD} -j4
	;;
esac


