// SPDX-License-Identifier: GPL-2.0

#include "tbv.h"
#include "ibdev_internal.h"
#include "ibdev_split.h"

void tbv_ibdev_rx_apple_frame(struct tbv_state *state,
			      const struct tbv_path *path,
			      const void *payload, u32 len, u8 sof, u8 eof)
{
	struct tbv_apple_pending_rx *pending;
	struct tbv_qp *tqp;
	bool starts_message;
	bool raw_rx;
	u32 qpn;
	u32 user_len = 0;
	int ret;

	if (!state || !state->verbs_registered)
		return;
	if (!payload || !len || len > TBV_APPLE_FRAME_SIZE) {
		atomic64_inc(&state->data_rx_bad_frame);
		return;
	}
	raw_rx = tbv_path_apple_rx_raw_mode();

	qpn = tbv_apple_qpn_from_path(path);
	if (sof)
		atomic64_inc(&state->apple_rx_sof);
	if (eof == 3)
		atomic64_inc(&state->apple_rx_eof3);
	else
		atomic64_inc(&state->apple_rx_eof_other);

	tqp = tbv_qp_get_by_num(state, qpn);
	if (!tqp) {
		atomic64_inc(&state->data_rx_no_qp);
		return;
	}

	/*
	 * macOS emits short single-frame SENDs as EOF=3 without SOF. Treat
	 * those as a complete message start, but keep rejecting idle non-final
	 * fragments because there is no Apple-side sequence number to recover
	 * the missing prefix.
	 */
	starts_message = sof || raw_rx || eof == TBV_DATA_PDF_FRAME_END;

	/*
	 * Apple frames carry no QPN; demux is by inbound hop ID only, and
	 * every Apple path uses the same hop. Make sure this QP is bound to
	 * the rail the frame actually arrived on so a second Apple peer can
	 * never feed another peer's QP.
	 */
	if (!tqp->rail || tqp->rail != path->rail) {
		atomic64_inc(&state->apple_rx_rail_mismatch);
		atomic64_inc(&state->data_rx_no_qp);
		pr_warn_ratelimited("apple rx frame from unbound rail qpn=%u\n",
				    qpn);
		tbv_qp_put(tqp);
		return;
	}

	mutex_lock(&tqp->rx_lock);
	if (tqp->apple_pending_ready_count)
		tbv_apple_rx_drain_pending_locked(state, tqp);
	if (tqp->apple_rx_discard) {
		/*
		 * Resynchronizing after a dropped message start: swallow
		 * frames up to the EOF=3 boundary so the tail of a truncated
		 * message is never delivered as a complete message.
		 */
		atomic64_inc(&state->apple_rx_resync_dropped);
		if (eof == 3)
			tqp->apple_rx_discard = false;
		mutex_unlock(&tqp->rx_lock);
		tbv_qp_put(tqp);
		return;
	}
	if (sof && tqp->apple_pending_active >= 0)
		atomic64_inc(&state->apple_rx_sof_while_active);
	if (tqp->apple_pending_active < 0 && !starts_message) {
		atomic64_inc(&state->apple_rx_no_sof_when_idle);
		atomic64_inc(&state->data_rx_bad_frame);
		mutex_unlock(&tqp->rx_lock);
		tbv_qp_put(tqp);
		return;
	}

	pending = tbv_apple_pending_active_locked(state, tqp);
	if (!pending) {
		atomic64_inc(&state->data_rx_no_recv);
		atomic64_inc(&state->data_rx_reorder_dropped);
		/*
		 * A message-start frame was dropped. Unless this frame also
		 * carried the message's EOF=3, the rest of the message is
		 * still inbound and must be discarded at the boundary, not
		 * delivered as a fresh (truncated) message.
		 */
		if (eof != 3)
			tqp->apple_rx_discard = true;
		mutex_unlock(&tqp->rx_lock);
		tbv_qp_put(tqp);
		return;
	}

	if (tbv_apple_rx_trace_take())
		pr_info("apple rx qpn=%u sof=%u eof=%u len=%u pending_len=%u recv_count=%u pending_active=%d pending_ready=%u\n",
			qpn, sof, eof, len, pending->delivered,
			tqp->recv_count, tqp->apple_pending_active,
			tqp->apple_pending_ready_count);

	ret = tbv_apple_rx_copy_frame_to_buf(tqp, pending, payload, len, eof,
					     &user_len);
	if (ret) {
		atomic64_inc(&state->data_rx_bad_frame);
		pending->status = (ret == -EMSGSIZE || ret == -ENOSPC) ?
			IB_WC_LOC_LEN_ERR : IB_WC_LOC_PROT_ERR;
	}

	if (eof == 3) {
		tbv_apple_pending_finish_locked(tqp);
		tbv_apple_rx_drain_pending_locked(state, tqp);
	} else if (!pending->active) {
		atomic64_inc(&state->apple_rx_eof_without_active);
	}
	mutex_unlock(&tqp->rx_lock);
	tbv_qp_put(tqp);
}