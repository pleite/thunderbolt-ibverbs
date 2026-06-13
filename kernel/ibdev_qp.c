// SPDX-License-Identifier: GPL-2.0
/*
 * Queue-pair verb entry points (create/destroy/modify/query) for the tbv
 * ibverbs provider.
 *
 * Split out of ibdev.c (finding R7).  Shared provider types live in
 * ibdev_internal.h; the QP lifecycle and state-machine helpers these entry
 * points call remain in ibdev.c next to the data path that also drives them,
 * and are declared in ibdev_internal.h.
 */

#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <rdma/ib_verbs.h>

#include "tbv.h"
#include "ibdev_internal.h"
#include "ibdev_split.h"

int tbv_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *init_attr,
			 struct ib_udata *udata)
{
	struct tbv_qp *tqp = container_of(qp, struct tbv_qp, base);
	struct tbv_state *state = tbv_ibdev_state(qp->device);
	struct tbv_ibdev *dev = tbv_to_ibdev(qp->device);
	unsigned long flags;
	bool gsi;
	bool qp_counted = false;
	u32 max_qp;
	int qpn;
	int ret;

	if (!init_attr || init_attr->srq)
		return -EOPNOTSUPP;
	gsi = init_attr->qp_type == IB_QPT_GSI;
	if (init_attr->qp_type != IB_QPT_RC &&
	    init_attr->qp_type != IB_QPT_UC &&
	    init_attr->qp_type != IB_QPT_GSI)
		return -EOPNOTSUPP;
	tqp->backend = tbv_ibdev_backend(qp->device);
	if (tbv_backend_is_apple(tqp->backend) &&
	    init_attr->qp_type != IB_QPT_UC)
		return -EOPNOTSUPP;
	if (gsi && (udata || init_attr->port_num != 1))
		return -EOPNOTSUPP;

	mutex_lock(&state->lock);
	tqp->rail = tbv_select_qp_rail_locked(dev, tqp->backend, gsi,
					      &tqp->rail_binding_counted);
	if (!tqp->rail) {
		mutex_unlock(&state->lock);
		return -ENOTCONN;
	}
	if (tqp->backend == TBV_BACKEND_NATIVE && tqp->rail->peer)
		tqp->peer_session_id =
			tqp->rail->peer->auth_established_session_id;
	mutex_unlock(&state->lock);
	if (init_attr->cap.max_send_wr > TBV_IBDEV_MAX_QP_WR ||
	    init_attr->cap.max_recv_wr > TBV_IBDEV_MAX_QP_WR ||
	    init_attr->cap.max_send_sge > TBV_IBDEV_MAX_SGE ||
	    init_attr->cap.max_recv_sge > TBV_IBDEV_MAX_SGE) {
		ret = -EINVAL;
		goto err_put_rail;
	}
	max_qp = U32_MAX;
	if (!tbv_backend_is_apple(tqp->backend))
		max_qp = TBV_IBDEV_MAX_QP + (state->cfg.apple_enabled ? 1 : 0);
	if ((u32)atomic_inc_return(&state->verbs_qps) > max_qp) {
		atomic_dec(&state->verbs_qps);
		ret = -ENOSPC;
		goto err_put_rail;
	}
	qp_counted = true;

	if (gsi) {
		qpn = TBV_GSI_QPN;
	} else {
		qpn = tbv_alloc_qpn(state, tqp->backend);
		if (qpn < 0) {
			ret = qpn;
			goto err_put_rail;
		}
	}

	if (init_attr->cap.max_recv_wr) {
		tqp->recvq = kcalloc(init_attr->cap.max_recv_wr,
				     sizeof(*tqp->recvq), GFP_KERNEL);
		if (!tqp->recvq) {
			if (!gsi)
				ida_free(&tbv_qpn_ida, qpn);
			ret = -ENOMEM;
			goto err_put_rail;
		}
		tqp->recvq_size = init_attr->cap.max_recv_wr;
	}

