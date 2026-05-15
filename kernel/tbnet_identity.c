// SPDX-License-Identifier: GPL-2.0
/*
 * Apple-compatible TBnet identity policy.
 *
 * This file will eventually own the minimal ThunderboltIP identity backend.
 * For now it validates profile combinations so the public module starts with
 * explicit behavior instead of hidden Mac-specific assumptions.
 */

#define pr_fmt(fmt) "thunderbolt_ibverbs: " fmt

#include <linux/errno.h>
#include <linux/kernel.h>

#include "tbv.h"

int tbv_tbnet_identity_check_config(const struct tbv_config *cfg)
{
	if (cfg->profile == TBV_PROFILE_MAC_COMPAT &&
	    cfg->tbnet_identity == TBV_TBNET_ID_OFF) {
		pr_err("profile=mac_compat requires TBnet identity\n");
		return -EINVAL;
	}

	if (cfg->tbnet == TBV_TBNET_BLOCK &&
	    (cfg->tbnet_identity == TBV_TBNET_ID_STOCK ||
	     cfg->tbnet_identity == TBV_TBNET_ID_STOCK_PROXY)) {
		pr_err("tbnet=block conflicts with tbnet_identity=%s\n",
		       tbv_tbnet_identity_name(cfg->tbnet_identity));
		return -EINVAL;
	}

	if (cfg->profile == TBV_PROFILE_LINUX_PERF &&
	    cfg->tbnet_identity != TBV_TBNET_ID_AUTO &&
	    cfg->tbnet_identity != TBV_TBNET_ID_OFF) {
		pr_warn("linux_perf ignores Apple TBnet identity unless an Apple peer is selected\n");
	}

	return 0;
}
