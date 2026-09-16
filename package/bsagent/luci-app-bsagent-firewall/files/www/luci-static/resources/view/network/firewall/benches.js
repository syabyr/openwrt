'use strict';
'require view';
'require form';
'require uci';

// LuCI page at /admin/network/firewall/benches.
//
// A bench mapping is a native fw4 `config redirect` owned by the marker
// option `ipset 'allowed_bench_clients'`: it DNATs a WAN address (src_dip) to
// a bench (dest_ip) and is gated by that ipset. fw4 renders the gated DNAT +
// forward-accept + reflection SNAT itself on reload -- no Python apply, no
// custom nft include (mirrors luci-app-firewall). The allowlist is the
// allowed_bench_clients ipset entries.
//
// Each mapping's name is `<iface>` -- the network interface name holding the
// chosen Map IP (e.g. bench0). One mapping per interface (the interface name IS
// the mapping name). The name is shown as the grid row title and is the link
// between a redirect and its companion nat (they share the name).
//
// Each ENABLED mapping also gets a DERIVED companion `config nat` (the SAME
// name, target MASQUERADE, src lan, dest_ip=bench) so a bench with no gateway
// can reply to external clients (return-path SNAT). The nat set is reconciled
// against the redirects on every Save -- see syncSnat() below.

var IPSET = 'allowed_bench_clients';
var MAPIPS = 'bench_map_ips';	// ipset of the live Map IPs; drives the chain-pre guard