	if (tbv_backend_is_apple(tqp->backend)) {
		u32 slots = min_t(u32, READ_ONCE(apple_rx_pending_slots),
				  TBV_APPLE_PENDING_RX_MAX_SLOTS);

		if (!slots || !READ_ONCE(apple_rx_pending_bytes) ||
		    !READ_ONCE(apple_rx_pending_total_bytes)) {
			kfree(tqp->recvq);
			if (!gsi)
				ida_free(&tbv_qpn_ida, qpn);
			ret = -EINVAL;
			goto err_put_rail;
		}

		tqp->apple_pending =
			kvcalloc(slots, sizeof(*tqp->apple_pending),
				 GFP_KERNEL);
		if (!tqp->apple_pending) {
			kfree(tqp->recvq);
			if (!gsi)
				ida_free(&tbv_qpn_ida, qpn);
			ret = -ENOMEM;
			goto err_put_rail;
		}
		tqp->apple_pending_slot_count = slots;
	}

	tqp->init_attr = *init_attr;
	tqp->owner = state;
	spin_lock_init(&tqp->lock);
	mutex_init(&tqp->rx_lock);
	init_waitqueue_head(&tqp->credit_wait);
	init_waitqueue_head(&tqp->apple_tx_wait);
	init_waitqueue_head(&tqp->refs_wait);
	refcount_set(&tqp->refs, 1);
	atomic_set(&tqp->apple_tx_inflight, 0);
	atomic_set(&tqp->apple_tx_inflight_frames, 0);
	init_completion(&tqp->refs_zero);
	INIT_LIST_HEAD(&tqp->pending_sends);
	INIT_LIST_HEAD(&tqp->pending_reads);
	INIT_LIST_HEAD(&tqp->pending_read_resps);
	INIT_LIST_HEAD(&tqp->apple_sq);
	INIT_LIST_HEAD(&tqp->rx_reorder);
	INIT_WORK(&tqp->apple_sq_work, tbv_apple_sq_work);
	INIT_WORK(&tqp->error_work, tbv_qp_error_work);
	INIT_DELAYED_WORK(&tqp->timeout_work, tbv_qp_timeout_work);
	tqp->apple_pending_active = -1;
	tqp->state = IB_QPS_RESET;
	tqp->type = init_attr->qp_type;
	tqp->qpn_allocated = !gsi;
	qp->qp_num = qpn;
	init_attr->cap.max_inline_data = 0;
	if (!gsi) {
		xa_lock_irqsave(&tqp->owner->verbs_qps_xa, flags);
		ret = __xa_insert(&tqp->owner->verbs_qps_xa, qpn, tqp,
				  GFP_KERNEL);
		xa_unlock_irqrestore(&tqp->owner->verbs_qps_xa, flags);
		if (ret) {
			kvfree(tqp->apple_pending);
			kfree(tqp->recvq);
			ida_free(&tbv_qpn_ida, qpn);
			tqp->qpn_allocated = false;
			goto err_put_rail;
		}
	} else {
		mutex_lock(&state->lock);
		if (dev->gsi_qp) {
			mutex_unlock(&state->lock);
			kvfree(tqp->apple_pending);
			kfree(tqp->recvq);
			ret = -EBUSY;
			goto err_put_rail;
		}
		dev->gsi_qp = tqp;
		mutex_unlock(&state->lock);
	}
	return 0;

err_put_rail:
	if (qp_counted)
		atomic_dec(&state->verbs_qps);
	tbv_qp_unbind_rail(tqp);
	return ret;
}

