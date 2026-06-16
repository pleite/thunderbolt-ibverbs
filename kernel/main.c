// SPDX-License-Identifier: GPL-2.0
/*
 * Release-track Thunderbolt/USB4 RDMA verbs module.
 *
 * This file intentionally starts small. Hardware binding, verbs registration,
 * native Linux transport, and Apple-compatible transport will be added behind
 * the profile model defined here instead of inheriting the exploratory module's
 * debug parameters.
 */

#define pr_fmt(fmt) "thunderbolt_ibverbs: " fmt

#include <linux/init.h>
#include <linux/hex.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "tbv.h"

#define TBV_HEX_CHARS_PER_BYTE 2
#define TBV_PEER_AUTH_PSK_HEX_LEN \
	(TBV_PEER_AUTH_PSK_LEN * TBV_HEX_CHARS_PER_BYTE)

static char *compat = "auto";
module_param(compat, charp, 0444);
MODULE_PARM_DESC(compat, "Compatibility mode: auto, force, off");

static char *profile = "auto";
module_param(profile, charp, 0444);
MODULE_PARM_DESC(profile, "Profile: auto, mac_compat, linux_perf, mixed");

static char *tbnet = "auto";
module_param(tbnet, charp, 0444);
MODULE_PARM_DESC(tbnet, "Thunderbolt-net coexistence: auto, allow, prefer_rdma, block");

static char *tbnet_identity = "auto";
module_param(tbnet_identity, charp, 0444);
MODULE_PARM_DESC(tbnet_identity,
		 "Apple TBnet identity: auto, stock, stock_proxy, minimal_packet, off");

static char *tbnet_identity_tbnet = "thunderbolt0";
module_param(tbnet_identity_tbnet, charp, 0444);
MODULE_PARM_DESC(tbnet_identity_tbnet,
		 "Netdev that carries stock ThunderboltIP packets for tbnet_identity=stock_proxy");

static char *tbnet_identity_gid = "auto";
module_param(tbnet_identity_gid, charp, 0444);
MODULE_PARM_DESC(tbnet_identity_gid,
		 "Netdev whose IPv4 address is proxied as the RDMA GID for tbnet_identity=stock_proxy; auto uses roce_netdev");

static bool tbnet_identity_minimal_e2e;
module_param(tbnet_identity_minimal_e2e, bool, 0444);
MODULE_PARM_DESC(tbnet_identity_minimal_e2e,
		 "Enable E2E flow control on minimal ThunderboltIP packet rings");

static bool tbnet_identity_minimal_apple_only = true;
module_param(tbnet_identity_minimal_apple_only, bool, 0444);
MODULE_PARM_DESC(tbnet_identity_minimal_apple_only,
		 "Bind minimal ThunderboltIP packet identity only to Apple peers");

static char *lanes = "auto";
module_param(lanes, charp, 0444);
MODULE_PARM_DESC(lanes, "Lane request: auto, N, or MIN-MAX");

static bool bind_services;
module_param(bind_services, bool, 0444);
MODULE_PARM_DESC(bind_services,
		 "Register Thunderbolt service drivers and advertise services");

static uint native_prtcstns;
module_param(native_prtcstns, uint, 0444);
MODULE_PARM_DESC(native_prtcstns,
		 "Protocol settings advertised by the native Linux service");

static uint apple_prtcstns;
module_param(apple_prtcstns, uint, 0444);
MODULE_PARM_DESC(apple_prtcstns,
		 "Protocol settings advertised by the Apple AD/FA57 service");

static bool allocate_rings;
module_param(allocate_rings, bool, 0444);
MODULE_PARM_DESC(allocate_rings,
		 "Allocate Thunderbolt rings on service probe without enabling paths");

static bool start_rings;
module_param(start_rings, bool, 0444);
MODULE_PARM_DESC(start_rings,
		 "Start allocated Thunderbolt rings without enabling paths");

static bool negotiate_native;
module_param(negotiate_native, bool, 0444);
MODULE_PARM_DESC(negotiate_native,
		 "Send a native HELLO after ring start without enabling paths");

static bool enable_tunnels;
module_param(enable_tunnels, bool, 0444);
MODULE_PARM_DESC(enable_tunnels,
		 "Enable negotiated Thunderbolt paths after native HELLO");

static bool native_data = true;
module_param(native_data, bool, 0444);
MODULE_PARM_DESC(native_data,
		 "Allow native Linux peers to allocate rings and enable data paths");

