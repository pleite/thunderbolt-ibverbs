// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "thunderbolt_ibverbs: " fmt

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/thunderbolt.h>
#include <linux/workqueue.h>

#include "../proto/native_wire.h"
#include "tbv.h"

#define TBV_NATIVE_HELLO_RETRIES 5
#define TBV_NATIVE_TUNNEL_RETRIES 20
#define TBV_NATIVE_READY_RETRIES 10
#define TBV_NATIVE_HELLO_TIMEOUT_MS 1000
#define TBV_NATIVE_HELLO_RETRY_DELAY_MS 200
#define TBV_NATIVE_HELLO_INITIAL_DELAY_MS 100
#define TBV_NATIVE_WIRE_AUTH_REQUIRED BIT(0)

static void tbv_native_control_work(struct work_struct *work);

struct tbv_native_auth_material {
	/*
	 * Canonical SipHash input for native control-plane authentication.
	 * Both peers hash the same op + nonce/session/UUID tuple to derive the
	 * per-peer session ID and the HELLO/READY authentication tags.
	 */
	u8 op;
	u8 reserved[7];
	u64 initiator_nonce;
	u64 responder_nonce;
	u64 session_id;
	uuid_t local_uuid;
	uuid_t remote_uuid;
};

static const siphash_key_t *tbv_native_control_auth_key(
	const struct tbv_peer *peer)
{
	if (!peer || !peer->auth_acl_configured || !peer->state ||
	    peer->auth_acl_index >= peer->state->peer_auth_acl_count)
		return NULL;

	return &peer->state->peer_auth_acl_psk[peer->auth_acl_index];
}

static bool tbv_native_control_auth_material_valid(const struct tbv_peer *peer)
{
	return peer && peer->xd && peer->xd->local_uuid && peer->xd->remote_uuid &&
	       tbv_native_control_auth_key(peer);
}

static u64 tbv_native_control_auth_digest(const struct tbv_peer *peer, u8 op,
					  u64 initiator_nonce,
					  u64 responder_nonce,
					  u64 session_id)
{
	struct tbv_native_auth_material material;
	const siphash_key_t *key = tbv_native_control_auth_key(peer);

	if (!tbv_native_control_auth_material_valid(peer) || !key)
		return 0;

	memset(&material, 0, sizeof(material));
	material.op = op;
	material.initiator_nonce = initiator_nonce;
	material.responder_nonce = responder_nonce;
	material.session_id = session_id;
	uuid_copy(&material.local_uuid, peer->xd->local_uuid);
	uuid_copy(&material.remote_uuid, peer->xd->remote_uuid);
	return siphash(&material, sizeof(material), key);
}

static u64 tbv_native_control_session_id(const struct tbv_peer *peer,
					 u64 initiator_nonce,
					 u64 responder_nonce)
{
	return tbv_native_control_auth_digest(peer, 0, initiator_nonce,
					      responder_nonce, 0);
}

static int tbv_native_control_prepare_initiator_hello(struct tbv_state *state,
						      struct tbv_peer *peer,
						      struct tbv_native_wire_hello *hello)
{
	if (!tbv_peer_auth_is_initiator(peer) ||
	    !tbv_native_control_auth_material_valid(peer))
		return -EACCES;

	mutex_lock(&state->lock);
	if (!peer->auth_local_nonce_valid) {
		peer->auth_local_nonce = get_random_u64();
		peer->auth_local_nonce_valid = true;
		peer->auth_remote_nonce = 0;
		peer->auth_session_id = 0;
		peer->auth_challenge_valid = false;
		peer->auth_ack_verified = false;
		peer->auth_authenticated = false;
	}
	hello->auth_flags = TBV_NATIVE_WIRE_AUTH_REQUIRED;
	hello->nonce = peer->auth_local_nonce;
	mutex_unlock(&state->lock);
	return 0;
}

static int tbv_native_control_accept_responder_hello(
	struct tbv_state *state, struct tbv_peer *peer,
	const struct tbv_native_wire_hello *remote,
	struct tbv_native_wire_hello *local)
{
	u64 local_nonce;
	u64 session_id;

	if (tbv_peer_auth_is_initiator(peer) ||
	    !tbv_native_control_auth_material_valid(peer) ||
	    !(remote->auth_flags & TBV_NATIVE_WIRE_AUTH_REQUIRED) ||
	    !remote->nonce)
		return -EACCES;

