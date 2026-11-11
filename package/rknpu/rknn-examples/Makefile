#
# rknn-examples - RKNPU example programs for bring-up and benchmarking.
#
#   rknn_smoke_test  deterministic pseudo-input, top-5 + checksum
#   rknn_test        image classification (stb_image, no external codec)
#   rknn_bench       latency / FPS / NPU-load benchmark for given models
#
# The programs are cross-compiled with the OpenWrt musl toolchain and
# linked against the prebuilt RKNPU runtime (glibc build) shipped in the
# image overlay.  --allow-shlib-undefined keeps librknnrt's glibc-internal
# undefined symbols as runtime (libgcompat) resolutions.
#
# Copyright (C) 2025 syabyr
#

include $(TOPDIR)/rules.mk

PKG_NAME:=rknn-examples
PKG_VERSION:=2.3.2
PKG_RELEASE:=1
PKG_LICENSE:=proprietary
PKG_MAINTAINER:=syabyr <noreply@localhost>

include $(INCLUDE_DIR)/package.mk

# Directory containing the prebuilt librknnrt.so used at link time,
# provided by the rknn-runtime package.  Override on the command line
# if needed:  make package/rknpu/rknn-examples/compile RKNN_LIB_DIR=...
RKNN_LIB_DIR?=$(TOPDIR)/package/rknpu/rknn-runtime/files/usr/lib

RKNN_EXAMPLES:=rknn_smoke_test rknn_test rknn_bench

define Package/rknn-examples
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=RKNPU example programs (smoke test, benchmark, image classifier)
  DEPENDS:=+rknn-runtime +libstdcpp +libgcc
endef

define Package/rknn-examples/description
  Three small RKNN C programs for bringing up and benchmarking the NPU:
  rknn_smoke_test (deterministic pseudo-input, top-5 + checksum),
  rknn_test (image classification using the bundled stb_image decoder)
  and rknn_bench (per-model latency / FPS / NPU load).  Built against the
  v2.3.2 runtime API.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -Wall \
		-I$(PKG_BUILD_DIR)/include \
		-o $(PKG_BUILD_DIR)/rknn_smoke_test $(PKG_BUILD_DIR)/rknn_smoke_test.c \
		-L$(RKNN_LIB_DIR) -lrknnrt -Wl,--allow-shlib-undefined -lm
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -Wall \
		-I$(PKG_BUILD_DIR)/include \
		-o $(PKG_BUILD_DIR)/rknn_test $(PKG_BUILD_DIR)/rknn_test.c \
		-L$(RKNN_LIB_DIR) -lrknnrt -Wl,--allow-shlib-undefined -lm
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -Wall \
		-I$(PKG_BUILD_DIR)/include \
		-o $(PKG_BUILD_DIR)/rknn_bench $(PKG_BUILD_DIR)/rknn_bench.c \
		-L$(RKNN_LIB_DIR) -lrknnrt -Wl,--allow-shlib-undefined -lm
endef

define Package/rknn-examples/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/rknn_smoke_test $(1)/usr/bin/rknn_smoke_test
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/rknn_test $(1)/usr/bin/rknn_test
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/rknn_bench $(1)/usr/bin/rknn_bench
endef

$(eval $(call BuildPackage,rknn-examples))
