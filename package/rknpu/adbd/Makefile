#
# adbd - Debian glibc adbd bundle under /opt/adbd, started via its own
# glibc loader.  Provides adb-over-network on tcp:5555, required by
# rknn-toolkit2 on the PC to reach rknn_server.
#
# Copyright (C) 2025 syabyr
#
# This is free software, licensed under the Apache License, Version 2.0.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=adbd
PKG_VERSION:=34.0.5
PKG_RELEASE:=3
PKG_LICENSE:=Apache-2.0
PKG_MAINTAINER:=syabyr <noreply@localhost>

include $(INCLUDE_DIR)/package.mk

# Prebuilt glibc binaries must not be stripped: rstrip.sh (which also runs
# patchelf) corrupts their ELF load layout ("not page-aligned").
RSTRIP:=:

define Package/adbd
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Android debug bridge daemon (Debian adbd 34.0.5 glibc bundle)
  URL:=https://android.googlesource.com/platform/packages/modules/adb/
endef

define Package/adbd/description
  Android Debug Bridge daemon (adbd) from the Debian sid arm64 package,
  shipped as a self-contained glibc closure under /opt/adbd and started
  through its own glibc loader.  Provides adb-over-network on tcp:5555,
  which rknn-toolkit2 on the PC uses to reach rknn_server.
endef

# Binary-only package: everything is prebuilt under ./files.
define Build/Prepare
endef

define Build/Compile
endef

define Package/adbd/install
	$(INSTALL_DIR) $(1)/opt/adbd
	$(CP) ./files/opt/adbd/. $(1)/opt/adbd/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/adbd $(1)/etc/init.d/adbd
endef

define Package/adbd/postinst
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] || /etc/init.d/adbd enable
exit 0
endef

$(eval $(call BuildPackage,adbd))