	mutex_lock(&state->lock);
	if (!peer->auth_challenge_valid || peer->auth_remote_nonce != remote->nonce) {
		local_nonce = get_random_u64();
		peer->auth_remote_nonce = remote->nonce;
		peer->auth_local_nonce = local_nonce;
		peer->auth_local_nonce_valid = true;
		peer->auth_session_id = tbv_native_control_session_id(
			peer, remote->nonce, local_nonce);
		peer->auth_challenge_valid = peer->auth_session_id != 0;
		peer->auth_ack_verified = false;
		peer->auth_authenticated = false;
	}
	local->auth_flags = TBV_NATIVE_WIRE_AUTH_REQUIRED;
	local->nonce = peer->auth_local_nonce;
	local->session_id = peer->auth_session_id;
	local->auth_tag = tbv_native_control_auth_digest(
		peer, TBV_NATIVE_WIRE_OP_HELLO_ACK, peer->auth_remote_nonce,
		peer->auth_local_nonce, peer->auth_session_id);
	session_id = peer->auth_session_id;
	mutex_unlock(&state->lock);

	return session_id && local->auth_tag ? 0 : -EACCES;
}

static int tbv_native_control_verify_hello_ack(struct tbv_state *state,
					       struct tbv_peer *peer,
					       const struct tbv_native_wire_hello *remote)
{
	u64 session_id;
	u64 auth_tag;

	if (!tbv_peer_auth_is_initiator(peer) ||
	    !tbv_native_control_auth_material_valid(peer) ||
	    !(remote->auth_flags & TBV_NATIVE_WIRE_AUTH_REQUIRED) ||
	    !remote->nonce || !remote->session_id || !remote->auth_tag)
		return -EACCES;

	mutex_lock(&state->lock);
	if (!peer->auth_local_nonce_valid) {
		mutex_unlock(&state->lock);
		return -EACCES;
	}
	session_id = tbv_native_control_session_id(peer, peer->auth_local_nonce,
						  remote->nonce);
	auth_tag = tbv_native_control_auth_digest(
		peer, TBV_NATIVE_WIRE_OP_HELLO_ACK, peer->auth_local_nonce,
		remote->nonce, session_id);
	if (!session_id || remote->session_id != session_id ||
	    remote->auth_tag != auth_tag) {
		mutex_unlock(&state->lock);
		return -EACCES;
	}
	peer->auth_remote_nonce = remote->nonce;
	peer->auth_session_id = session_id;
	peer->auth_challenge_valid = true;
	peer->auth_ack_verified = true;
	peer->auth_authenticated = false;
	mutex_unlock(&state->lock);
	return 0;
}

static int tbv_native_control_prepare_initiator_ready(struct tbv_state *state,
						      struct tbv_peer *peer,
						      struct tbv_native_wire_hello *hello)
{
	int ret = -EACCES;

	if (!tbv_peer_auth_is_initiator(peer) ||
	    !tbv_native_control_auth_material_valid(peer))
		return -EACCES;

	mutex_lock(&state->lock);
	if (peer->auth_challenge_valid && peer->auth_ack_verified &&
	    peer->auth_local_nonce_valid && peer->auth_remote_nonce &&
	    peer->auth_session_id) {
		hello->auth_flags = TBV_NATIVE_WIRE_AUTH_REQUIRED;
		hello->nonce = peer->auth_local_nonce;
		hello->session_id = peer->auth_session_id;
		hello->auth_tag = tbv_native_control_auth_digest(
			peer, TBV_NATIVE_WIRE_OP_READY, peer->auth_local_nonce,
			peer->auth_remote_nonce, peer->auth_session_id);
		ret = hello->auth_tag ? 0 : -EACCES;
	}
	mutex_unlock(&state->lock);
	return ret;
}

static int tbv_native_control_accept_responder_ready(
	struct tbv_state *state, struct tbv_peer *peer,
	const struct tbv_native_wire_hello *remote,
	struct tbv_native_wire_hello *local)
{
	u64 auth_tag;

	if (tbv_peer_auth_is_initiator(peer) ||
	    !tbv_native_control_auth_material_valid(peer) ||
	    !(remote->auth_flags & TBV_NATIVE_WIRE_AUTH_REQUIRED) ||
	    !remote->nonce || !remote->session_id || !remote->auth_tag)
		return -EACCES;

	mutex_lock(&state->lock);
	auth_tag = tbv_native_control_auth_digest(
		peer, TBV_NATIVE_WIRE_OP_READY, peer->auth_remote_nonce,
		peer->auth_local_nonce, peer->auth_session_id);
	if (!peer->auth_challenge_valid || remote->nonce != peer->auth_remote_nonce ||
	    remote->session_id != peer->auth_session_id ||
	    remote->auth_tag != auth_tag) {
		mutex_unlock(&state->lock);
		return -EACCES;
	}
	peer->auth_authenticated = true;
	local->auth_flags = TBV_NATIVE_WIRE_AUTH_REQUIRED;
	local->nonce = peer->auth_local_nonce;
	local->session_id = peer->auth_session_id;
	local->auth_tag = tbv_native_control_auth_digest(
		peer, TBV_NATIVE_WIRE_OP_READY_ACK, peer->auth_remote_nonce,
		peer->auth_local_nonce, peer->auth_session_id);
	mutex_unlock(&state->lock);

