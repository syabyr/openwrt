#!/bin/sh
# migrate-to-native-redirects.sh -- one-shot migration to native fw4 redirects
# using the <iface>:<target> naming scheme.
#
# History: earlier versions modelled bench mappings as an abstract
# /etc/config/bs-agent-firewall `config mapping` set + a custom nft include
# (/etc/nftables.d/30-bs-agent.nft) + bs_agent_bench:forward fw4 rules,
# materialised by a Python apply script. That was replaced by native fw4
# `config redirect` sections (gated by the allowed_bench_clients ipset) which
# fw4 renders itself -- no Python, no include. The FIRST native scheme used the
# name prefixes bs_agent_bench: / bs_agent_bench_snat:; this script renames
# those to the current <iface>:<target> scheme (e.g. bench0:irc100) and
# reconciles the companion return-path nats.
#
# A bench redirect is owned by `option ipset allowed_bench_clients` and named
# <iface>:<target>. Its companion return-path nat shares the SAME name and is
# identified by signature (target MASQUERADE + src lan + a colon name) -- no
# custom marker option, so fw4 reload stays warning-free.
#
# Runs once at boot via uci-defaults, and is also invoked by install-direct.sh.
# Safe to re-run: every step is idempotent. No fw4 reload here -- uci-defaults
# run before the firewall init at boot (which reloads fw4); for manual
# invocation the caller (or a reboot) reloads fw4.

set -u

OLD_PREFIX="bs_agent_bench:"
OLD_NAT_PREFIX="bs_agent_bench_snat:"
IPSET="allowed_bench_clients"
MAPIPS="bench_map_ips"
OLD_CFG="bs-agent-firewall"

# iface_of_ip <ip> -> network interface name holding that IPv4 (per
# /etc/config/network), or the ip itself if none (mirrors the LuCI fallback).
# NOTE: no `return`/`break`-only early exit -- BusyBox ash (this box's /bin/sh)
# mishandles `return` inside the $(...) subshell and skips the caller's next
# statements, so we drive the loops with a flag instead.
iface_of_ip() {
	_ip="${1%%/*}"
	_found=
	for _if in $(uci -q show network 2>/dev/null | sed -n 's/^network\.\([^.]*\)=interface$/\1/p'); do
		for _a in $(uci -q get "network.$_if.ipaddr" 2>/dev/null); do
			_a="${_a%%/*}"
			[ "$_a" = "$_ip" ] && _found=$_if
			[ -n "$_found" ] && break
		done
		[ -n "$_found" ] && break
	done
	echo "${_found:-$_ip}"
}

# redirect_name_for_target <target> -> full <iface>:<target> name of the owned
# redirect whose name ends in ":<target>", or empty. Target has no colon, so a
# name with exactly one colon matches uniquely. (grep+head, no while/return --
# see iface_of_ip note on ash.)
redirect_name_for_target() {
	uci -q show firewall 2>/dev/null \
		| sed -n "s/^firewall\.@redirect\[[0-9]*\]\.name='\([^']*\)'\$/\1/p" \
		| grep -e ":$1\$" | head -n1
}

# nat_sids_by_name <name> -> ascending list of @nat[idx] sids with that name.
nat_sids_by_name() {
	uci -q show firewall 2>/dev/null \
		| sed -n "s/^firewall\.\(@nat\[[0-9]*\]\)\.name='$1'\$/\1/p"
}

# ipset_sid_by_name <name> -> the @ipset[idx] sid with that name, or empty.
# (ipset sections are anonymous, hence the @ipset[idx] form.) No `return` --
# BusyBox ash (this box's /bin/sh) mishandles `return` inside $(...) and skips
# the caller's following statement (see iface_of_ip note above); the sed|head
# pipeline exits the helper cleanly without it.
ipset_sid_by_name() {
	uci -q show firewall 2>/dev/null \
		| sed -n "s/^firewall\.\(@ipset\[[0-9]*\]\)\.name='$1'\$/\1/p" \
		| head -n1
}

# reverse_stdin -> input lines in reverse order (highest @nat index first).
reverse_stdin() { awk '{a[NR]=$0} END{for(i=NR;i>=1;i--) print a[i]}'; }

