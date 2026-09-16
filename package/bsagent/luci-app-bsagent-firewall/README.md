# luci-app-bsagent-firewall

OpenWrt package that adds the **Bench Mappings** page at
`/admin/network/firewall/benches` for managing bench DNAT exposure.

It is a **pure LuCI app** (like `luci-app-firewall`): bench mappings are native
fw4 `config redirect` sections, each with a derived companion `config nat`
(return-path SNAT), and the allowlist is a native `config ipset` — all in
`/etc/config/firewall`. **fw4 renders all nft rules itself on `fw4 reload`** —
no Python apply, no init.d service. Neither the LuCI page nor `bs_agent` calls
`nft` at runtime. (Two declarative nft includes — see
[Map IP isolation](#map-ip-isolation) — are auto-included by fw4: a `table-pre`
set definition that makes the guard fail-safe, and a `chain-pre` rule that
drops non-allowlisted ingress to the Map IPs. The bench DNAT/SNAT/allowlist
itself stays 100% native, no include.)

Each bench mapping is a native `config redirect` **owned by the marker option
`option ipset allowed_bench_clients`** (not by a name prefix), so its `name` is
free to be the network interface holding the chosen Map IP: `<iface>` (e.g.
`bench0`). One mapping per interface — the interface name IS the mapping name.
The page's grid shows this name as the row title instead of the anonymous UCI
section id (`cfg…`).

| Section in `/etc/config/firewall` | Holds | Owned by |
|--------|-------|----------|
| `config redirect` (name `<iface>`, e.g. `bench0`) | bench mapping: DNAT `src_dip` (WAN) → `dest_ip` (bench), gated by `option ipset allowed_bench_clients` | this page / `bs_agent` — ownership = the ipset marker |
| `config nat` (SAME name as its redirect, e.g. `bench0`) | companion return-path SNAT: MASQUERADE traffic to the bench so a bench with no gateway can reply (derived 1:1 from each enabled bench redirect) | this page (auto, derived) — identified by signature (`target MASQUERADE` + `src lan` + a `dest_ip`) |
| `config ipset allowed_bench_clients` (`list entry`) | client allowlist (IPs/CIDRs that may reach the benches) | this page / `bs_agent` |
| `config ipset bench_map_ips` (`list entry`) | the live Map IPs (derived from the owned redirects' `src_dip`); drives the chain-pre guard | this page (auto, derived) |

A bench redirect DNATs only clients in the `allowed_bench_clients` ipset (fw4
emits `... ip saddr @allowed_bench_clients ... dnat <dest>`); fw4 also emits the
forward-accept and reflection SNAT. Apply = `fw4 reload` (LuCI Save & Apply
triggers it via the standard firewall ucitrack; `bs_agent` triggers it after a
UCI write).

## Companion SNAT (return path)

A bench on `br-lan` often ships with no default gateway, so a pure DNAT leaves
it unable to return-route replies to external clients — connections time out
(the original symptom this app had to solve). Each enabled bench mapping
therefore gets a **derived** companion `config nat` that shares the redirect's
**exact name** (`<iface>`, e.g. `bench0`), with
`target MASQUERADE`, `src lan`, `dest_ip` = bench. MASQUERADE auto-uses
`br-lan`'s address, so nothing hardcodes the router's LAN IP — it tracks
`network.lan.ipaddr` for free, and the bench replies on-link (no gateway
needed); conntrack then reverse-DNATs the reply to the real client. Sharing the
name is what links a redirect to its companion nat.

The nat set is **fully derived** from the redirect set: on every Save & Apply
the page reconciles the companion nats against the owned redirects — add /
update / delete / rename / enable / disable / dedup all stay in sync (`syncSnat`
in `benches.js`). A nat is recognized as a companion by **signature**
(`target MASQUERADE` + `src lan` + a `dest_ip` — the bench it SNATs for; a
generic LAN masquerade carries no `dest_ip`, so it is not swept) rather than by a custom marker
option, which keeps `fw4 reload` warning-free (fw4 would otherwise print
"unknown option" for any non-standard nat option). There is no separate UI for
the nats. Trade-off: SNAT hides the real client IP from the bench (the bench
sees the router's LAN IP, not the external source); preserving client-IP
transparency would instead require a default gateway on the bench and *no* SNAT.

Redirects created outside the page (by `bs_agent` or the UCI CLI) get their
companion nat on the next Save here, or by re-running the migration script /
`install-direct.sh` (the reconcile is idempotent).

`bs_agent`'s `NftBenchFirewallManager` (in the bs_agent wheel) is a thin UCI
client over these same sections (read local, write native redirects/ipset, then
`fw4 reload`); its public API is unchanged. Redirects it writes get their
companion `config nat` on the next Save in this page (or by re-running the
migration script — the reconcile is idempotent).

> **Naming history.** Earlier versions named redirects `bs_agent_bench:<target>`
> and nats `bs_agent_bench_snat:<target>`, and used that *prefix* as the
> ownership marker. A later scheme used `<iface>:<target>` (e.g.
> `bench0:irc100`). The current scheme simplifies the name to just `<iface>`
> (e.g. `bench0`) — one mapping per interface — and marks ownership with
> `option ipset allowed_bench_clients`. The migration script renames the
> ancient prefixed sections and re-tags them with the ipset marker; existing
> `<iface>:<target>` mappings are renamed to `<iface>` automatically the next
> time they are saved in the page.

## Map IP isolation

A bench Map IP (e.g. `10.0.96.169`) is a secondary IP on the WAN interface.
The bench **DNAT** is already gated by `allowed_bench_clients` (the redirect
emits `... ip saddr @allowed_bench_clients ... dnat <dest>`), so non-allowlisted
clients can't reach the bench. But a Map IP is also a *local* router address, so
a non-allowlisted source could still reach the router's **own** services through
it — e.g. `ssh root@<map_ip>` lands on the router's `sshd`, because the wan
`ssh`/`web`/`bs_agent` accept rules are not destination-aware and run before any
appended drop.

To close that, a single declarative nft rule is auto-included by fw4 at the top
of the `input_wan` chain (via
`/usr/share/nftables.d/chain-pre/input_wan/30-bsagent-map-guard.nft` — one of
fw4's standard `chain-pre` auto-include slots; no Python, survives reload/reboot):

```
ip daddr @bench_map_ips ip saddr != @allowed_bench_clients counter drop
```

`@bench_map_ips` is a second fw4 ipset whose `list entry` is the `src_dip` of
every enabled owned redirect (the live Map IPs). The rule drops any ingress
destined to a Map IP whose source is **not** in the allowlist, **before** the
`ssh`/`web`/`bs_agent` accepts. Effects:

- The bench DNAT is untouched — DNAT'd flows are forwarded (daddr rewritten to
  the bench) and never enter `input_wan`.
- The router's primary WAN IP (e.g. `10.0.96.168`) is **not** in
  `bench_map_ips`, so management SSH/DHCP/ICMPv6 to it are unaffected.
- If the allowlist contains `0.0.0.0/0`, the guard matches nothing (no-op); if
  the allowlist is empty, every Map IP is fully blocked.

The `bench_map_ips` set's **definition** is always guaranteed by a `table-pre`
include (`30-bsagent-map-guard-set.nft`, auto-included by fw4 at table level on
every load) — so the guard's `@bench_map_ips` reference always resolves and a
missing config section can never break `fw4 reload` / `/etc/init.d/firewall
restart` (fail-safe: no section → empty set → guard is a no-op, never a
broken firewall). Its **entries** (the live Map IPs) are seeded by the
migration uci-default and kept current on every Save by `syncMapIps` in
`benches.js` (mirrors `syncSnat`, and also creates the section on first need so
LuCI-added mappings populate the guard even if the migration never ran). It is
derived/hidden — there is no UI for it.

## Files

- `Makefile` — generic buildroot package definition (build host).
- `build-apk.sh` — build a proper apk-tools 3 (v3 adb) `.apk` without the
  buildroot, optionally signed (`--gen-key` / `--sign-key`). Uses the host
  `apk mkpkg` bundled with the imagebuilder/SDK and records root:root ownership
  via the bundled fakeroot. **Preferred install path.**
- `install-direct.sh` — place files onto the filesystem directly, bypassing apk
  entirely (no signature, not tracked by apk). No-apk fallback for quick installs.
- `files/usr/share/luci/menu.d/luci-app-bsagent-firewall.json` — menu node.
- `files/usr/share/rpcd/acl.d/luci-app-bsagent-firewall.json` — ACL grants
  read+write on `firewall` (redirects + ipset).
- `files/www/luci-static/resources/view/network/firewall/benches.js` — the view
  (mappings grid over `config redirect` filtered by the `ipset` ownership marker
  + allowlist dynamic list over the ipset entries).
- `files/etc/uci-defaults/luci-bs-agent-firewall.sh` — flush LuCI cache on install.
- `files/etc/uci-defaults/migrate-to-native-redirects.sh` — one-shot migration
  from the old abstract-config + custom-include model to native redirects; also
  creates + seeds the `bench_map_ips` ipset the guard references.
- `files/usr/share/nftables.d/chain-pre/input_wan/30-bsagent-map-guard.nft` —
  the map-IP isolation guard (fw4 `chain-pre` auto-include into `input_wan`).
- `files/usr/share/nftables.d/table-pre/30-bsagent-map-guard-set.nft` — the
  always-present `bench_map_ips` set definition (fw4 `table-pre` auto-include)
  that makes the guard fail-safe against a missing config ipset section.

## Install

**Preferred — signed apk (OpenWrt 24+/25, apk-tools 3):** build a proper v3 adb
package and sign it with your own EC P-256 key:
```sh
./build-apk.sh --gen-key     # one-time: creates key-build (+ key-build.pub) and signs
```
Deploy the public key to the device once (any filename under `/etc/apk/keys/`;
apk matches by fingerprint, not name), then install without `--allow-untrusted`:
```sh
scp key-build.pub root@<device>:/etc/apk/keys/key-build.pub
apk add ./luci-app-bsagent-firewall-*.apk
```
The uci-defaults scripts run on the next boot; to apply immediately after the
apk install:
```sh
sh /etc/uci-defaults/migrate-to-native-redirects.sh
sh /etc/uci-defaults/luci-bs-agent-firewall.sh
fw4 reload
```
If the device image was built from this repo's imagebuilder, its signing key is
already in `/etc/apk/keys/` — sign with
`--sign-key ../../openwrt-dev/imagebuilder/keys/local-private-key.pem` and skip
the `scp` (verify with `ls /etc/apk/keys/` on the device).

Unsigned quick install (no key deployed):
`./build-apk.sh` → `apk add --allow-untrusted ./luci-app-bsagent-firewall-*.apk`.

**No-apk fallback:** place files directly (not tracked by apk; no signature):
```sh
./install-direct.sh          # installs files + migrates old state + fw4 reload
```
(Uninstall list is printed by the script.)

**Build host (v3 apk for a feed) / SDK:** place this dir in your feed and
```sh
make package/luci-app-bsagent-firewall/compile V=s   # → bin/packages/<arch>/<feed>/
```
(On a minimal SDK, drop the `DEPENDS` line first — `luci` isn't in the SDK
package set; it's a runtime dep only. Then `make defconfig`, enable the package,
`make olddefconfig`, compile.)

## Migration from the old model

Older versions modelled mappings as `/etc/config/bs-agent-firewall`
(`config mapping`) + a custom `/etc/nftables.d/30-bs-agent.nft` include +
`bs_agent_bench:forward` fw4 rules, materialised by a Python script. The
`migrate-to-native-redirects.sh` uci-default converts that to native redirects
once (idempotent): mappings → redirects, removes the custom include + old forward
rules + the obsolete config. It also renames the ancient prefixed sections
(`bs_agent_bench:<target>` → `<iface>:<target>`, deriving `<iface>` from the
redirect's `src_dip`) and re-tags redirects with the `ipset allowed_bench_clients`
ownership marker; then it (re-)derives the companion return-path SNAT nat for
every owned bench redirect and dedups any duplicates, so re-running it after
`bs_agent`/CLI edits brings the derived nats back in sync. (Companion nats are
matched by signature `MASQUERADE + src lan + dest_ip`, so it manages both the
new `<iface>` names and legacy `<iface>:<target>` names.) The page itself names
mappings `<iface>` (one per interface); legacy `<iface>:<target>` names are
renamed to `<iface>` on the next in-page save. It runs on first boot
after install, and is also invoked by `install-direct.sh` (with an immediate
`fw4 reload`).

## Verified

- A bench redirect (`src_dip`/`dest_ip`/`ipset=allowed_bench_clients`) → `fw4 print`
  emits `ip daddr <map_ip> ip saddr @allowed_bench_clients counter dnat <dest_ip>`
  (gated DNAT), plus forward-accept + reflection SNAT.
- Each enabled bench mapping has a companion `config nat` (same name as the
  redirect, e.g. `bench0`) → `fw4 print` emits
  `ip daddr <bench_ip> counter masquerade` in `chain srcnat_lan`, rewriting the
  source to the router's LAN IP so a bench with no gateway can reply.
- The chain-pre `input_wan` guard emits
  `ip daddr @bench_map_ips ip saddr != @allowed_bench_clients counter drop`
  ahead of the ssh/web/bs_agent accepts, so a non-allowlisted source cannot
  reach the router's own services through a Map IP; the primary WAN IP and the
  bench DNAT (forwarded, not input) are unaffected.
- Ownership is the marker `option ipset allowed_bench_clients` on the redirect
  (the page's grid filter), not a name prefix; companion nats are matched by
  signature (`target MASQUERADE` + `src lan` + a `dest_ip`). The page only
  touches its own redirects + their derived nats + the named ipset — never
  user/luci-app-firewall rules.