	return local->auth_tag ? 0 : -EACCES;
}

static int tbv_native_control_verify_ready_ack(struct tbv_state *state,
					       struct tbv_peer *peer,
					       const struct tbv_native_wire_hello *remote)
{
	u64 auth_tag;

	if (!tbv_peer_auth_is_initiator(peer) ||
	    !tbv_native_control_auth_material_valid(peer) ||
	    !(remote->auth_flags & TBV_NATIVE_WIRE_AUTH_REQUIRED) ||
	    !remote->nonce || !remote->session_id || !remote->auth_tag)
		return -EACCES;

	mutex_lock(&state->lock);
	auth_tag = tbv_native_control_auth_digest(
		peer, TBV_NATIVE_WIRE_OP_READY_ACK, peer->auth_local_nonce,
		peer->auth_remote_nonce, peer->auth_session_id);
	if (!peer->auth_challenge_valid || !peer->auth_ack_verified ||
	    remote->nonce != peer->auth_remote_nonce ||
	    remote->session_id != peer->auth_session_id ||
	    remote->auth_tag != auth_tag) {
		mutex_unlock(&state->lock);
		return -EACCES;
	}
	peer->auth_authenticated = true;
	mutex_unlock(&state->lock);
	return 0;
}

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

static void tbv_native_control_fill_hello(const struct tbv_state *state,
					  const struct tbv_peer *peer,
					  const struct tbv_rail *rail,
					  struct tbv_native_wire_hello *hello)
{
	memset(hello, 0, sizeof(*hello));
	hello->capabilities = tbv_native_control_caps(state, peer);
	hello->rail_id = rail->rail_id;
	hello->route = rail->key.route;
	hello->tx_hop = rail->path.tx_ring ?
			rail->path.tx_ring->hop : U32_MAX;
	hello->rx_hop = rail->path.rx_ring ?
			rail->path.rx_ring->hop : U32_MAX;
	hello->transmit_path = rail->path.local_transmit_path >= 0 ?
			rail->path.local_transmit_path : U32_MAX;
	hello->tx_ring_size = rail->path.cfg.tx_ring_size;
	hello->rx_ring_size = rail->path.cfg.rx_ring_size;
	hello->path_flags = tbv_native_control_path_flags(&rail->path);
}

static bool tbv_native_control_peer_matches_source(
	const struct tbv_peer *peer, const struct tb_xdomain *source_xd)
{
	return !source_xd || peer->xd == source_xd;
}

static bool tbv_native_control_can_kick_rail(const struct tbv_state *state,
					     const struct tbv_rail *rail)
{
	return rail->native_work_state == state &&
	       !READ_ONCE(rail->native_work_stop) &&
	       rail->path.state != TBV_PATH_STOPPED;
}

static int tbv_native_control_kick_matching_rail(
	struct tbv_state *state, const struct tb_xdomain *source_xd,
	const struct tbv_native_wire_info *info, u32 rail_id)
{
	struct tbv_peer *peer;
	int ret = -ENOENT;

	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		struct tbv_rail *rail;

		if (peer->backend != TBV_BACKEND_NATIVE)
			continue;
		if (!tbv_native_control_peer_matches_source(peer, source_xd))
			continue;

		list_for_each_entry(rail, &peer->rails, node) {
			if (rail->key.route != info->route ||
			    rail->rail_id != rail_id)
				continue;

			if (tbv_native_control_can_kick_rail(state, rail))
				schedule_delayed_work(&rail->native_work, 0);
			ret = 0;
			goto out;
		}
	}

out:
	mutex_unlock(&state->lock);
	return ret;
}

static u8 tbv_native_control_sequence(const struct tbv_rail *rail)
{
	if (rail->path.local_transmit_path >= 0)
		return rail->path.local_transmit_path & 0x3;

	return rail->rail_id & 0x3;
}

void tbv_native_control_init_rail(struct tbv_rail *rail,
				  struct tbv_peer *peer)
{
	rail->peer = peer;
	INIT_DELAYED_WORK(&rail->native_work, tbv_native_control_work);
}

void tbv_native_control_queue_rail(struct tbv_state *state,
				   struct tbv_rail *rail)
{
	rail->native_work_state = state;
	WRITE_ONCE(rail->native_work_stop, false);
	rail->native_attempts = 0;
	rail->native_tunnel_attempts = 0;
	rail->native_ready_attempts = 0;
	rail->native_last_error = 0;
	rail->native_ready_sent = false;
	rail->native_remote_ready = false;
	schedule_delayed_work(&rail->native_work,
			      msecs_to_jiffies(TBV_NATIVE_HELLO_INITIAL_DELAY_MS));
}