return view.extend({
	load: function () {
		return Promise.all([uci.load('firewall'), uci.load('network')]);
	},

	render: function () {
		// ---- Map IP (WAN) -> network interface name ----
		// A mapping name IS its <iface> -- the network interface holding the
		// chosen Map IP (one mapping per interface). Collect every static IPv4
		// across all interfaces (minus loopback/IPv6).
		var ipIface = {}; // ip -> iface name
		uci.sections('network', 'interface').forEach(function (iface) {
			var ifname = iface['.name'];
			var addrs = iface.ipaddr;
			if (!addrs) return;
			if (!Array.isArray(addrs)) addrs = [addrs];
			addrs.forEach(function (a) {
				a = (a || '').split('/')[0].trim();
				if (!a || a.indexOf(':') !== -1) return;	// skip empty / IPv6
				if (a.slice(0, 4) === '127.') return;		// skip loopback
				ipIface[a] = ifname;
			});
		});
		function ifaceOfIp(ip) { return ipIface[ip] || ip; } // fallback: the IP itself

		// ---- Mappings (config redirect, owned by the ipset marker) ----
		var map = new form.Map('firewall', _('Bench Mappings'),
			_('Each row exposes a bench (Dest IP) behind a WAN address (Map IP) via ' +
			  'DNAT, gated by the client allowlist. Native fw4 redirects; fw4 applies ' +
			  'on Save &amp; Apply.'));

		var s = map.section(form.GridSection, 'redirect', _('Bench mappings'));
		s.addremove = true;
		// Redirects are anonymous UCI sections (cfg…). Declaring `anonymous`
		// makes the GridSection render NO name-input before Add and keeps Add
		// always enabled (the default = named: a name input appears and Add stays
		// disabled until text is typed into it). handleAdd ignores any name
		// anyway -- it creates an anonymous redirect and opens the edit modal.
		s.anonymous = true;
		s.nodescriptions = true;
		s.addbtntitle = _('Add');

		// Ownership = the ipset marker option. This frees the name from any
		// fixed prefix, so the name can be the interface name itself (<iface>).
		s.filter = function (section_id) {
			return uci.get('firewall', section_id, 'ipset') === IPSET;
		};
		// Row title + edit-modal title = the mapping name (e.g. bench0) instead
		// of the anonymous UCI section id (cfg123456).
		s.sectiontitle = function (section_id) {
			return uci.get('firewall', section_id, 'name') || section_id;
		};
		// Preset a new bench redirect: ownership marker + the fixed DNAT options.
		s.handleAdd = function (ev) {
			var sid = uci.add('firewall', 'redirect');
			uci.set('firewall', sid, 'name', _('(new mapping)'));
			uci.set('firewall', sid, 'src', 'wan');
			uci.set('firewall', sid, 'dest', 'lan');
			uci.set('firewall', sid, 'target', 'DNAT');
			uci.set('firewall', sid, 'ipset', IPSET);
			uci.set('firewall', sid, 'enabled', '1');
			this.map.addedSection = sid;
			this.renderMoreOptionsModal(sid);
		};
		// Drop the companion nat explicitly at delete time (by exact name), so
		// the nat never outlives its redirect -- syncSnat only reconciles
		// against *existing* redirects and couldn't see a just-deleted one.
		s.handleRemove = function (section_id, ev) {
			var nm = uci.get('firewall', section_id, 'name') || '';
			uci.sections('firewall', 'nat').forEach(function (n) {
				if (n.name === nm && n.target === 'MASQUERADE' && n.src === 'lan')
					uci.remove('firewall', n['.name']);
			});
			uci.remove('firewall', section_id);
			return this.map.save(null, true);
		};

		var o;

		// Map IP (WAN) -- combobox of configured router IPs; required + unique.
		// The mapping NAME is derived from it on write: name = the network
		// interface holding this IP (e.g. 10.0.96.169 -> bench0). So each Map IP
		// AND each interface may back only one mapping (the interface name IS
		// the mapping name; two mappings on one interface would collide).
		o = s.option(form.Value, 'src_dip', _('Map IP (WAN)'));
		o.datatype = 'ipaddr';
		o.rmempty = false;
		o.description = _('A WAN-facing router IP. Pick from /etc/config/network or type a custom one. Each Map IP and each interface may be used by only one mapping (the interface name becomes the mapping name).');
		o.keylist = [];
		o.vallist = [];
		Object.keys(ipIface).sort().forEach(function (ip) {
			o.keylist.push(ip);
			o.vallist.push(ip + '  (' + ipIface[ip] + ')');
		});
		// Write src_dip AND derive the name from it (name = owning interface).
		o.write = function (section_id, value) {
			uci.set('firewall', section_id, 'src_dip', value);
			uci.set('firewall', section_id, 'name', ifaceOfIp(value));
		};
		o.validate = function (section_id, value) {
			if (!value || !value.length) return _('Required');
			var dup = uci.sections('firewall', 'redirect').some(function (r) {
				return r.ipset === IPSET && r.src_dip === value && r['.name'] !== section_id;
			});
			if (dup) return _('This Map IP is already used by another mapping');
			// name = ifaceOfIp(value); enforce one mapping per interface.
			var iface = ifaceOfIp(value);
			var ifdup = uci.sections('firewall', 'redirect').some(function (r) {
				return r.ipset === IPSET && r['.name'] !== section_id &&
					ifaceOfIp(r.src_dip) === iface;
			});
			return ifdup ? _('Interface already used by another mapping') + ': ' + iface : true;
		};

		// Dest IP (the bench behind br-lan).
		o = s.option(form.Value, 'dest_ip', _('Dest IP (bench)'));
		o.datatype = 'ipaddr';
		o.rmempty = false;

		o = s.option(form.Flag, 'enabled', _('Enabled'));
		o.rmempty = false;
		o.default = '1';

		// ---- Companion return-path SNAT (derived per mapping) ----
		//
		// A bench behind this router usually has no default gateway, so a pure
		// DNAT redirect leaves it unable to return-route SYN-ACK to external
		// clients -> connections time out. Each enabled bench mapping therefore
		// gets a companion `config nat` (target MASQUERADE, src lan, dest_ip =
		// bench) so the bench sees src = router lan IP (on-link, no gateway
		// needed) and conntrack reverse-DNATs the reply. MASQUERADE auto-uses
		// br-lan's address, so nothing hardcodes the lan IP.
		//
		// The nat set is FULLY DERIVED from the redirect set and reconciled on
		// every Save (add / update / delete / dedup / enable / disable). A nat
		// is a companion by SIGNATURE (MASQUERADE + src lan + a dest_ip -- the
		// bench it SNATs for) rather than by a custom marker option -- that
		// avoids the "[!] unknown option" warning fw4 prints on every reload
		// for unknown nat options. (The dest_ip requirement excludes generic
		// LAN masquerade, which carries no dest_ip.) Companion nats share the
		// EXACT name of their redirect (the interface name, e.g. bench0), so
		// redirect and nat are linked by name. The reconcile runs in the SHARED
		// uci.save() hook
		// (see below): both the parent grid Map and the add/edit modal's sub-Map
		// do `this.data = uci` (CBIMap.__init__) and call the same uci.save() in
		// their save chain (`Map.save = parse().then(cb).then(this.data.save)`),
		// so hooking uci.save reconciles on EVERY save -- including the modal add
		// path, which a map.save wrapper would miss. It runs after parse()
		// (option-writes are in the in-memory creates) and before the uci stage,
		// so derived nats land in the same Save & Apply transaction fw4 renders.

		// Session guard against the staging-visibility race. LuCI's uci.save()
		// STAGES creates to /tmp/.uci (rpcd uci.add/set/delete) and then reloads
		// state from COMMITTED /etc/config -- so a nat created this save becomes
		// invisible to uci.sections() on the NEXT save (it's staged, not
		// committed until apply). Without a guard, a 2nd save before apply would
		// not see the just-created nat and create a DUPLICATE (the bug). With the
		// guard, a 2nd pass skips creation; the staged nat lands on apply. The
		// guard is cleared when a name drops out of `want` (redirect deleted /
		// disabled / renamed) so a later re-add under the same name recreates it.
		var natCreated = {};
var mapIpsCreated = false;

		function syncSnat() {
			// name -> dest_ip for every owned, enabled redirect with a dest_ip
			// (name = the interface, e.g. bench0; legacy colon names like
			// bench0:irc100 still flow through and get renamed to <iface> on edit).
			var want = {};
			uci.sections('firewall', 'redirect').forEach(function (r) {
				if (r.ipset !== IPSET) return;
				if (r.enabled === '0') return;		// disabled mapping -> no nat
				if (!r.dest_ip) return;
				want[r.name || ''] = r.dest_ip;
			});

			// name -> [sids] for ALL existing companion nats (signature:
			// MASQUERADE + src lan + dest_ip). dest_ip excludes generic LAN
			// masquerade (which carries no dest_ip), so only SNAT-to-a-host
			// rules are swept -- the companion pattern. Older versions created
			// duplicates, so collect them all and collapse below. uci.sections()
			// merges in-session creates too, so this converges on double-save.
			var have = {};
			uci.sections('firewall', 'nat').forEach(function (n) {
				if (n.target !== 'MASQUERADE' || n.src !== 'lan' || !n.dest_ip) return;
				(have[n.name || ''] || (have[n.name || ''] = [])).push(n['.name']);
			});

			// Drop companion nats whose redirect vanished/got disabled, and
			// collapse duplicates. NOTE: the LuCI uci module's removal method
			// is `remove` (not `delete` -- `delete` is the JS operator; calling
			// uci.delete throws "not a function", which on the delete path
			// aborts the whole silent Save before staging, so nothing happens).
			Object.keys(have).forEach(function (nm) {
				var sids = have[nm];
				if (!want[nm]) {
					sids.forEach(function (sid) { uci.remove('firewall', sid); });
					delete natCreated[nm];	// name gone -> allow a future re-add
				} else if (sids.length > 1) {
					sids.slice(1).forEach(function (sid) { uci.remove('firewall', sid); });
				}
			});

			// upsert one desired nat per name (reuse the survivor if any). The
			// natCreated guard skips a 2nd creation when a prior nat is already
			// staged-but-invisible this session (see comment above).
			Object.keys(want).forEach(function (nm) {
				var sid = (have[nm] || [])[0];
				if (!sid) {
					if (natCreated[nm]) return;		// already created this session (staged) -> skip
					sid = uci.add('firewall', 'nat');
					uci.set('firewall', sid, 'name', nm);
					uci.set('firewall', sid, 'src', 'lan');
					uci.set('firewall', sid, 'target', 'MASQUERADE');
					natCreated[nm] = true;
				}
				uci.set('firewall', sid, 'dest_ip', want[nm]);
			});
		}

	// Keep the bench_map_ips ipset's `entry` list in sync with the enabled owned
	// redirects' src_dip (the live WAN Map IPs). The chain-pre input_wan guard
	// (/usr/share/nftables.d/chain-pre/input_wan/30-bsagent-map-guard.nft)
	// drops ingress to @bench_map_ips from any non-allowlisted source, so the
	// set must list every live Map IP for the guard to cover them.
	//
	// The set DEFINITION is always guaranteed by the table-pre include
	// 30-bsagent-map-guard-set.nft (so the guard never breaks the firewall load,
	// even with no config section). The ENTRIES need a config ipset section to
	// land in: it is created by the uci-defaults migration, and -- so that
	// LuCI-added mappings populate the guard even if the migration never ran --
	// also created HERE on first need. mapIpsCreated guards the same
	// staging-invisibility race as natCreated: a 2nd save before apply can't see
	// the just-staged section and would otherwise create a DUPLICATE bench_map_ips
	// ipset; the flag makes the create fire once per session.
	//
	// Array-valued uci.set is the SAME list-write path the allowlist DynamicList
	// uses (rpcd serialises an array-valued option as `list entry`); an empty
	// want clears the list (null -> option delete). Like syncSnat this reads the
	// redirects (source of truth), so it converges regardless of staging state.
	function syncMapIps() {
		var want = [];
		uci.sections('firewall', 'redirect').forEach(function (r) {
			if (r.ipset !== IPSET) return;
			if (r.enabled === '0') return;		// disabled mapping -> not a live Map IP
			if (r.src_dip && want.indexOf(r.src_dip) < 0) want.push(r.src_dip);
		});
		var sid = null;
		uci.sections('firewall', 'ipset').forEach(function (i) {
			if (i.name === MAPIPS) sid = i['.name'];
		});
		if (!sid) {
			// No live Map IPs AND no section -> nothing to populate; the table-pre
			// include already defines the (empty) set, so leave it. Only create the
			// config section when there are actually entries to write.
			if (!want.length) return;
			if (mapIpsCreated) return;		// already created this session (staged) -> skip
			sid = uci.add('firewall', 'ipset');
			uci.set('firewall', sid, 'name', MAPIPS);
			uci.set('firewall', sid, 'family', 'ipv4');
			uci.set('firewall', sid, 'match', ['net']);	// array -> list match (rpcd)
			mapIpsCreated = true;
		}
		if (want.length)
			uci.set('firewall', sid, 'entry', want);	// array -> list entry (rpcd)
		else
			uci.set('firewall', sid, 'entry', null);	// clear the list
	}

	// Hook the SHARED uci singleton's save(), not this map's save(). Both the
		// parent grid Map and the add/edit modal's sub-Map do `this.data = uci`
		// (CBIMap.__init__) and call the same uci.save() in their save chain
		// (`Map.save = parse().then(cb).then(this.data.save)`). Wrapping map.save
		// only catches parent saves and lets the modal add path bypass reconcile
		// entirely (no companion nat); wrapping uci.save reconciles on EVERY save,
		// including the modal. syncSnat only touches bench redirects/nats, so it is
		// a harmless no-op when the allowlist Map (or any non-bench state) saves.
		// bsagentHooked guards against a double-wrap if render() runs twice.
		if (!uci.save.bsagentHooked) {
			var origUciSave = uci.save.bind(uci);
			uci.save = function () {
				syncSnat();
				syncMapIps();
				return origUciSave.apply(uci, arguments);
			};
			uci.save.bsagentHooked = true;
		}

		// ---- Allowlist (ipset allowed_bench_clients entries) ----
		var fw = new form.Map('firewall', _('Client allowlist'),
			_('IPs/CIDRs permitted to reach the benches. Stored as list entries of the ' +
			  'fw4-owned ipset "allowed_bench_clients"; the set definition itself is managed by fw4.'));

		var fs = fw.section(form.TypedSection, 'ipset', _('Allowed client IPs'));
		fs.filter = function (section_id) {
			return uci.get('firewall', section_id, 'name') === IPSET;
		};
		fs.anonymous = true;

		o = fs.option(form.DynamicList, 'entry', _('Allowed IPs / CIDRs'));
		o.datatype = 'ipaddr';
		o.placeholder = '0.0.0.0/0';

		return Promise.all([map.render(), fw.render()]).then(function (nodes) {
			return E('div', {}, nodes);
		});
	}
});
