#
# Copyright (C) 2025 bs_agent
#
# This is free software, licensed under the MIT license.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-bsagent-firewall
PKG_VERSION:=1.1.0
PKG_RELEASE:=10
PKG_LICENSE:=MIT
PKG_MAINTAINER:=bs_agent
PKGARCH:=all

# Generic buildroot package (no luci.mk dependency); installs files/ verbatim.
include $(INCLUDE_DIR)/package.mk

define Package/luci-app-bsagent-firewall
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=BS Agent bench firewall mappings (LuCI)
  DEPENDS:=+luci
endef

define Package/luci-app-bsagent-firewall/description
  LuCI page at /admin/network/firewall/benches to manage bench DNAT mappings
  and the client allowlist. Mappings are native fw4 `config redirect` sections
  (gated by the allowed_bench_clients ipset) in /etc/config/firewall; the
  allowlist is the allowed_bench_clients ipset. fw4 renders all nft rules on
  reload -- no Python apply. A single declarative nft chain-pre rule isolates
  the Map IPs (drops non-allowlisted ingress to them); everything else is
  native fw4.
endef

define Build/Prepare
endef

define Build/Compile
endef

define Package/luci-app-bsagent-firewall/install
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/network/firewall
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_DATA) ./files/usr/share/luci/menu.d/luci-app-bsagent-firewall.json \
		$(1)/usr/share/luci/menu.d/
	$(INSTALL_DATA) ./files/usr/share/rpcd/acl.d/luci-app-bsagent-firewall.json \
		$(1)/usr/share/rpcd/acl.d/
	$(INSTALL_DATA) ./files/www/luci-static/resources/view/network/firewall/benches.js \
		$(1)/www/luci-static/resources/view/network/firewall/
	$(INSTALL_BIN) ./files/etc/uci-defaults/luci-bs-agent-firewall.sh $(1)/etc/uci-defaults/
	$(INSTALL_BIN) ./files/etc/uci-defaults/migrate-to-native-redirects.sh $(1)/etc/uci-defaults/
	$(INSTALL_DIR) $(1)/usr/share/nftables.d/chain-pre/input_wan
	$(INSTALL_DATA) ./files/usr/share/nftables.d/chain-pre/input_wan/30-bsagent-map-guard.nft \
		$(1)/usr/share/nftables.d/chain-pre/input_wan/
	$(INSTALL_DIR) $(1)/usr/share/nftables.d/table-pre
	$(INSTALL_DATA) ./files/usr/share/nftables.d/table-pre/30-bsagent-map-guard-set.nft \
		$(1)/usr/share/nftables.d/table-pre/
endef

$(eval $(call BuildPackage,luci-app-bsagent-firewall))