void tbv_native_control_cancel_rail(struct tbv_rail *rail)
{
	struct tbv_state *state = rail->native_work_state;

	if (state) {
		mutex_lock(&state->lock);
		WRITE_ONCE(rail->native_work_stop, true);
		mutex_unlock(&state->lock);
	} else {
		WRITE_ONCE(rail->native_work_stop, true);
	}
	cancel_delayed_work_sync(&rail->native_work);
	rail->native_work_state = NULL;
}

static int tbv_native_control_snapshot(struct tbv_state *state,
				       const struct tb_xdomain *source_xd,
				       const struct tbv_native_wire_info *info,
				       u32 rail_id,
				       bool require_matching_rail,
				       bool require_tunnel_enabled,
				       struct tbv_native_wire_hello *hello,
				       struct tb_xdomain **xd,
				       struct tbv_peer **matched_peer)
{
	struct tbv_peer *peer;
	int ret = -ENOENT;

	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		struct tbv_rail *rail;

		if (peer->backend != TBV_BACKEND_NATIVE)
			continue;
		if (!tbv_native_control_peer_matches_source(peer, source_xd))
			continue;

		list_for_each_entry(rail, &peer->rails, node) {
			if (rail->key.route != info->route)
				continue;
			if (require_matching_rail && rail->rail_id != rail_id)
				continue;
			if (require_tunnel_enabled &&
			    rail->path.state != TBV_PATH_TUNNEL_ENABLED)
				continue;

			tbv_native_control_fill_hello(state, peer, rail,
						      hello);
			*xd = tb_xdomain_get(peer->xd);
			if (matched_peer)
				*matched_peer = peer;
			ret = 0;
			goto out;
		}
	}

out:
	mutex_unlock(&state->lock);
	return ret;
}

static int tbv_native_control_apply_remote(struct tbv_state *state,
					   const struct tb_xdomain *source_xd,
					   const struct tbv_native_wire_info *info,
					   const struct tbv_native_wire_hello *remote,
					   bool require_matching_rail)
{
	struct tbv_peer *peer;
	int ret = -ENOENT;

	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		struct tbv_rail *rail;

		if (peer->backend != TBV_BACKEND_NATIVE)
			continue;
		if (!tbv_native_control_peer_matches_source(peer, source_xd))
			continue;

		list_for_each_entry(rail, &peer->rails, node) {
			if (rail->key.route != info->route)
				continue;
			if (require_matching_rail &&
			    rail->rail_id != remote->rail_id)
				continue;

			rail->native_negotiated = true;
			rail->remote_rail_id = remote->rail_id;
			rail->remote_transmit_path = remote->transmit_path;
			rail->remote_tx_hop = remote->tx_hop;
			rail->remote_rx_hop = remote->rx_hop;
			tbv_path_set_remote_rx_capacity(&rail->path,
							remote->rx_ring_size);
			ret = 0;
			goto out;
		}
	}

out:
	mutex_unlock(&state->lock);
	return ret;
}

static int tbv_native_control_apply_ack(struct tbv_state *state,
					const struct tb_xdomain *source_xd,
					const struct tbv_native_wire_info *info,
					const struct tbv_native_wire_hello *remote)
{
	return tbv_native_control_apply_remote(state, source_xd, info, remote,
					       true);
}

static int tbv_native_control_mark_remote_ready(struct tbv_state *state,
						const struct tb_xdomain *source_xd,
						const struct tbv_native_wire_info *info,
						const struct tbv_native_wire_hello *remote)
{
	struct tbv_rail *publish = NULL;
	struct tbv_peer *peer;
	int ret = -ENOENT;

	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		struct tbv_rail *rail;

		if (peer->backend != TBV_BACKEND_NATIVE)
			continue;
		if (!tbv_native_control_peer_matches_source(peer, source_xd))
			continue;

		list_for_each_entry(rail, &peer->rails, node) {
			if (rail->key.route != info->route ||
			    rail->rail_id != remote->rail_id)
				continue;

			rail->native_remote_ready = true;
			ret = 0;
			/*
			 * Take a refcount so we can call tbv_ibdev_rail_event
			 * after dropping state->lock — ib_register_device may
			 * sleep and can reach back into our ops.
			 */
			if (tbv_rail_data_ready(rail) && !rail->removing) {
				refcount_inc(&rail->refcnt);
				publish = rail;
			}
			goto out;
		}
	}

out:
	mutex_unlock(&state->lock);
	if (publish) {
		tbv_ibdev_rail_event(state, publish, true);
		tbv_rail_put(publish);
	}
	return ret;
}

