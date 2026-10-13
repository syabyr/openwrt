#
# Copyright (C) 2026 bs_agent
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
# WCH CH342/CH343/CH344/CH346/CH347/CH910x/CH9143 etc. USB-to-UART driver
# (vendor version V2.2, 2026.04). Upstream vendor package kept at
# ../ch343ser_linux (demo, lib, udev rules); only the driver is packaged
# here. The driver is standalone: it registers its own tty driver
# (major 170) and struct usb_driver directly -- it does NOT use the
# kernel's usb_serial core, so no kmod-usb-serial dependency.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=kmod-ch343
PKG_VERSION:=2.2
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0+
PKG_MAINTAINER:=bs_agent

include $(INCLUDE_DIR)/package.mk

define KernelPackage/ch343
  SUBMENU:=USB Support
  TITLE:=WCH CH343/CH34x USB-to-UART driver
  DEPENDS:=+kmod-usb-core
  FILES:=$(PKG_BUILD_DIR)/ch343.ko
  AUTOLOAD:=$(call AutoLoad,60,ch343)
endef

define KernelPackage/ch343/description
  USB serial driver for WCH multi-port UART chips: CH342/CH343/CH344/
  CH346/CH347/CH339/CH9101..CH9105/CH9143/CH9111/CH9114 (vendor version
  V2.2). Registers one tty (ttyUSB*) per port via its own tty driver
  (major 170); supports non-standard high baud rates (CH347 etc. up to
  2Mbps+) and GPIO access on CH344/CH347 via /dev/ch343_iodev*.
  Matches USB VID 1a86 (WCH) product ids 55d2/55d3/55d5/55d6/55da...
  (see the ch343_ids table in ch343.c).
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,ch343))
