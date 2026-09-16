#!/bin/sh
# One-shot: flush the LuCI index cache on install/upgrade so the new
# /admin/network/firewall/benches menu entry and ACL are picked up.
# Runs once on first boot of the package.
rm -rf /tmp/luci-*
exit 0