static int tbv_native_control_mark_local_ready_sent(
	struct tbv_state *state, const struct tb_xdomain *source_xd,
	const struct tbv_native_wire_info *info, u32 rail_id)
{
	struct tbv_rail *publish = NULL;
	struct tbv_peer *peer;
	int ret = -ENOENT;

	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		struct tbv_rail *rail;

		if (peer->backend != TBV_BACKEND_NATIVE)
			continue;
		if (!tbv_native_control_peer_matches_source(peer, source_xd))
			continue;

		list_for_each_entry(rail, &peer->rails, node) {
			if (rail->key.route != info->route ||
			    rail->rail_id != rail_id)
				continue;

			rail->native_ready_sent = true;
			rail->native_last_error = 0;
			ret = 0;
			if (tbv_rail_data_ready(rail) && !rail->removing) {
				refcount_inc(&rail->refcnt);
				publish = rail;
			}
			goto out;
		}
	}

out:
	mutex_unlock(&state->lock);
	if (publish) {
		tbv_ibdev_rail_event(state, publish, true);
		tbv_rail_put(publish);
	}
	return ret;
}

static bool tbv_native_control_rail_data_ready(struct tbv_state *state,
					       const struct tbv_rail *rail)
{
	bool ready;

	mutex_lock(&state->lock);
	ready = tbv_rail_data_ready(rail);
	mutex_unlock(&state->lock);
	return ready;
}

int tbv_native_control_handle_packet(struct tbv_state *state,
				     struct tb_xdomain *source_xd,
				     const void *buf, size_t size)
{
	struct tbv_native_wire_hello remote;
	struct tbv_native_wire_hello local;
	struct tbv_native_wire_info info;
	u8 reply[TBV_NATIVE_WIRE_HELLO_MSG_SIZE];
	struct tbv_peer *peer = NULL;
	struct tb_xdomain *xd = NULL;
	int ret;

	if (!state)
		return 0;

	ret = tbv_native_wire_parse_hello(buf, size, &remote, &info);
	if (ret)
		return 0;

	if (info.op == TBV_NATIVE_WIRE_OP_HELLO_ACK) {
		/*
		 * Let tb_xdomain_request() match the ACK after observing it.
		 * XDomain dispatch calls protocol handlers before request
		 * matching, so consuming the ACK here would make the sender
		 * time out even though the peer replied correctly.
		 */
		return 0;
	}

	if (info.op == TBV_NATIVE_WIRE_OP_READY_ACK)
		return 0;

	if (info.op == TBV_NATIVE_WIRE_OP_READY) {
		memset(&local, 0, sizeof(local));
		ret = tbv_native_control_snapshot(state, source_xd, &info,
						  remote.rail_id, true,
						  false,
						  &local, &xd, &peer);
		if (ret) {
			pr_warn_ratelimited("native READY route=0x%llx rail=0x%x has no matching peer rail\n",
					    info.route, remote.rail_id);
			return 1;
		}
		tb_xdomain_put(xd);
		xd = NULL;
		peer = NULL;
		ret = tbv_native_control_snapshot(state, source_xd, &info,
						  remote.rail_id, true,
						  true,
						  &local, &xd, &peer);
		if (ret) {
			tbv_native_control_mark_remote_ready(state, source_xd,
							    &info, &remote);
			tbv_native_control_kick_matching_rail(state, source_xd,
							      &info,
							      remote.rail_id);
			return 1;
		}
		ret = tbv_native_control_accept_responder_ready(state, peer,
							       &remote, &local);
		if (ret) {
			tb_xdomain_put(xd);
			return 1;
		}

		ret = tbv_native_control_mark_remote_ready(state, source_xd,
							  &info, &remote);
		if (!ret)
			pr_info("native READY received route=0x%llx rail=0x%x\n",
				info.route, remote.rail_id);

		ret = tbv_native_wire_build_hello(reply, sizeof(reply),
						  &local,
						  TBV_NATIVE_WIRE_OP_READY_ACK,
						  0, info.seq, local.route,
						  info.xdomain_sequence);
		if (ret >= 0)
			ret = tb_xdomain_response(xd, reply, sizeof(reply),
						  TB_CFG_PKG_XDOMAIN_RESP);

		if (ret < 0)
			pr_warn("native READY_ACK route=0x%llx rail=0x%x failed: %d\n",
				info.route, remote.rail_id, ret);
		else
			tbv_native_control_mark_local_ready_sent(state,
								source_xd, &info,
								local.rail_id);

		tb_xdomain_put(xd);
		tbv_native_control_kick_matching_rail(state, source_xd, &info,
						      remote.rail_id);
		return 1;
	}

	if (info.op != TBV_NATIVE_WIRE_OP_HELLO)
		return 0;

	memset(&local, 0, sizeof(local));
	ret = tbv_native_control_snapshot(state, source_xd, &info,
					  remote.rail_id, true, false, &local,
					  &xd, &peer);
	if (ret) {
		pr_warn("native HELLO route=0x%llx rail=0x%x has no matching peer rail\n",
			info.route, remote.rail_id);
		return 1;
	}
	ret = tbv_native_control_accept_responder_hello(state, peer, &remote,
						       &local);
	if (ret) {
		tb_xdomain_put(xd);
		return 1;
	}

	ret = tbv_native_control_apply_remote(state, source_xd, &info,
					      &remote, true);
	if (!ret)
		pr_info("native HELLO received route=0x%llx rail=0x%x remote_out=%u remote_tx=%u remote_rx=%u\n",
			info.route, remote.rail_id, remote.transmit_path,
			remote.tx_hop, remote.rx_hop);

	ret = tbv_native_wire_build_hello(reply, sizeof(reply), &local,
					  TBV_NATIVE_WIRE_OP_HELLO_ACK,
					  0, info.seq, local.route,
					  info.xdomain_sequence);
	if (ret >= 0)
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
	tbv_native_control_kick_matching_rail(state, source_xd, &info,
					      remote.rail_id);
	return 1;
}

