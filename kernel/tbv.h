/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TBV_H
#define TBV_H

#include <linux/types.h>

#define TBV_DRV_NAME "thunderbolt_ibverbs"

enum tbv_compat_mode {
	TBV_COMPAT_AUTO,
	TBV_COMPAT_FORCE,
	TBV_COMPAT_OFF,
};

enum tbv_profile {
	TBV_PROFILE_AUTO,
	TBV_PROFILE_MAC_COMPAT,
	TBV_PROFILE_LINUX_PERF,
	TBV_PROFILE_MIXED,
};

enum tbv_tbnet_policy {
	TBV_TBNET_AUTO,
	TBV_TBNET_ALLOW,
	TBV_TBNET_PREFER_RDMA,
	TBV_TBNET_BLOCK,
};

enum tbv_tbnet_identity_mode {
	TBV_TBNET_ID_AUTO,
	TBV_TBNET_ID_STOCK,
	TBV_TBNET_ID_STOCK_PROXY,
	TBV_TBNET_ID_MINIMAL_PACKET,
	TBV_TBNET_ID_OFF,
};

struct tbv_config {
	enum tbv_compat_mode compat;
	enum tbv_profile profile;
	enum tbv_tbnet_policy tbnet;
	enum tbv_tbnet_identity_mode tbnet_identity;
	u32 lanes_min;
	u32 lanes_max;
	bool lanes_auto;
};

int tbv_config_parse(struct tbv_config *cfg, const char *compat,
		     const char *profile, const char *tbnet,
		     const char *tbnet_identity, const char *lanes);

const char *tbv_compat_name(enum tbv_compat_mode mode);
const char *tbv_profile_name(enum tbv_profile profile);
const char *tbv_tbnet_policy_name(enum tbv_tbnet_policy policy);
const char *tbv_tbnet_identity_name(enum tbv_tbnet_identity_mode mode);

int tbv_tbnet_identity_check_config(const struct tbv_config *cfg);

#endif
