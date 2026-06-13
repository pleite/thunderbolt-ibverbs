// SPDX-License-Identifier: GPL-2.0
/*
 * Memory-region verb entry points for the tbv ibverbs provider.
 *
 * Split out of ibdev.c (finding R7).  Shared provider types and helpers live
 * in ibdev_internal.h; the public verb prototypes live in ibdev_split.h.
 */

#include <linux/overflow.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_verbs.h>

#include "tbv.h"
#include "ibdev_internal.h"
#include "ibdev_split.h"

/*
 * Insert @mr under a freshly drawn random key, retrying on the (astronomically
 * unlikely) event of a collision with an existing key.  Key 0 is reserved: it
 * is the implicit local_dma_lkey value, which this driver intentionally does
 * not honour.  Must be called with owner->verbs_mrs_xa locked, so insertion
 * uses GFP_ATOMIC (the lock is held with IRQs disabled).
 */
static int tbv_mr_insert_random_key(struct tbv_state *owner, struct tbv_mr *mr,
				    u32 *out_key)
{
	unsigned int attempts;
	u32 key;
	int ret;

	for (attempts = 0; attempts < TBV_MR_KEY_MAX_ATTEMPTS; attempts++) {
		key = get_random_u32();
		if (key == 0)
			continue;
		ret = __xa_insert(&owner->verbs_mrs_xa, key, mr, GFP_ATOMIC);
		if (ret == -EBUSY)
			continue;
		if (ret)
			return ret;
		*out_key = key;
		return 0;
	}
	return -ENOSPC;
}

static int tbv_mr_publish(struct tbv_mr *mr, struct ib_pd *pd)
{
	struct tbv_state *owner = tbv_ibdev_state(pd->device);
	unsigned long flags;
	u32 lkey = 0, rkey = 0;
	int ret;

	mr->base.device = pd->device;
	mr->base.pd = pd;
	mr->owner = owner;
	mr->peer_id = tbv_ibdev_peer_id(pd->device);
	refcount_set(&mr->refs, 1);
	INIT_WORK(&mr->free_work, tbv_mr_free_work);

	xa_lock_irqsave(&owner->verbs_mrs_xa, flags);
	ret = tbv_mr_insert_random_key(owner, mr, &lkey);
	if (!ret) {
		ret = tbv_mr_insert_random_key(owner, mr, &rkey);
		if (ret)
			__xa_erase(&owner->verbs_mrs_xa, lkey);
	}
	xa_unlock_irqrestore(&owner->verbs_mrs_xa, flags);
	if (ret == -ENOSPC)
		pr_warn_ratelimited("failed to allocate unique MR key\n");
	if (ret)
		return ret;

	mr->base.lkey = lkey;
	mr->base.rkey = rkey;
	if ((u32)atomic_inc_return(&owner->verbs_mrs) > TBV_IBDEV_MAX_MR) {
		atomic_dec(&owner->verbs_mrs);
		xa_lock_irqsave(&owner->verbs_mrs_xa, flags);
		__xa_erase(&owner->verbs_mrs_xa, lkey);
		if (rkey != lkey)
			__xa_erase(&owner->verbs_mrs_xa, rkey);
		xa_unlock_irqrestore(&owner->verbs_mrs_xa, flags);
		return -ENOSPC;
	}
	return 0;
}

struct ib_mr *tbv_get_dma_mr(struct ib_pd *pd, int access)
{
	struct tbv_mr *mr;
	int ret;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->base.type = IB_MR_TYPE_DMA;
	mr->base.iova = 0;
	mr->base.length = U64_MAX;
	mr->start = 0;
	mr->length = U64_MAX;
	mr->virt_addr = 0;
	mr->access = access;
	mr->dma_mr = true;

	ret = tbv_mr_publish(mr, pd);
	if (ret) {
		kfree(mr);
		return ERR_PTR(ret);
	}

	return &mr->base;
}

struct ib_mr *tbv_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				     u64 virt_addr, int access,
#ifdef TBV_KERNEL_HAS_IB_DMAH
				     struct ib_dmah *dmah,
#endif
				     struct ib_udata *udata)
{
	struct tbv_mr *mr;
	int ret;
	u64 start_end;
	u64 virt_end;

	if (!length)
		return ERR_PTR(-EINVAL);
	if (check_add_overflow(start, length, &start_end) ||
	    check_add_overflow(virt_addr, length, &virt_end))
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->umem = ib_umem_get(pd->device, start, length, access);
	if (IS_ERR(mr->umem)) {
		struct ib_umem *umem = mr->umem;

		kfree(mr);
		return ERR_CAST(umem);
	}

	mr->base.type = IB_MR_TYPE_USER;
	mr->base.iova = virt_addr;
	mr->base.length = length;
	mr->start = start;
	mr->length = length;
	mr->virt_addr = virt_addr;
	mr->access = access;
	ret = tbv_mr_publish(mr, pd);
	if (ret) {
		ib_umem_release(mr->umem);
		kfree(mr);
		return ERR_PTR(ret);
	}
	return &mr->base;
}

int tbv_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct tbv_mr *mr = container_of(ibmr, struct tbv_mr, base);
	unsigned long flags;

	if (mr->owner) {
		xa_lock_irqsave(&mr->owner->verbs_mrs_xa, flags);
		mr->closing = true;
		__xa_erase(&mr->owner->verbs_mrs_xa, ibmr->lkey);
		if (ibmr->rkey != ibmr->lkey)
			__xa_erase(&mr->owner->verbs_mrs_xa, ibmr->rkey);
		xa_unlock_irqrestore(&mr->owner->verbs_mrs_xa, flags);
	}

	tbv_mr_put(mr);
	return 0;
}