static int tbv_native_control_exchange_once(struct tbv_state *state,
					    struct tbv_peer *peer,
					    struct tbv_rail *rail,
					    u32 attempt)
{
	struct tbv_native_wire_hello local;
	struct tbv_native_wire_hello remote;
	u8 response[TBV_NATIVE_WIRE_HELLO_MSG_SIZE];
	u8 request[TBV_NATIVE_WIRE_HELLO_MSG_SIZE];
	struct tbv_native_wire_info info;
	int ret;

	if (peer->backend != TBV_BACKEND_NATIVE)
		return 0;

	if (rail->path.state != TBV_PATH_RING_STARTED)
		return -EINVAL;

	/*
	 * tb_xdomain_request() matches responses by XDomain route and protocol
	 * UUID only.  It does not include the XDomain sequence or our native
	 * rail/op fields in the match key, so concurrent native requests over
	 * the same XDomain can steal each other's responses.  Keep one native
	 * control transaction in flight per peer/XDomain.
	 */
	mutex_lock(&peer->control_lock);

	tbv_native_control_fill_hello(state, peer, rail, &local);
	ret = tbv_native_control_prepare_initiator_hello(state, peer, &local);
	if (ret)
		goto out_unlock;
	ret = tbv_native_wire_build_hello(request, sizeof(request), &local,
					  TBV_NATIVE_WIRE_OP_HELLO, 0,
					  rail->rail_id, local.route,
					  tbv_native_control_sequence(rail));
	if (ret < 0)
		goto out_unlock;

	memset(response, 0, sizeof(response));
	ret = tb_xdomain_request(peer->xd, request, sizeof(request),
				 TB_CFG_PKG_XDOMAIN_REQ, response,
				 sizeof(response),
				 TB_CFG_PKG_XDOMAIN_RESP,
				 TBV_NATIVE_HELLO_TIMEOUT_MS);
	if (ret)
		goto out_unlock;

	ret = tbv_native_wire_parse_hello(response, sizeof(response), &remote,
					 &info);
	if (ret)
		goto out_unlock;
	if (info.op != TBV_NATIVE_WIRE_OP_HELLO_ACK) {
		ret = -EPROTO;
		goto out_unlock;
	}
	ret = tbv_native_control_verify_hello_ack(state, peer, &remote);
	if (ret)
		goto out_unlock;
	ret = tbv_native_control_apply_ack(state, peer->xd, &info, &remote);
	if (ret)
		goto out_unlock;

	pr_info("native HELLO negotiated route=0x%llx rail=0x%x remote_out=%u remote_tx=%u remote_rx=%u attempt=%u\n",
		info.route, remote.rail_id, remote.transmit_path,
		remote.tx_hop, remote.rx_hop, attempt);
out_unlock:
	mutex_unlock(&peer->control_lock);
	return ret;
}

static int tbv_native_control_ready_once(struct tbv_state *state,
					 struct tbv_peer *peer,
					 struct tbv_rail *rail)
{
	struct tbv_native_wire_hello local;
	struct tbv_native_wire_hello remote;
	u8 response[TBV_NATIVE_WIRE_HELLO_MSG_SIZE];
	u8 request[TBV_NATIVE_WIRE_HELLO_MSG_SIZE];
	struct tbv_native_wire_info info;
	int ret;

	if (peer->backend != TBV_BACKEND_NATIVE)
		return 0;