/*
 * apple_data: -1 = auto (true when the resolved profile enables the Apple
 * transport), 0 = off, positive = on.  Most users should leave this unset;
 * the profile=mac_compat and profile=mixed selections enable it implicitly.
 */
static int apple_data_param = -1;
module_param_named(apple_data, apple_data_param, int, 0444);
MODULE_PARM_DESC(apple_data,
		 "Allow Apple-compatible data paths: -1=auto (from profile), 0=off, 1=on");

static bool native_fragment_striping;
module_param(native_fragment_striping, bool, 0444);
MODULE_PARM_DESC(native_fragment_striping,
		 "Stripe native Linux SEND fragments across active rails");

static bool register_verbs;
module_param(register_verbs, bool, 0444);
MODULE_PARM_DESC(register_verbs,
		 "Register a guarded libibverbs device skeleton");

static char *peer_allowlist;
module_param(peer_allowlist, charp, 0444);
MODULE_PARM_DESC(peer_allowlist,
		 "Optional comma-separated remote host UUID allow-list (for example 00112233-4455-6677-8899-aabbccddeeff)");

static bool production_mode;
module_param(production_mode, bool, 0400);
MODULE_PARM_DESC(production_mode,
		 "Disable debugfs/configfs diagnostic surfaces for production deployments");
static char *peer_auth_acl;
module_param(peer_auth_acl, charp, 0444);
MODULE_PARM_DESC(peer_auth_acl,
		 "Required native peer auth ACL as comma-separated uuid=32hexpsk entries");

#ifdef CONFIG_TBV_GPU_DIRECT
/*
 * gpu_direct: load-time gate for the dma-buf (GPU-direct) memory-region path.
 * Defaults to auto so that, when the feature is compiled in, registration is
 * attempted and cleanly falls back to EOPNOTSUPP if runtime dma-buf support is
 * absent.  See docs/gpu-direct-plan.md Phase 1.
 */
static char *gpu_direct = "auto";
module_param(gpu_direct, charp, 0444);
MODULE_PARM_DESC(gpu_direct,
		 "GPU-direct dma-buf memory regions: auto, on, off");

static enum tbv_gpu_direct_mode tbv_gpu_direct_resolved = TBV_GPU_DIRECT_OFF;

enum tbv_gpu_direct_mode tbv_gpu_direct_mode(void)
{
	return tbv_gpu_direct_resolved;
}

static int tbv_gpu_direct_parse(const char *value,
				enum tbv_gpu_direct_mode *out)
{
	if (!value || !strcmp(value, "auto")) {
		*out = TBV_GPU_DIRECT_AUTO;
		return 0;
	}
	if (!strcmp(value, "on")) {
		*out = TBV_GPU_DIRECT_ON;
		return 0;
	}
	if (!strcmp(value, "off")) {
		*out = TBV_GPU_DIRECT_OFF;
		return 0;
	}
	return -EINVAL;
}
#else
enum tbv_gpu_direct_mode tbv_gpu_direct_mode(void)
{
	return TBV_GPU_DIRECT_OFF;
}
#endif /* CONFIG_TBV_GPU_DIRECT */

static struct tbv_state tbv_driver_state;

bool tbv_debug_surfaces_enabled(void)
{
	if (!TBV_DEBUG_SURFACES_COMPILED)
		return false;

	return !production_mode;
}

