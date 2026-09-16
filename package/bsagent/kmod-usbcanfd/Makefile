#
# Copyright (C) 2026 bs_agent
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=kmod-usbcanfd
PKG_VERSION:=2.10
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=bs_agent

include $(INCLUDE_DIR)/package.mk

define KernelPackage/usbcanfd
  SUBMENU:=$(CAN_MENU)
  TITLE:=ZLG USBCANFD-200U/400U SocketCAN driver
  DEPENDS:=+kmod-can +kmod-can-raw +kmod-usb-core
  FILES:=$(PKG_BUILD_DIR)/usbcanfd.ko
  AUTOLOAD:=$(call AutoLoad,60,usbcanfd)
endef

define KernelPackage/usbcanfd/description
  ZLG (Zhiyuan) USBCANFD-200U/400U USB adapter driver (driver version
  2.10). Registers SocketCAN netdevices for the CAN/CAN-FD channels
  (can0..can3) and LIN channels (lin0/lin1); LIN is configured via
  the lin*_cfg sysfs attributes. VID:PID 04cc:1240 and 3068:0009.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,usbcanfd))