int tbv_destroy_qp(struct ib_qp *qp, struct ib_udata *udata)
{
	struct tbv_qp *tqp = container_of(qp, struct tbv_qp, base);
	LIST_HEAD(flush);
	unsigned long flags;
	u32 pending;
	u32 i;

	tbv_qp_begin_close(tqp);

	if (tqp->type == IB_QPT_GSI && tqp->rail && tqp->rail->ibdev) {
		mutex_lock(&tqp->owner->lock);
		if (tqp->rail->ibdev->gsi_qp == tqp)
			tqp->rail->ibdev->gsi_qp = NULL;
		mutex_unlock(&tqp->owner->lock);
	}

	wake_up_all(&tqp->credit_wait);
	wake_up_all(&tqp->apple_tx_wait);
	cancel_work_sync(&tqp->apple_sq_work);
	cancel_delayed_work_sync(&tqp->timeout_work);

	tbv_qp_flush_apple_sq(tqp);
	tbv_qp_flush_sends(tqp, &flush);
	while (!list_empty(&flush)) {
		struct tbv_send_ctx *send =
			list_first_entry(&flush, struct tbv_send_ctx, node);
		int status = send->completion_status;

		list_del_init(&send->node);
		tbv_cancel_send_ctx_packets(send);
		tbv_send_complete(send, status);
		tbv_send_ctx_put(send);
	}

	tbv_qp_flush_reads(tqp, &flush);
	while (!list_empty(&flush)) {
		struct tbv_read_ctx *read =
			list_first_entry(&flush, struct tbv_read_ctx, node);

		list_del_init(&read->node);
		tbv_cancel_read_ctx_packets(read);
		tbv_read_complete(read, -ECANCELED);
		tbv_read_ctx_put(read);
	}

	tbv_qp_cancel_read_resps(tqp, &flush);
	while (!list_empty(&flush)) {
		struct tbv_read_resp_ctx *ctx =
			list_first_entry(&flush, struct tbv_read_resp_ctx, node);

		list_del_init(&ctx->node);
		tbv_read_resp_ctx_put(ctx);
	}

	if (!wait_event_timeout(tqp->refs_wait,
				refcount_read(&tqp->refs) == 1,
				msecs_to_jiffies(TBV_QP_DESTROY_TIMEOUT_MS))) {
		pr_warn("QP %u destroy timed out with %u refs; leaving it closing for retry\n",
			qp->qp_num, refcount_read(&tqp->refs));
		return -ETIMEDOUT;
	}

	if (tqp->owner && tqp->qpn_allocated) {
		xa_lock_irqsave(&tqp->owner->verbs_qps_xa, flags);
		__xa_erase(&tqp->owner->verbs_qps_xa, qp->qp_num);
		xa_unlock_irqrestore(&tqp->owner->verbs_qps_xa, flags);
	}

	tbv_qp_flush_active_rx(tqp);
	tbv_qp_flush_reorder(tqp);
	tbv_qp_flush_apple_pending(tqp);
	for (i = 0; i < tqp->apple_pending_slot_count; i++)
		kvfree(tqp->apple_pending[i].buf);
	kvfree(tqp->apple_pending);

	pending = tqp->recv_count;
	if (tqp->owner && pending)
		atomic_sub(pending, &tqp->owner->verbs_recv_wqes);
	kfree(tqp->recvq);
	if (tqp->qpn_allocated) {
		tbv_free_qpn(tqp->backend, qp->qp_num);
		tqp->qpn_allocated = false;
	}
	if (tqp->owner)
		atomic_dec(&tqp->owner->verbs_qps);
	tbv_qp_release_apple_tunnel(tqp);
	tbv_qp_unbind_rail(tqp);
	tbv_qp_put(tqp);
	return 0;
}