static int tbv_parse_peer_allowlist(struct tbv_state *state, const char *allowlist)
{
	char *dup;
	char *cursor;
	char *token;

	state->peer_allowlist_enabled = false;
	state->peer_allowlist_count = 0;
	if (!allowlist || !*allowlist)
		return 0;

	dup = kstrdup(allowlist, GFP_KERNEL);
	if (!dup)
		return -ENOMEM;

	cursor = dup;
	while ((token = strsep(&cursor, ",")) != NULL) {
		uuid_t parsed;
		u32 i;
		bool duplicate = false;

		strim(token);
		if (!*token)
			continue;
		if (uuid_parse(token, &parsed)) {
			pr_err("peer_allowlist contains an invalid UUID entry\n");
			kfree(dup);
			return -EINVAL;
		}
		for (i = 0; i < state->peer_allowlist_count; i++) {
			if (uuid_equal(&state->peer_allowlist[i], &parsed)) {
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			continue;
		if (state->peer_allowlist_count >= TBV_PEER_ALLOWLIST_MAX) {
			pr_err("peer_allowlist supports at most %u UUIDs\n",
			       TBV_PEER_ALLOWLIST_MAX);
			kfree(dup);
			return -E2BIG;
		}
		state->peer_allowlist[state->peer_allowlist_count++] = parsed;
	}

	kfree(dup);
	state->peer_allowlist_enabled = state->peer_allowlist_count > 0;
	return 0;
}

static int tbv_parse_peer_auth_acl(struct tbv_state *state, const char *acl)
{
	char *dup;
	char *cursor;
	char *token;

	state->peer_auth_acl_enabled = false;
	state->peer_auth_acl_count = 0;
	if (!acl || !*acl)
		return 0;

	dup = kstrdup(acl, GFP_KERNEL);
	if (!dup)
		return -ENOMEM;

	cursor = dup;
	while ((token = strsep(&cursor, ",")) != NULL) {
		siphash_key_t key;
		char *hex;
		uuid_t parsed;
		u32 i;
		bool duplicate = false;

		strim(token);
		if (!*token)
			continue;

		hex = strchr(token, '=');
		if (!hex) {
			pr_err("peer_auth_acl entries must use uuid=32hexpsk\n");
			memzero_explicit(&key, sizeof(key));
			kfree(dup);
			return -EINVAL;
		}

		*hex++ = '\0';
		strim(token);
		strim(hex);
		if (!*token || !*hex ||
		    strlen(hex) != TBV_PEER_AUTH_PSK_HEX_LEN) {
			pr_err("peer_auth_acl entries must use uuid=32hexpsk\n");
			memzero_explicit(&key, sizeof(key));
			kfree(dup);
			return -EINVAL;
		}
		if (uuid_parse(token, &parsed)) {
			pr_err("peer_auth_acl contains an invalid UUID entry\n");
			memzero_explicit(&key, sizeof(key));
			kfree(dup);
			return -EINVAL;
		}
		if (hex2bin((u8 *)key.key, hex, sizeof(key.key))) {
			pr_err("peer_auth_acl contains an invalid PSK entry\n");
			memzero_explicit(&key, sizeof(key));
			kfree(dup);
			return -EINVAL;
		}

		for (i = 0; i < state->peer_auth_acl_count; i++) {
			if (uuid_equal(&state->peer_auth_acl_uuid[i], &parsed)) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) {
			memzero_explicit(&key, sizeof(key));
			continue;
		}
		if (state->peer_auth_acl_count >= TBV_PEER_ALLOWLIST_MAX) {
			pr_err("peer_auth_acl supports at most %u peers\n",
			       TBV_PEER_ALLOWLIST_MAX);
			memzero_explicit(&key, sizeof(key));
			kfree(dup);
			return -E2BIG;
		}

		state->peer_auth_acl_uuid[state->peer_auth_acl_count] = parsed;
		state->peer_auth_acl_psk[state->peer_auth_acl_count++] = key;
		memzero_explicit(&key, sizeof(key));
	}

	kfree(dup);
	state->peer_auth_acl_enabled = state->peer_auth_acl_count > 0;
	return 0;
}

static int __init tbv_init(void)
{
	char lanes_desc[32];
	struct tbv_resolved_config resolved;
	struct tbv_tbnet_identity_config identity_cfg;
	struct tbv_service_config service_cfg;
	struct tbv_config cfg;
	bool apple_data;
	int ret;

	ret = tbv_config_parse(&cfg, compat, profile, tbnet,
			       tbnet_identity, lanes);
	if (ret)
		return ret;

#ifdef CONFIG_TBV_GPU_DIRECT
	ret = tbv_gpu_direct_parse(gpu_direct, &tbv_gpu_direct_resolved);
	if (ret) {
		pr_err("gpu_direct must be one of auto, on, off\n");
		return ret;
	}
#endif

	ret = tbv_config_resolve(&resolved, &cfg);
	if (ret)
		return ret;

	/*
	 * Resolve apple_data: when the caller did not override it (-1), derive
	 * the value from the profile so that profile=mac_compat and
	 * profile=mixed enable Apple data paths automatically.
	 */
	apple_data = (apple_data_param >= 0) ? (apple_data_param != 0)
					     : resolved.apple_enabled;

	ret = tbv_tbnet_identity_check_config(&resolved);
	if (ret)
		return ret;

	tbv_path_init_optional_symbols();

	if (start_rings && !allocate_rings) {
		pr_err("start_rings=1 requires allocate_rings=1\n");
		ret = -EINVAL;
		goto err_path_symbols;
	}

	if (negotiate_native && !start_rings) {
		pr_err("negotiate_native=1 requires start_rings=1\n");
		ret = -EINVAL;
		goto err_path_symbols;
	}

	if (enable_tunnels && !start_rings) {
		pr_err("enable_tunnels=1 requires start_rings=1\n");
		ret = -EINVAL;
		goto err_path_symbols;
	}

	if (enable_tunnels && resolved.native_enabled && !negotiate_native) {
		pr_err("enable_tunnels=1 requires negotiate_native=1 when native Linux transport is enabled\n");
		ret = -EINVAL;
		goto err_path_symbols;
	}

	identity_cfg.tbnet_netdev = tbnet_identity_tbnet;
	identity_cfg.gid_netdev = tbnet_identity_gid;
	identity_cfg.minimal_e2e = tbnet_identity_minimal_e2e;
	identity_cfg.minimal_apple_only = tbnet_identity_minimal_apple_only;

	ret = tbv_core_init(&tbv_driver_state, &resolved, &identity_cfg);
	if (ret)
		goto err_path_symbols;
	tbv_driver_state.native_fragment_striping = native_fragment_striping;
	tbv_driver_state.native_data = native_data;
	tbv_driver_state.apple_data = apple_data;
	ret = tbv_parse_peer_allowlist(&tbv_driver_state, peer_allowlist);
	if (ret) {
		tbv_core_exit(&tbv_driver_state);
		goto err_path_symbols;
	}
	ret = tbv_parse_peer_auth_acl(&tbv_driver_state, peer_auth_acl);
	if (ret) {
		tbv_core_exit(&tbv_driver_state);
		goto err_path_symbols;
	}
	if (resolved.native_enabled && !tbv_driver_state.peer_auth_acl_enabled)
		pr_warn("native peer authentication requires peer_auth_acl entries; unlisted peers will be rejected\n");

	service_cfg.native_prtcstns = native_prtcstns;
	service_cfg.apple_prtcstns = apple_prtcstns;
	service_cfg.allocate_rings = allocate_rings;
	service_cfg.start_rings = start_rings;
	service_cfg.negotiate_native = negotiate_native;
	service_cfg.enable_tunnels = enable_tunnels;

	ret = tbv_services_start(&tbv_driver_state, bind_services,
				 &service_cfg);
	if (ret) {
		tbv_core_exit(&tbv_driver_state);
		goto err_path_symbols;
	}

	ret = tbv_ibdev_start(&tbv_driver_state, register_verbs);
	if (ret) {
		tbv_services_stop(&tbv_driver_state);
		tbv_core_exit(&tbv_driver_state);
		goto err_path_symbols;
	}

	if (cfg.lanes_auto)
		strscpy(lanes_desc, "auto", sizeof(lanes_desc));
	else if (cfg.lanes_min == cfg.lanes_max)
		snprintf(lanes_desc, sizeof(lanes_desc), "%u",
			 cfg.lanes_min);
	else
		snprintf(lanes_desc, sizeof(lanes_desc), "%u-%u",
			 cfg.lanes_min, cfg.lanes_max);

	pr_info("loaded compat=%s profile=%s resolved_profile=%s tbnet=%s tbnet_identity=%s tbnet_identity_minimal_e2e=%u tbnet_identity_minimal_apple_only=%u lanes=%s native_control=%s native_data=%u apple_data=%u native_fragment_striping=%u peer_allowlist=%u peer_auth_acl=%u\n",
		tbv_compat_name(cfg.compat),
		tbv_profile_name(cfg.profile),
		tbv_profile_name(resolved.profile),
		tbv_tbnet_policy_name(cfg.tbnet),
		tbv_tbnet_identity_name(resolved.tbnet_identity),
		tbnet_identity_minimal_e2e,
		tbnet_identity_minimal_apple_only,
		lanes_desc,
		tbv_native_control_mode_name(&tbv_driver_state),
		native_data,
		apple_data,
		native_fragment_striping,
		tbv_driver_state.peer_allowlist_count,
		tbv_driver_state.peer_auth_acl_count);

	return 0;

err_path_symbols:
	tbv_path_exit_optional_symbols();
	return ret;
}

static void __exit tbv_exit(void)
{
	tbv_ibdev_stop(&tbv_driver_state);
	tbv_services_stop(&tbv_driver_state);
	tbv_core_exit(&tbv_driver_state);
	tbv_path_exit_optional_symbols();
	pr_info("unloaded\n");
}

module_init(tbv_init);
module_exit(tbv_exit);

MODULE_AUTHOR("thunderbolt-ibverbs contributors");
MODULE_DESCRIPTION("Thunderbolt/USB4 host-to-host RDMA verbs");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: configfs ib_core ib_uverbs");
