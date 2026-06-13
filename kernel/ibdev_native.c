// SPDX-License-Identifier: GPL-2.0

#include "tbv.h"
#include "ibdev_internal.h"
#include "ibdev_split.h"

void tbv_ibdev_rx_native_frame(struct tbv_state *state,
			       struct tbv_path *rx_path,
			       const struct tbv_native_data_header *hdr,
			       const void *payload)
{
	struct tbv_qp *tqp;
	enum tbv_rx_endpoint_status endpoint_status;

	if (!state || !state->verbs_registered)
		return;

	switch (hdr->opcode) {
	case TBV_NATIVE_DATA_OP_SEND_ACK:
	case TBV_NATIVE_DATA_OP_RDMA_READ_ACK:
	case TBV_NATIVE_DATA_OP_RECV_CREDIT:
	case TBV_NATIVE_DATA_OP_RDMA_READ_REQ:
	case TBV_NATIVE_DATA_OP_RDMA_READ_RESP:
	case TBV_NATIVE_DATA_OP_SEND:
	case TBV_NATIVE_DATA_OP_SEND_IMM:
	case TBV_NATIVE_DATA_OP_RDMA_WRITE:
	case TBV_NATIVE_DATA_OP_RDMA_WRITE_IMM:
	case TBV_NATIVE_DATA_OP_MAD:
		break;
	default:
		tbv_rx_bad_header_note(state, rx_path,
				       &state->data_rx_bad_header_opcode,
				       "opcode", hdr,
				       TBV_NATIVE_DATA_HDR_SIZE + hdr->length, 0);
		return;
	}

	if (hdr->opcode == TBV_NATIVE_DATA_OP_MAD) {
		tbv_rx_handle_mad(state, rx_path, hdr, payload);
		return;
	}

	tqp = tbv_qp_get_by_num(state, hdr->dest_qp);
	if (!tqp) {
		atomic64_inc(&state->data_rx_no_qp);
		return;
	}

	if (hdr->opcode == TBV_NATIVE_DATA_OP_RECV_CREDIT) {
		atomic64_inc(&state->data_rx_ack);
		if (!hdr->imm_data) {
			tbv_rx_bad_header_note(state, rx_path,
					       &state->data_rx_bad_header_recv_credit,
					       "recv_credit", hdr,
					       TBV_NATIVE_DATA_HDR_SIZE +
					       hdr->length, 0);
			tbv_qp_put(tqp);
			return;
		}

		endpoint_status = tbv_qp_accept_recv_credit(tqp, hdr);
		if (endpoint_status != TBV_RX_ENDPOINT_OK) {
			if (endpoint_status == TBV_RX_ENDPOINT_QP_ERROR)
				atomic64_inc(&state->data_rx_qp_error);
			else
				atomic64_inc(&state->data_rx_bad_peer);
			tbv_qp_put(tqp);
			return;
		}

		wake_up_all(&tqp->credit_wait);
		tbv_qp_schedule_timeout_now(tqp);
		tbv_qp_put(tqp);
		return;
	}

	endpoint_status = tbv_qp_validate_native_endpoint(tqp, hdr);
	if (endpoint_status != TBV_RX_ENDPOINT_OK) {
		if (endpoint_status == TBV_RX_ENDPOINT_UNCONNECTED)
			atomic64_inc(&state->data_rx_unconnected_qp);
		else if (endpoint_status == TBV_RX_ENDPOINT_QP_ERROR)
			atomic64_inc(&state->data_rx_qp_error);
		else
			atomic64_inc(&state->data_rx_bad_peer);
		tbv_qp_put(tqp);
		return;
	}

	if (hdr->opcode == TBV_NATIVE_DATA_OP_SEND_ACK) {
		LIST_HEAD(acked);
		struct tbv_send_ctx *matched_send = NULL;
		int status;
		bool saw_ack = false;
		bool completed_ack = false;
		bool completed_error = false;

		atomic64_inc(&state->data_rx_ack);
		switch (hdr->imm_data) {
		case TBV_NATIVE_SEND_ACK_OK:
			status = 0;
			saw_ack = tbv_qp_complete_send_ordered(tqp, hdr->psn,
							       status, &acked,
							       &matched_send);
			break;
		case TBV_NATIVE_SEND_ACK_RNR:
			status = -EAGAIN;
			atomic64_inc(&state->data_rx_ack_rnr);
			saw_ack = tbv_qp_note_rnr_ack(tqp, hdr->psn,
						      &acked, &matched_send);
			break;
		case TBV_NATIVE_SEND_ACK_ERROR:
			status = -EIO;
			saw_ack = tbv_qp_complete_send_ordered(tqp, hdr->psn,
							       status, &acked,
							       &matched_send);
			break;
		default:
			tbv_rx_bad_header_note(state, rx_path,
					       &state->data_rx_bad_header_ack,
					       "send_ack", hdr,
					       TBV_NATIVE_DATA_HDR_SIZE +
					       hdr->length, 0);
			tbv_qp_put(tqp);
			return;
		}
		if (matched_send) {
			tbv_note_matched_send_ack(state, hdr, matched_send);
			tbv_send_ctx_put(matched_send);
		}
		if (!saw_ack) {
			if (tbv_qp_ack_is_late_duplicate(tqp, hdr->psn))
				atomic64_inc(&state->data_rx_late_ack);
			else
				atomic64_inc(&state->data_rx_ack_miss);
		}
		while (!list_empty(&acked)) {
			struct tbv_send_ctx *send =
				list_first_entry(&acked, struct tbv_send_ctx,
						 node);

			list_del_init(&send->node);
			completed_ack = true;
			if (send->completion_status)
				completed_error = true;
			if (send->completion_status == -EAGAIN)
				atomic64_inc(&state->data_wr_rnr_retry_exhausted);
			tbv_send_complete(send, send->completion_status);
			tbv_send_ctx_put(send);
		}
		if (completed_error && completed_ack)
			tbv_qp_mark_error(tqp);
		tbv_qp_put(tqp);
		return;
	}

	if (hdr->opcode == TBV_NATIVE_DATA_OP_RDMA_READ_ACK) {
		struct tbv_read_resp_ctx *ctx;

		atomic64_inc(&state->data_rx_ack);
		tbv_count_rx_read_ack(state, hdr->imm_data);
		if (hdr->imm_data == TBV_NATIVE_READ_ACK_RETRY) {
			tbv_qp_retry_read_resp(tqp, hdr->psn);
		} else {
			ctx = tbv_qp_take_read_resp(tqp, hdr->psn);
			if (ctx)
				tbv_read_resp_ctx_put(ctx);
		}
		tbv_qp_put(tqp);
		return;
	}

	if (hdr->opcode == TBV_NATIVE_DATA_OP_RDMA_READ_REQ) {
		tbv_rx_handle_rdma_read_req(state, tqp, hdr, rx_path);
		tbv_qp_put(tqp);
		return;
	}

	if (hdr->opcode == TBV_NATIVE_DATA_OP_RDMA_READ_RESP) {
		tbv_rx_handle_rdma_read_resp(state, tqp, hdr, payload, rx_path);
		tbv_qp_put(tqp);
		return;
	}

	atomic64_inc(&state->data_rx_send);
	if (hdr->opcode == TBV_NATIVE_DATA_OP_SEND)
		atomic64_inc(&state->data_rx_op_send);
	else if (hdr->opcode == TBV_NATIVE_DATA_OP_SEND_IMM)
		atomic64_inc(&state->data_rx_op_send_imm);
	else if (hdr->opcode == TBV_NATIVE_DATA_OP_RDMA_WRITE_IMM)
		atomic64_inc(&state->data_rx_op_write_imm);
	else
		atomic64_inc(&state->data_rx_op_write);

	if (hdr->opcode == TBV_NATIVE_DATA_OP_SEND ||
	    hdr->opcode == TBV_NATIVE_DATA_OP_SEND_IMM)
		tbv_rx_handle_send_fragment(state, tqp, hdr, payload,
					    rx_path);
	else
		tbv_rx_handle_rdma_write_fragment(state, tqp, hdr, payload,
						  rx_path);
	tbv_qp_put(tqp);
}

void tbv_ibdev_rx_frame(struct tbv_state *state, struct tbv_path *rx_path,
			const void *data, u32 len)
{
	struct tbv_native_data_header hdr;
	const u8 *payload;
	int ret;

	if (!state || !state->verbs_registered)
		return;

	ret = tbv_native_data_parse_header(data, len, &hdr);
	if (ret) {
		tbv_rx_bad_header_parse_note(state, rx_path, data, len, ret);
		return;
	}
	if (hdr.length > len - TBV_NATIVE_DATA_HDR_SIZE) {
		tbv_rx_bad_header_note(state, rx_path,
				       &state->data_rx_bad_header_len,
				       "frame_len", &hdr, len, 0);
		return;
	}

	payload = (const u8 *)data + TBV_NATIVE_DATA_HDR_SIZE;
	tbv_ibdev_rx_native_frame(state, rx_path, &hdr, payload);
}