	if (rail->path.state != TBV_PATH_TUNNEL_ENABLED)
		return -EINVAL;

	/* See tbv_native_control_exchange_once() for why this is serialized. */
	mutex_lock(&peer->control_lock);

	tbv_native_control_fill_hello(state, peer, rail, &local);
	ret = tbv_native_control_prepare_initiator_ready(state, peer, &local);
	if (ret)
		goto out_unlock;
	ret = tbv_native_wire_build_hello(request, sizeof(request), &local,
					  TBV_NATIVE_WIRE_OP_READY, 0,
					  rail->rail_id, local.route,
					  tbv_native_control_sequence(rail));
	if (ret < 0)
		goto out_unlock;

	memset(response, 0, sizeof(response));
	ret = tb_xdomain_request(peer->xd, request, sizeof(request),
				 TB_CFG_PKG_XDOMAIN_REQ, response,
				 sizeof(response),
				 TB_CFG_PKG_XDOMAIN_RESP,
				 TBV_NATIVE_HELLO_TIMEOUT_MS);
	if (ret)
		goto out_unlock;

	ret = tbv_native_wire_parse_hello(response, sizeof(response), &remote,
					 &info);
	if (ret)
		goto out_unlock;
	if (info.op != TBV_NATIVE_WIRE_OP_READY_ACK ||
	    remote.rail_id != rail->rail_id) {
		ret = -EPROTO;
		goto out_unlock;
	}
	ret = tbv_native_control_verify_ready_ack(state, peer, &remote);
	if (ret)
		goto out_unlock;

	ret = tbv_native_control_mark_remote_ready(state, peer->xd, &info,
						  &remote);
	if (ret)
		goto out_unlock;

	pr_info("native READY sent route=0x%llx rail=0x%x\n",
		info.route, rail->rail_id);
out_unlock:
	mutex_unlock(&peer->control_lock);
	return ret;
}

static int tbv_native_control_enable_tunnel_once(struct tbv_peer *peer,
						 struct tbv_rail *rail)
{
	int ret;

	/*
	 * Tunnel activation programs the same XDomain control path as native
	 * HELLO/READY traffic.  Serialize it with those transactions so two
	 * rails on one link do not race Thunderbolt config-space operations.
	 */
	mutex_lock(&peer->control_lock);
	ret = tbv_path_enable_tunnel(&rail->path, peer->xd,
				     rail->remote_transmit_path);
	mutex_unlock(&peer->control_lock);
	return ret;
}

int tbv_native_control_exchange(struct tbv_state *state, struct tbv_peer *peer,
				struct tbv_rail *rail)
{
	u32 attempt;
	int ret = 0;

	for (attempt = 1; attempt <= TBV_NATIVE_HELLO_RETRIES; attempt++) {
		ret = tbv_native_control_exchange_once(state, peer, rail,
						       attempt);
		if (!ret)
			return 0;
		if (ret != -ETIMEDOUT)
			return ret;
		if (attempt < TBV_NATIVE_HELLO_RETRIES)
			msleep(TBV_NATIVE_HELLO_RETRY_DELAY_MS);
	}

	pr_warn("native HELLO route=0x%llx rail=0x%x timed out after %d attempts\n",
		rail->key.route, rail->rail_id, TBV_NATIVE_HELLO_RETRIES);
	return ret;
}