# keep exactly one nat named <name> with dest_ip=<dip> (create if none, update
# the lowest-index survivor, delete extras highest-index first). MASQUERADE +
# src lan are (re)asserted so a stray old nat is normalized in place.
reconcile_named_nat() {
	_nm="$1"; _dip="$2"
	_first=$(nat_sids_by_name "$_nm" | head -n1)
	if [ -z "$_first" ]; then
		_nsid=$(uci add firewall nat)
		uci set "firewall.$_nsid.name=$_nm"
		uci set "firewall.$_nsid.src=lan"
		uci set "firewall.$_nsid.target=MASQUERADE"
		uci set "firewall.$_nsid.dest_ip=$_dip"
	else
		uci set "firewall.$_first.src=lan"
		uci set "firewall.$_first.target=MASQUERADE"
		uci set "firewall.$_first.dest_ip=$_dip"
		nat_sids_by_name "$_nm" | tail -n +2 | reverse_stdin | while IFS= read -r _extra; do
			[ -n "$_extra" ] && uci -q delete "firewall.$_extra"
		done
	fi
}

# delete every nat named <name>, highest index first.
delete_named_nat() {
	nat_sids_by_name "$1" | reverse_stdin | while IFS= read -r _sid; do
		[ -n "$_sid" ] && uci -q delete "firewall.$_sid"
	done
}

# 1) Migrate any leftover abstract /etc/config/bs-agent-firewall mappings to
#    native redirects (skip disabled). Idempotent. Produces old-prefix names;
#    step 5 renames them to <iface>:<target>.
if uci -q show "$OLD_CFG" >/dev/null 2>&1; then
	for sid in $(uci -q show "$OLD_CFG" | sed -n 's/^'"$OLD_CFG"'\.\([^.]*\)=mapping$/\1/p'); do
		en=$(uci -q get "$OLD_CFG.$sid.enabled"); en=${en:-1}
		[ "$en" = "0" ] && continue
		mip=$(uci -q get "$OLD_CFG.$sid.map_ip")
		dip=$(uci -q get "$OLD_CFG.$sid.dest_ip")
		[ -z "$mip" ] || [ -z "$dip" ] && continue
		name="${OLD_PREFIX}${mip}->${dip}"
		uci -q show firewall | grep -q "\.name='${name}'" && continue
		rsid=$(uci add firewall redirect)
		uci set "firewall.$rsid.name=$name"
		uci set "firewall.$rsid.src=wan"
		uci set "firewall.$rsid.dest=lan"
		uci set "firewall.$rsid.src_dip=$mip"
		uci set "firewall.$rsid.dest_ip=$dip"
		uci set "firewall.$rsid.target=DNAT"
		uci set "firewall.$rsid.ipset=$IPSET"
		uci set "firewall.$rsid.enabled=1"
	done
	uci -q commit firewall
fi

# 2) Remove old bs_agent_bench:forward fw4 rules (forward-accept is native now).
for sid in $(uci -q show firewall | sed -n "s/^firewall\.\([^.]*\)\.name='bs_agent_bench:forward.*$/\1/p"); do
	uci -q delete "firewall.$sid"
done
uci -q commit firewall

# 3) Remove the custom nft include + flush its chains (fw4 renders all now).
rm -f /etc/nftables.d/30-bs-agent.nft
for stage in dstnat srcnat; do
	nft "flush chain inet fw4 bs_agent_bench_${stage}" 2>/dev/null || true
	nft "delete chain inet fw4 bs_agent_bench_${stage}" 2>/dev/null || true
done

# 4) Remove the obsolete abstract mapping config (migrated in step 1).
rm -f /etc/config/bs-agent-firewall

# 5) Rename legacy prefixed sections to <iface>:<target>.
#    bs_agent_bench:<target>      -> <iface>:<target>   (iface from src_dip)
#    bs_agent_bench_snat:<target> -> <iface>:<target>   (matched via redirect)
#    Setting `.name` does not shift section indices, so iterating @type[idx]
#    sids collected up front is safe. Legacy colon-less names are untouched.

