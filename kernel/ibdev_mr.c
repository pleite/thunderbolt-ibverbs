// SPDX-License-Identifier: GPL-2.0

#include "tbv.h"
#include "ibdev_split.h"

struct ib_mr *tbv_get_dma_mr(struct ib_pd *pd, int access)
{
	return tbv_get_dma_mr_impl(pd, access);
}

struct ib_mr *tbv_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			      u64 virt_addr, int access,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
			      struct ib_dmah *dmah,
#endif
			      struct ib_udata *udata)
{
	return tbv_reg_user_mr_impl(pd, start, length, virt_addr, access,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
				dmah,
#endif
				udata);
}

int tbv_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	return tbv_dereg_mr_impl(ibmr, udata);
}
