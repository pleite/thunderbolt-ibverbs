// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "thunderbolt_ibverbs: apple: " fmt

#include <linux/errno.h>
#include <linux/printk.h>

#include "transport.h"

const struct tbv_backend_ops tbv_apple_backend_ops = {
	.type = TBV_BACKEND_APPLE,
	.name = "apple-fa57",
	.supports_rc = false,
	.supports_uc = true,
	.needs_tbnet_identity = true,
};

static int tbv_apple_validate_config(const struct tbv_cfg_link *link)
{
	if (link->backend != TBV_CFG_BACKEND_APPLE)
		return -EINVAL;
	if (!link->app_selection.valid)
		return -EINVAL;

	/*
	 * app_selection has already been validated by tbv_cfg_link_seal(),
	 * which calls tbv_id_validate_app_compat() and requires a RoCE GID
	 * (V1 or V2) whose address matches the ThunderboltIP IPv4 assignment.
	 * No further constraint is imposed here: unlike the native Linux backend
	 * (which enforces RoCE V2), the Apple path accepts V1 or V2 since
	 * older macOS releases may advertise a V1 GID on the IOEthernetInterface
	 * created by AppleThunderboltIP.
	 */
	return 0;
}

/*
 * tbv_apple_activate / tbv_apple_deactivate — configfs link lifecycle hooks.
 *
 * These are intentionally thin.  The heavy lifting — FA57 DMA tunnel
 * programming, Thunderbolt path negotiation, and verbs device registration —
 * happens in service.c via tbv_service_publish_apple_rail().  The configfs
 * activate/deactivate pair only needs to record that the link is live so that
 * tbv_link_activate_config() can add it to the configured_links list used by
 * the ibdev layer for GID selection.
 */
static int tbv_apple_activate(struct tbv_state *state,
			      const struct tbv_cfg_link *link)
{
	pr_info("activate link=%u dev=%u gid=%u\n",
		link->link_id, link->app_selection.device_id,
		link->app_selection.gid_index);
	return 0;
}

static void tbv_apple_deactivate(struct tbv_state *state,
				 const struct tbv_cfg_link *link)
{
	pr_info("deactivate link=%u\n", link->link_id);
}

const struct tbv_transport_ops tbv_apple_transport_ops = {
	.type = TBV_BACKEND_APPLE,
	.name = "apple-fa57",
	.validate_config = tbv_apple_validate_config,
	.activate = tbv_apple_activate,
	.deactivate = tbv_apple_deactivate,
};
