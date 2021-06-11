old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := platform/t19x/retronix/kernel-dts

dtb-$(CONFIG_ARCH_TEGRA_18x_SOC) += tegra194-p2888-ADT-Drone.dtb
#~ dtb-$(CONFIG_ARCH_TEGRA_18x_SOC) += tegra194-p2888-RTX-0000.dtb

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
