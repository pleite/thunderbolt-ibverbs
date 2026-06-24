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
#ifdef CONFIG_TBV_GPU_DIRECT
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#endif
#include <rdma/ib_umem.h>
#ifdef TBV_KERNEL_HAS_IB_UMEM_DMABUF_H
#include <rdma/ib_umem_dmabuf.h>
#endif
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

#ifdef TBV_HAS_IB_UMEM_GET_VA_API
	mr->umem = ib_umem_get_va(pd->device, start, length, access);
#else
	mr->umem = ib_umem_get(pd->device, start, length, access);
#endif
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

#ifdef CONFIG_TBV_GPU_DIRECT
/*
 * Move-notify callback for dynamic dma-buf MRs (Phase 4).  The exporting GPU
 * driver invokes this with the dma-buf reservation lock held when it needs to
 * migrate or reclaim the backing pages.  Unmapping here drops our DMA mapping
 * so the pages can move; the data path re-maps lazily (also under the resv
 * lock) on the next transfer.  Because dma-buf MRs only ever touch GPU pages
 * via a bounded CPU copy performed under the same resv lock — never via a
 * long-lived NHI ring DMA — unmapping is sufficient to quiesce in-flight
 * access without a separate fence.  See docs/gpu-direct-plan.md Phase 4.
 */
static void tbv_dmabuf_move_notify(struct dma_buf_attachment *attach)
{
	struct ib_umem_dmabuf *umem_dmabuf = attach->importer_priv;

	ib_umem_dmabuf_unmap_pages(umem_dmabuf);
}

static const struct dma_buf_attach_ops tbv_dmabuf_attach_ops = {
	/*
	 * Unified-memory only: dma-buf pages are imported into host system
	 * memory and copied through the CPU, so PCIe peer-to-peer (discrete-GPU
	 * BAR DMA) is neither used nor supported here.  Enabling P2P is a v2
	 * non-goal tracked in docs/gpu-direct-plan.md (§ future adjustments).
	 */
	.allow_peer2peer = false,
	.move_notify = tbv_dmabuf_move_notify,
};

/*
 * Register a dma-buf-backed (GPU-direct) memory region.
 *
 * Two import modes are supported, selected by the gpu_direct_dynamic load-time
 * parameter:
 *
 *   - Pinned (default, Phase 1): ib_umem_dmabuf_get_pinned() holds a hard pin
 *     for the MR lifetime, so no move-notify callback is required and the pages
 *     are always mapped.
 *   - Dynamic (Phase 4): ib_umem_dmabuf_get() installs a move-notify callback
 *     so the GPU driver may reclaim/migrate the pages while the MR is live; the
 *     data path maps the pages on demand under the dma-buf reservation lock.
 *
 * Gated at load time by the gpu_direct module parameter: when off, or when the
 * runtime dma-buf import fails (for example because the exporting GPU driver is
 * unavailable), the call returns an error so consumers fall back to the
 * host-copy staging path, matching the EOPNOTSUPP probe contract.
 */
struct ib_mr *tbv_reg_dmabuf_mr(struct ib_pd *pd, u64 offset, u64 length,
				u64 virt_addr, int fd, int access,
#ifdef TBV_KERNEL_HAS_IB_DMAH
				struct ib_dmah *dmah,
#endif
				struct uverbs_attr_bundle *attrs)
{
	struct ib_umem_dmabuf *umem_dmabuf;
	bool dynamic = tbv_gpu_direct_dynamic();
	struct tbv_mr *mr;
	u64 virt_end;
	int ret;

	if (tbv_gpu_direct_mode() == TBV_GPU_DIRECT_OFF)
		return ERR_PTR(-EOPNOTSUPP);

#ifdef TBV_KERNEL_HAS_IB_DMAH
	/* DMA-handle-qualified dma-buf MRs are not supported. */
	if (dmah)
		return ERR_PTR(-EOPNOTSUPP);
#endif

	if (!length)
		return ERR_PTR(-EINVAL);
	if (check_add_overflow(virt_addr, length, &virt_end))
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	if (dynamic)
		umem_dmabuf = ib_umem_dmabuf_get(pd->device, offset, length, fd,
						 access, &tbv_dmabuf_attach_ops);
	else
		umem_dmabuf = ib_umem_dmabuf_get_pinned(pd->device, offset,
							length, fd, access);
	if (IS_ERR(umem_dmabuf)) {
		ret = PTR_ERR(umem_dmabuf);
		kfree(mr);
		pr_info_ratelimited("gpu_direct dma-buf MR unavailable (err %d); falling back to host-copy\n",
				    ret);
		return ERR_PTR(ret);
	}

	mr->umem_dmabuf = umem_dmabuf;
	mr->dmabuf_mr = true;
	mr->dmabuf_dynamic = dynamic;
	mr->base.type = IB_MR_TYPE_USER;
	mr->base.iova = virt_addr;
	mr->base.length = length;
	/*
	 * dma-buf MRs have no separate userspace VA: the remote (rkey) address
	 * is the iova, and the umem spans [offset, offset+length).  Anchor
	 * mr->start at virt_addr so the data-path offset arithmetic resolves to
	 * an offset within the umem.
	 */
	mr->start = virt_addr;
	mr->length = length;
	mr->virt_addr = virt_addr;
	mr->access = access;
	ret = tbv_mr_publish(mr, pd);
	if (ret) {
		ib_umem_release(&umem_dmabuf->umem);
		kfree(mr);
		return ERR_PTR(ret);
	}

	pr_info_ratelimited("gpu_direct dma-buf MR registered (len %llu, %s)\n",
			    length, dynamic ? "dynamic" : "pinned");
	return &mr->base;
}
#endif /* CONFIG_TBV_GPU_DIRECT */

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