int tbv_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
			 int attr_mask, struct ib_udata *udata)
{
	struct tbv_qp *tqp = container_of(qp, struct tbv_qp, base);
	enum ib_qp_state cur_state;
	enum ib_qp_state next_state;
	unsigned long flags;
	bool activate_apple_tunnel = false;
	bool deactivate_apple_tunnel = false;
	bool apple_tunnel_acquired = false;
	bool flush_error = false;
	int ret = 0;

	if (!attr)
		return -EINVAL;

	if ((attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) &&
	    attr->max_dest_rd_atomic > TBV_IBDEV_MAX_READ_CTX)
		return -EINVAL;
	if ((attr_mask & IB_QP_MAX_QP_RD_ATOMIC) &&
	    attr->max_rd_atomic > TBV_IBDEV_MAX_READ_CTX)
		return -EINVAL;

	spin_lock_irqsave(&tqp->lock, flags);
	ret = tbv_validate_modify_qp_locked(tqp, attr, attr_mask, &cur_state,
					    &next_state);
	if (!ret && tbv_qp_uses_apple_transport(tqp)) {
		activate_apple_tunnel =
			tbv_qp_state_uses_transport(next_state);
		deactivate_apple_tunnel =
			tbv_qp_state_uses_transport(cur_state) &&
			!tbv_qp_state_uses_transport(next_state);
	}
	spin_unlock_irqrestore(&tqp->lock, flags);
	if (ret)
		return ret;

	if (activate_apple_tunnel) {
		ret = tbv_qp_ensure_apple_tunnel(tqp,
						 &apple_tunnel_acquired);
		if (ret)
			return ret;
	}

	spin_lock_irqsave(&tqp->lock, flags);
	ret = tbv_validate_modify_qp_locked(tqp, attr, attr_mask, &cur_state,
					    &next_state);
	if (ret)
		goto out_unlock;

	if (attr_mask & IB_QP_STATE) {
		flush_error = cur_state != IB_QPS_ERR &&
			      attr->qp_state == IB_QPS_ERR;
		tqp->state = attr->qp_state;
		tqp->attr.qp_state = attr->qp_state;
	}
	if (attr_mask & IB_QP_PKEY_INDEX)
		tqp->attr.pkey_index = attr->pkey_index;
	if (attr_mask & IB_QP_PORT)
		tqp->attr.port_num = attr->port_num;
	if (attr_mask & IB_QP_QKEY)
		tqp->attr.qkey = attr->qkey;
	if (attr_mask & IB_QP_ACCESS_FLAGS)
		tqp->attr.qp_access_flags = attr->qp_access_flags;
	if (attr_mask & IB_QP_AV)
		tqp->attr.ah_attr = attr->ah_attr;
	if (attr_mask & IB_QP_PATH_MTU)
		tqp->attr.path_mtu = attr->path_mtu;
	if (attr_mask & IB_QP_DEST_QPN) {
		tqp->attr.dest_qp_num = attr->dest_qp_num;
		tqp->dest_qp_known = true;
	}
	if (attr_mask & IB_QP_RQ_PSN) {
		tqp->rx_expected_psn = attr->rq_psn & TBV_PSN_MASK;
		tqp->attr.rq_psn = attr->rq_psn & TBV_PSN_MASK;
	}
	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		tqp->attr.max_dest_rd_atomic = attr->max_dest_rd_atomic;
	if (attr_mask & IB_QP_MIN_RNR_TIMER)
		tqp->attr.min_rnr_timer = attr->min_rnr_timer;
	if (attr_mask & IB_QP_SQ_PSN) {
		tqp->send_psn = attr->sq_psn & TBV_PSN_MASK;
		tqp->attr.sq_psn = attr->sq_psn & TBV_PSN_MASK;
	}
	if (attr_mask & IB_QP_TIMEOUT) {
		tqp->attr.timeout = attr->timeout;
		tqp->ack_timeout_set = true;
	}
	if (attr_mask & IB_QP_RETRY_CNT)
		tqp->attr.retry_cnt = attr->retry_cnt;
	if (attr_mask & IB_QP_RNR_RETRY)
		tqp->attr.rnr_retry = attr->rnr_retry;
	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC)
		tqp->attr.max_rd_atomic = attr->max_rd_atomic;
out_unlock:
	spin_unlock_irqrestore(&tqp->lock, flags);
	if (ret) {
		if (apple_tunnel_acquired)
			tbv_qp_release_apple_tunnel(tqp);
		return ret;
	}
	if (flush_error || deactivate_apple_tunnel) {
		tbv_qp_flush_error(tqp);
		if (deactivate_apple_tunnel)
			flush_work(&tqp->apple_sq_work);
	}
	if (deactivate_apple_tunnel)
		tbv_qp_release_apple_tunnel(tqp);
	if (attr_mask & (IB_QP_STATE | IB_QP_DEST_QPN))
		tbv_qp_advertise_recv_credits(tqp);
	return 0;
}

int tbv_query_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
			int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct tbv_qp *tqp = container_of(qp, struct tbv_qp, base);

	if (attr)
		*attr = tqp->attr;
	if (attr)
		attr->qp_state = tqp->state;
	if (init_attr)
		*init_attr = tqp->init_attr;
	return 0;
}

int tbv_post_send(struct ib_qp *qp, const struct ib_send_wr *wr,
			 const struct ib_send_wr **bad_wr)
{
	struct tbv_qp *tqp = container_of(qp, struct tbv_qp, base);
	const struct ib_send_wr *cur;
	int ret;

	for (cur = wr; cur; cur = cur->next) {
		ret = tbv_post_send_one(tqp, cur);
		if (ret) {
			if (bad_wr)
				*bad_wr = cur;
			return ret;
		}
	}

	return 0;
}

