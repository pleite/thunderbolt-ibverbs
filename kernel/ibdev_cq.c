// SPDX-License-Identifier: GPL-2.0
/*
 * Completion-queue verb entry points for the tbv ibverbs provider.
 *
 * Split out of ibdev.c (finding R7).  Shared provider types and helpers live
 * in ibdev_internal.h; the public verb prototypes live in ibdev_split.h.
 * Completion delivery into the CQ ring (tbv_cq_push) stays in ibdev.c with the
 * data path that drives it.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <rdma/ib_verbs.h>

#include "tbv.h"
#include "ibdev_internal.h"
#include "ibdev_split.h"

int tbv_create_cq(struct ib_cq *cq, const struct ib_cq_init_attr *attr,
			 struct uverbs_attr_bundle *attrs)
{
	struct tbv_cq *tcq = container_of(cq, struct tbv_cq, base);

	if (!attr || attr->cqe <= 0 || attr->cqe > TBV_IBDEV_MAX_CQE)
		return -EINVAL;

	tcq->entries = kcalloc(attr->cqe, sizeof(*tcq->entries), GFP_KERNEL);
	if (!tcq->entries)
		return -ENOMEM;

	spin_lock_init(&tcq->lock);
	tcq->owner = tbv_ibdev_state(cq->device);
	tcq->cqe = attr->cqe;
	if ((u32)atomic_inc_return(&tcq->owner->verbs_cqs) > TBV_IBDEV_MAX_CQ) {
		atomic_dec(&tcq->owner->verbs_cqs);
		kfree(tcq->entries);
		tcq->entries = NULL;
		return -ENOSPC;
	}
	return 0;
}

int tbv_destroy_cq(struct ib_cq *cq, struct ib_udata *udata)
{
	struct tbv_cq *tcq = container_of(cq, struct tbv_cq, base);

	if (tcq->owner)
		atomic_dec(&tcq->owner->verbs_cqs);
	kfree(tcq->entries);
	return 0;
}

int tbv_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
	struct tbv_cq *tcq = container_of(cq, struct tbv_cq, base);
	unsigned long flags;
	int polled = 0;
	bool overflowed;

	if (num_entries <= 0 || !wc)
		return 0;

	spin_lock_irqsave(&tcq->lock, flags);
	while (polled < num_entries && tcq->count) {
		wc[polled++] = tcq->entries[tcq->head];
		tcq->head = (tcq->head + 1) % tcq->cqe;
		tcq->count--;
	}
	overflowed = tcq->overflowed;
	spin_unlock_irqrestore(&tcq->lock, flags);

	if (!polled && overflowed)
		return -EIO;
	return polled;
}

int tbv_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags)
{
	struct tbv_cq *tcq = container_of(cq, struct tbv_cq, base);
	unsigned long irq_flags;
	int ret;

	spin_lock_irqsave(&tcq->lock, irq_flags);
	if (tcq->overflowed) {
		ret = -EIO;
	} else {
		tcq->notify_armed = true;
		ret = tcq->count ? 1 : 0;
	}
	spin_unlock_irqrestore(&tcq->lock, irq_flags);
	return ret;
}
