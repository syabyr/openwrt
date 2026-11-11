#
# rknn-server - Rockchip RKNN Toolkit2 board-side server.
#
#   /usr/lib/rknn_server          pristine glibc binary (do NOT patchelf)
#   /usr/bin/rknn_server          wrapper: LD_PRELOAD=libgcompat.so + exec
#   /usr/bin/transfer_proxy_relay musl-static relay bridging the adb
#                                 "local:transfer_proxy" socket to the
#                                 abstract @transfer_proxy socket
#   /usr/bin/nohup                HUP-ignoring shim (busybox lacks the applet)
#
# The musl compat layer (libgcompat.so, glibc soname aliases, the
# ld-linux-aarch64.so.1 interpreter name) is provided by the
# rknn-runtime package, which this one depends on.
#
# Copyright (C) 2025 syabyr
#

include $(TOPDIR)/rules.mk

PKG_NAME:=rknn-server
PKG_VERSION:=2.3.2
PKG_RELEASE:=2
PKG_LICENSE:=proprietary
PKG_MAINTAINER:=syabyr <noreply@localhost>

include $(INCLUDE_DIR)/package.mk

# rknn_server is a pristine glibc binary; rstrip.sh (which also runs
# patchelf) corrupts its ELF layout, so never strip this package.
RSTRIP:=:

define Package/rknn-server
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Rockchip RKNN Toolkit2 board-side server (rknn_server + transfer_proxy relay)
  DEPENDS:=+adbd +rknn-runtime +libstdcpp +libgcc
endef

define Package/rknn-server/description
  Board-side counterpart of the RKNN-Toolkit2 "connect to board" flow.
  Runs rknn_server (glibc) on musl via libgcompat, plus a small relay that
  bridges the adb forward socket (filesystem /dev/socket/transfer_proxy) to
  the abstract @transfer_proxy socket rknn_server listens on.  Depends on
  adbd for the adb-over-network link and on the musl C++/GCC runtimes.
endef

# Binary-only package: everything is prebuilt under ./files.
define Build/Prepare
endef

define Build/Compile
endef

define Package/rknn-server/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) ./files/usr/bin/rknn_server $(1)/usr/bin/rknn_server
	$(INSTALL_BIN) ./files/usr/bin/nohup $(1)/usr/bin/nohup
	$(INSTALL_BIN) ./files/usr/bin/transfer_proxy_relay $(1)/usr/bin/transfer_proxy_relay
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_BIN) ./files/usr/lib/rknn_server $(1)/usr/lib/rknn_server
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/rknn-server $(1)/etc/init.d/rknn-server
endef

define Package/rknn-server/postinst
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] || /etc/init.d/rknn-server enable
exit 0
endef

$(eval $(call BuildPackage,rknn-server))