# 5a) rename redirects (and ensure the ipset ownership marker is present)
for sid in $(uci -q show firewall | sed -n 's/^firewall\.\(@redirect\[[0-9]*\]\)=redirect$/\1/p'); do
	nm=$(uci -q get "firewall.$sid.name")
	case "$nm" in
		"${OLD_PREFIX}"*) ;;
		*) continue ;;
	esac
	target=${nm#"$OLD_PREFIX"}
	mip=$(uci -q get "firewall.$sid.src_dip")
	iface=$(iface_of_ip "$mip")
	uci set "firewall.$sid.name=${iface}:${target}"
	uci set "firewall.$sid.ipset=$IPSET"
done
uci -q commit firewall

# 5b) rename companion nats via the (now renamed) redirect
for sid in $(uci -q show firewall | sed -n 's/^firewall\.\(@nat\[[0-9]*\]\)=nat$/\1/p'); do
	nm=$(uci -q get "firewall.$sid.name")
	case "$nm" in
		"${OLD_NAT_PREFIX}"*) ;;
		*) continue ;;
	esac
	target=${nm#"$OLD_NAT_PREFIX"}
	newname=$(redirect_name_for_target "$target")
	[ -n "$newname" ] && uci set "firewall.$sid.name=$newname"
done
uci -q commit firewall

# 6) Reconcile companion return-path nats (mirrors LuCI syncSnat()).
#    Build the want set (owned, enabled redirects with a dest_ip), then for
#    each wanted name keep exactly one nat (6a) and drop any companion nat
#    whose name is not wanted (6b). All deletes use the highest matching index
#    first so lower @nat indices never shift mid-loop. Companion signature is
#    MASQUERADE + src lan + dest_ip (matches LuCI; works for both the new
#    <iface> names and legacy <iface>:<target> names).

want=""
for sid in $(uci -q show firewall | sed -n 's/^firewall\.\(@redirect\[[0-9]*\]\)=redirect$/\1/p'); do
	[ "$(uci -q get "firewall.$sid.ipset")" = "$IPSET" ] || continue
	nm=$(uci -q get "firewall.$sid.name")
	en=$(uci -q get "firewall.$sid.enabled"); en=${en:-1}
	[ "$en" = "0" ] && continue
	dip=$(uci -q get "firewall.$sid.dest_ip")
	[ -z "$dip" ] && continue
	want="${want}${nm}|${dip}
"
done

# 6a) upsert + dedup per wanted name
echo "$want" | while IFS='|' read -r wnm wdip; do
	[ -n "$wnm" ] && reconcile_named_nat "$wnm" "$wdip"
done
uci -q commit firewall

# 6b) collect orphan companion-nat names (not wanted), then delete them.
#     Companion signature: MASQUERADE + src lan + dest_ip (no colon-name
#     requirement -- matches the new <iface> names). No deletes during
#     collection, so the @nat indices read here are stable.
orphans=""
for sid in $(uci -q show firewall | sed -n 's/^firewall\.\(@nat\[[0-9]*\]\)=nat$/\1/p'); do
	[ "$(uci -q get "firewall.$sid.target")" = "MASQUERADE" ] || continue
	[ "$(uci -q get "firewall.$sid.src")" = "lan" ] || continue
	[ -n "$(uci -q get "firewall.$sid.dest_ip")" ] || continue
	nm=$(uci -q get "firewall.$sid.name")
	echo "$want" | grep -q "^${nm}|" || orphans="${orphans}${nm}
"
done
echo "$orphans" | sort -u | while IFS= read -r onm; do
	[ -n "$onm" ] && delete_named_nat "$onm"
done

uci -q commit firewall

# 7) (Re)seed the bench_map_ips ipset ENTRIES from the enabled owned redirects.
#    The set DEFINITION is always guaranteed by the table-pre include
#    30-bsagent-map-guard-set.nft (auto-included by fw4 on every load), so the
#    chain-pre guard's @bench_map_ips reference always resolves -- even if this
#    step never runs (fail-safe: missing section -> empty set -> guard no-op,
#    never a broken firewall). This step creates the config ipset section and
#    seeds its entries so the guard is ACTIVE at first boot (not waiting for the
#    first LuCI Save). benches.js syncMapIps keeps it current at runtime.
#    Idempotent: creates the section once, and always rebuilds the entry list.
msid=$(ipset_sid_by_name "$MAPIPS")
if [ -z "$msid" ]; then
	msid=$(uci add firewall ipset)
	uci set "firewall.$msid.name=$MAPIPS"
	uci set "firewall.$msid.family=ipv4"
	uci add_list "firewall.$msid.match=net"
fi
# (re)seed: drop any old entries, then add each enabled owned redirect's src_dip.
uci -q delete "firewall.$msid.entry" 2>/dev/null || true
for sid in $(uci -q show firewall | sed -n 's/^firewall\.\(@redirect\[[0-9]*\]\)=redirect$/\1/p'); do
	[ "$(uci -q get "firewall.$sid.ipset")" = "$IPSET" ] || continue
	en=$(uci -q get "firewall.$sid.enabled"); en=${en:-1}
	[ "$en" = "0" ] && continue
	mip=$(uci -q get "firewall.$sid.src_dip")
	[ -n "$mip" ] && uci add_list "firewall.$msid.entry=$mip"
done
uci -q commit firewall

exit 0
