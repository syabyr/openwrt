#
# rknn-runtime - Rockchip RKNPU userspace runtime + musl compat layer.
#
#   /usr/lib/librknnrt.so        Rockchip runtime 2.3.2 (glibc build,
#                                 patched --add-needed libgcompat.so)
#   /usr/lib/libgcompat.so       glibc -> musl symbol shim (built here)
#   /lib/ld-linux-aarch64.so.1  \
#   /lib/libc.so.6               |
#   /lib/libm.so.6               >  glibc soname aliases -> libc.so
#   /lib/libpthread.so.0         |
#   /lib/libdl.so.2             /
#
# Copyright (C) 2025 syabyr
#

include $(TOPDIR)/rules.mk

PKG_NAME:=rknn-runtime
PKG_VERSION:=2.3.2
PKG_RELEASE:=2
PKG_LICENSE:=proprietary
PKG_MAINTAINER:=syabyr <noreply@localhost>

include $(INCLUDE_DIR)/package.mk

# librknnrt.so is a prebuilt glibc binary; stripping (rstrip.sh, which also
# runs patchelf) corrupts its ELF layout, so never strip this package.
RSTRIP:=:

define Package/rknn-runtime
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=Rockchip RKNPU runtime (librknnrt.so) with musl compat layer
  DEPENDS:=+libstdcpp +libgcc
endef

define Package/rknn-runtime/description
  Rockchip RKNPU userspace runtime (librknnrt.so 2.3.2, glibc build)
  plus the musl compatibility layer.  libgcompat.so provides the glibc
  internal symbols (*_finite, mmap64, __pthread_key_create, __strdup,
  backtrace) that musl does not export; the libc.so.6 / libm.so.6 /
  libpthread.so.0 / libdl.so.2 / ld-linux-aarch64.so.1 soname aliases
  point at musl's libc.  librknnrt.so is patched with
  `--add-needed libgcompat.so`, so any program linked with -lrknnrt
  loads the shim automatically (no LD_PRELOAD required).
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -shared -fPIC \
		-o $(PKG_BUILD_DIR)/libgcompat.so $(PKG_BUILD_DIR)/libgcompat.c -lm
endef

define Package/rknn-runtime/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_DATA) ./files/usr/lib/librknnrt.so $(1)/usr/lib/librknnrt.so
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/libgcompat.so $(1)/usr/lib/libgcompat.so
	$(INSTALL_DIR) $(1)/lib
	ln -sf libc.so $(1)/lib/ld-linux-aarch64.so.1
	ln -sf libc.so $(1)/lib/libc.so.6
	ln -sf libc.so $(1)/lib/libm.so.6
	ln -sf libc.so $(1)/lib/libpthread.so.0
	ln -sf libc.so $(1)/lib/libdl.so.2
endef

$(eval $(call BuildPackage,rknn-runtime))