static void tbv_native_control_work(struct work_struct *work)
{
	struct tbv_rail *rail =
		container_of(to_delayed_work(work), struct tbv_rail,
			     native_work);
	struct tbv_state *state = rail->native_work_state;
	struct tbv_peer *peer = rail->peer;
	bool retry = false;
	u32 attempt;
	int ret = 0;

	if (!state || !peer || READ_ONCE(rail->native_work_stop))
		return;

	if (peer->backend != TBV_BACKEND_NATIVE)
		return;

	if (rail->path.state != TBV_PATH_RING_STARTED &&
	    rail->path.state != TBV_PATH_TUNNEL_ENABLED)
		return;

	if (state->negotiate_native && !rail->native_negotiated) {
		if (!tbv_peer_auth_is_initiator(peer))
			goto out;
		attempt = ++rail->native_attempts;
		ret = tbv_native_control_exchange_once(state, peer, rail,
						       attempt);
		rail->native_last_error = ret;
		if (ret) {
			if (attempt < TBV_NATIVE_HELLO_RETRIES) {
				retry = true;
			} else {
				pr_warn("native HELLO route=0x%llx rail=0x%x failed after %u attempts: %d\n",
					rail->key.route, rail->rail_id,
					attempt, ret);
			}
			goto out;
		}
	}

	if (state->enable_tunnels &&
	    rail->path.state == TBV_PATH_RING_STARTED) {
		if (!rail->native_negotiated ||
		    rail->remote_transmit_path < 0) {
			ret = -ENOTCONN;
			rail->native_last_error = ret;
			goto out;
		}

		attempt = ++rail->native_tunnel_attempts;
		ret = tbv_native_control_enable_tunnel_once(peer, rail);
		rail->native_last_error = ret;
		if (ret) {
			if (attempt < TBV_NATIVE_TUNNEL_RETRIES) {
				retry = true;
			} else {
				pr_warn("native tunnel route=0x%llx rail=0x%x enable failed after %u attempts: %d\n",
					rail->key.route, rail->rail_id,
					attempt, ret);
			}
			goto out;
		}

		pr_info("enabled tunnel route=0x%llx rail=0x%x out_hop=%d remote_out_hop=%d tx_hop=%d rx_hop=%d\n",
			rail->key.route, rail->rail_id,
			rail->path.local_transmit_path,
			rail->remote_transmit_path,
			rail->path.tx_ring->hop, rail->path.rx_ring->hop);

		/*
		 * Tunnel-enable is the third edge that can complete data-readiness
		 * (alongside local READY sent + remote READY received). If both
		 * READYs already arrived during HELLO retries, neither
		 * mark_local_ready_sent nor mark_remote_ready will fire again —
		 * publish synchronously here so the rail's ib_device appears.
		 */
		mutex_lock(&state->lock);
		if (tbv_rail_data_ready(rail) && !rail->removing) {
			refcount_inc(&rail->refcnt);
			mutex_unlock(&state->lock);
			tbv_ibdev_rail_event(state, rail, true);
			tbv_rail_put(rail);
		} else {
			mutex_unlock(&state->lock);
		}
	}

	if (state->enable_tunnels &&
	    rail->path.state == TBV_PATH_TUNNEL_ENABLED &&
	    !rail->native_ready_sent) {
		if (!tbv_peer_auth_is_initiator(peer))
			goto out;
		attempt = ++rail->native_ready_attempts;
		ret = tbv_native_control_ready_once(state, peer, rail);
		if (ret && tbv_native_control_rail_data_ready(state, rail))
			ret = 0;
		rail->native_last_error = ret;
		if (ret) {
			if (attempt < TBV_NATIVE_READY_RETRIES) {
				retry = true;
			} else {
				pr_warn("native READY route=0x%llx rail=0x%x failed after %u attempts: %d\n",
					rail->key.route, rail->rail_id,
					attempt, ret);
			}
			goto out;
		}
		rail->native_ready_sent = true;

		/*
		 * We initiated the READY exchange: ready_once already marked the
		 * remote ready (its READY_ACK carried their state), but that hook
		 * fired before we set native_ready_sent above, so it saw
		 * data_ready=false. Re-check now that all three edges
		 * (tunnel_enabled, ready_sent, remote_ready) are in place and
		 * publish if appropriate. The wire-handler path goes through
		 * mark_local_ready_sent which has the publish baked in; this is
		 * the work-function equivalent.
		 */
		mutex_lock(&state->lock);
		if (tbv_rail_data_ready(rail) && !rail->removing) {
			refcount_inc(&rail->refcnt);
			mutex_unlock(&state->lock);
			tbv_ibdev_rail_event(state, rail, true);
			tbv_rail_put(rail);
		} else {
			mutex_unlock(&state->lock);
		}
	}

out:
	if (retry) {
		mutex_lock(&state->lock);
		if (!READ_ONCE(rail->native_work_stop))
			schedule_delayed_work(&rail->native_work,
					      msecs_to_jiffies(TBV_NATIVE_HELLO_RETRY_DELAY_MS));
		mutex_unlock(&state->lock);
	}
}

int tbv_native_control_start(struct tbv_state *state)
{
	int ret;

	if (!state->cfg.native_enabled)
		return 0;

#ifdef TB_PROTOCOL_HANDLER_HAS_XDOMAIN
	state->native_control_source_aware = true;
	ret = tbv_native_control_xdomain_start(state);
#else
	state->native_control_source_aware = false;
	ret = tbv_native_control_legacy_start(state);
#endif
	if (!ret)
		state->native_control_registered = true;
	return ret;
}

void tbv_native_control_stop(struct tbv_state *state)
{
#ifdef TB_PROTOCOL_HANDLER_HAS_XDOMAIN
	tbv_native_control_xdomain_stop();
#else
	tbv_native_control_legacy_stop();
#endif
	if (state) {
		state->native_control_registered = false;
		state->native_control_source_aware = false;
	}
}

const char *tbv_native_control_mode_name(const struct tbv_state *state)
{
	if (!state || !state->cfg.native_enabled ||
	    !state->native_control_registered)
		return "off";

	return state->native_control_source_aware ? "source_aware" : "legacy";
}
