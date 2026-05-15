// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "thunderbolt_ibverbs: " fmt

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/thunderbolt.h>

#include "../proto/native_wire.h"
#include "tbv.h"

static struct tb_protocol_handler tbv_native_handler;
static bool tbv_native_handler_registered;

static u32 tbv_native_control_caps(const struct tbv_state *state,
				   const struct tbv_peer *peer)
{
	u32 caps = 0;

	if (state->cfg.uc_supported)
		caps |= TBV_NATIVE_WIRE_CAP_UC;
	if (state->cfg.rc_supported)
		caps |= TBV_NATIVE_WIRE_CAP_RC;
	if (peer->nr_rails > 1)
		caps |= TBV_NATIVE_WIRE_CAP_MULTI_RAIL;

	return caps;
}

static u32 tbv_native_control_path_flags(const struct tbv_path *path)
{
	u32 flags = 0;

	if (path->cfg.tx_flags & RING_FLAG_FRAME)
		flags |= TBV_NATIVE_WIRE_PATH_FRAME;
	if (path->cfg.e2e)
		flags |= TBV_NATIVE_WIRE_PATH_E2E;

	return flags;
}

static int tbv_native_control_snapshot(struct tbv_state *state,
				       const struct tbv_native_wire_info *info,
				       struct tbv_native_wire_hello *hello,
				       struct tb_xdomain **xd)
{
	struct tbv_peer *peer;
	int ret = -ENOENT;

	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		struct tbv_rail *rail;

		if (peer->backend != TBV_BACKEND_NATIVE)
			continue;

		list_for_each_entry(rail, &peer->rails, node) {
			if (rail->key.route != info->route)
				continue;

			hello->capabilities =
				tbv_native_control_caps(state, peer);
			hello->rail_id = rail->rail_id;
			hello->route = rail->key.route;
			hello->tx_hop = rail->path.tx_ring ?
					rail->path.tx_ring->hop : U32_MAX;
			hello->rx_hop = rail->path.rx_ring ?
					rail->path.rx_ring->hop : U32_MAX;
			hello->transmit_path =
				rail->path.local_transmit_path >= 0 ?
				rail->path.local_transmit_path : U32_MAX;
			hello->tx_ring_size = rail->path.cfg.tx_ring_size;
			hello->rx_ring_size = rail->path.cfg.rx_ring_size;
			hello->path_flags =
				tbv_native_control_path_flags(&rail->path);
			*xd = tb_xdomain_get(peer->xd);
			ret = 0;
			goto out;
		}
	}

out:
	mutex_unlock(&state->lock);
	return ret;
}

static int tbv_native_control_handle(const void *buf, size_t size, void *data)
{
	struct tbv_state *state = data;
	struct tbv_native_wire_hello remote;
	struct tbv_native_wire_hello local;
	struct tbv_native_wire_info info;
	u8 reply[TBV_NATIVE_WIRE_HELLO_MSG_SIZE];
	struct tb_xdomain *xd = NULL;
	int ret;

	ret = tbv_native_wire_parse_hello(buf, size, &remote, &info);
	if (ret)
		return 0;

	if (info.op != TBV_NATIVE_WIRE_OP_HELLO)
		return 1;

	memset(&local, 0, sizeof(local));
	ret = tbv_native_control_snapshot(state, &info, &local, &xd);
	if (ret) {
		pr_warn("native HELLO route=0x%llx has no matching peer\n",
			info.route);
		return 1;
	}

	ret = tbv_native_wire_build_hello(reply, sizeof(reply), &local,
					  TBV_NATIVE_WIRE_OP_HELLO_ACK,
					  0, info.seq, local.route,
					  info.xdomain_sequence);
	if (!ret)
		ret = tb_xdomain_response(xd, reply, sizeof(reply),
					  TB_CFG_PKG_XDOMAIN_RESP);

	if (ret < 0)
		pr_warn("native HELLO_ACK route=0x%llx failed: %d\n",
			info.route, ret);
	else
		pr_info("native HELLO_ACK route=0x%llx rail=0x%x tx_hop=%u rx_hop=%u out_hop=%u\n",
			info.route, local.rail_id, local.tx_hop,
			local.rx_hop, local.transmit_path);

	tb_xdomain_put(xd);
	return 1;
}

int tbv_native_control_start(struct tbv_state *state)
{
	int ret;

	if (!state->cfg.native_enabled)
		return 0;

	memset(&tbv_native_handler, 0, sizeof(tbv_native_handler));
	tbv_native_handler.uuid = &tbv_native_service_uuid;
	tbv_native_handler.callback = tbv_native_control_handle;
	tbv_native_handler.data = state;

	ret = tb_register_protocol_handler(&tbv_native_handler);
	if (ret)
		return ret;

	tbv_native_handler_registered = true;
	return 0;
}

void tbv_native_control_stop(void)
{
	if (!tbv_native_handler_registered)
		return;

	tb_unregister_protocol_handler(&tbv_native_handler);
	tbv_native_handler_registered = false;
}
