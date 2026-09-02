# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 OpenWrt.org

define KernelPackage/pru-remoteproc
  SUBMENU:=Other modules
  TITLE:=TI PRU-ICSS remoteproc support
  DEPENDS:=@TARGET_omap
  KCONFIG:= \
	CONFIG_TI_PRUSS \
	CONFIG_TI_PRUSS_INTC \
	CONFIG_TI_ICSS_IEP \
	CONFIG_PRU_REMOTEPROC
  FILES:= \
	$(LINUX_DIR)/drivers/soc/ti/pruss.ko \
	$(LINUX_DIR)/drivers/irqchip/irq-pruss-intc.ko \
	$(LINUX_DIR)/drivers/net/ethernet/ti/icssg/icss_iep.ko \
	$(LINUX_DIR)/drivers/remoteproc/pru_rproc.ko
  AUTOLOAD:=$(call AutoLoad,35,pruss irq-pruss-intc icss_iep pru_rproc)
endef

define KernelPackage/pru-remoteproc/description
  TI PRU-ICSS subsystem platform driver, interrupt controller, IEP
  timer and PRU remoteproc driver for AM33xx/AM43xx SoCs such as
  the BeagleBone Black (AM3358).

  The remoteproc framework (CONFIG_REMOTEPROC, CONFIG_REMOTEPROC_CDEV,
  CONFIG_SOC_TI) is built into the kernel by the omap target config.
  PRUs do not auto-boot; load firmware (e.g. am335x-pru0-fw ELF
  images from /lib/firmware) and start them via
  /sys/class/remoteproc/remoteproc*/state or /dev/rproc*.
endef

$(eval $(call KernelPackage,pru-remoteproc))