static int tbv_validate_recv_sge(struct tbv_qp *tqp, const struct ib_sge *sge)
{
	struct tbv_mr *mr;
	u64 mr_end;
	u64 end;
	int ret = 0;

	if (!sge->length)
		return 0;
	if (tbv_qp_accepts_kernel_dma_lkey(tqp, sge->lkey))
		return ib_virt_dma_to_ptr(sge->addr) ? 0 : -EFAULT;
	if (check_add_overflow(sge->addr, (u64)sge->length, &end))
		return -EINVAL;

	mr = tbv_mr_get(tqp->owner, sge->lkey, tbv_qp_peer_id(tqp));
	if (!mr)
		return -EINVAL;
	if (!(mr->access & IB_ACCESS_LOCAL_WRITE)) {
		ret = -EACCES;
		goto err_put;
	}
	if (check_add_overflow(mr->start, mr->length, &mr_end)) {
		ret = -EINVAL;
		goto err_put;
	}
	if (sge->addr < mr->start || end > mr_end) {
		ret = -EFAULT;
		goto err_put;
	}

	tbv_mr_put(mr);
	return 0;

err_put:
	tbv_mr_put(mr);
	return ret;
}

int tbv_post_recv(struct ib_qp *qp, const struct ib_recv_wr *wr,
			 const struct ib_recv_wr **bad_wr)
{
	struct tbv_qp *tqp = container_of(qp, struct tbv_qp, base);
	unsigned long flags;
	const struct ib_recv_wr *cur;
	u32 posted = 0;
	int ret;

	if (!tbv_qp_allows_post(tqp)) {
		if (bad_wr)
			*bad_wr = wr;
		return -EINVAL;
	}

	for (cur = wr; cur; cur = cur->next) {
		const struct ib_sge *sge = NULL;

		if (cur->num_sge > 1) {
			ret = -EINVAL;
			goto err_bad;
		}
		if (cur->num_sge == 1) {
			sge = cur->sg_list;
			if (!sge) {
				ret = -EINVAL;
				goto err_bad;
			}
			ret = tbv_validate_recv_sge(tqp, sge);
			if (ret)
				goto err_bad;
		}

		spin_lock_irqsave(&tqp->lock, flags);
		if (tqp->closing || tqp->state == IB_QPS_RESET ||
		    tqp->state == IB_QPS_ERR) {
			spin_unlock_irqrestore(&tqp->lock, flags);
			ret = -EINVAL;
			goto err_bad;
		}
		if (tqp->recv_count == tqp->recvq_size) {
			spin_unlock_irqrestore(&tqp->lock, flags);
			ret = -ENOMEM;
			goto err_bad;
		}

		tbv_recv_wqe_set_wr(tqp, &tqp->recvq[tqp->recv_tail], cur);
		tqp->recvq[tqp->recv_tail].addr = sge ? sge->addr : 0;
		tqp->recvq[tqp->recv_tail].length = sge ? sge->length : 0;
		tqp->recvq[tqp->recv_tail].lkey = sge ? sge->lkey : 0;
		tqp->recv_tail = (tqp->recv_tail + 1) % tqp->recvq_size;
		tqp->recv_count++;
		posted++;
		atomic_inc(&tqp->owner->verbs_recv_wqes);
		spin_unlock_irqrestore(&tqp->lock, flags);
	}

	if (posted)
		tbv_qp_advertise_recv_credits(tqp);
	if (posted) {
		mutex_lock(&tqp->rx_lock);
		if (tbv_qp_uses_apple_transport(tqp))
			tbv_apple_rx_drain_pending_locked(tqp->owner, tqp);
		tbv_rx_drain_reorder_locked(tqp->owner, tqp, NULL);
		mutex_unlock(&tqp->rx_lock);
	}
	return 0;

err_bad:
	if (posted)
		tbv_qp_advertise_recv_credits(tqp);
	if (bad_wr)
		*bad_wr = cur;
	return ret;
}