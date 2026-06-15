/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TBV_IBDEV_SPLIT_H
#define TBV_IBDEV_SPLIT_H

#include <linux/version.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>

struct ib_udata;

int tbv_create_cq(struct ib_cq *cq, const struct ib_cq_init_attr *attr,
		  struct uverbs_attr_bundle *attrs);
int tbv_destroy_cq(struct ib_cq *cq, struct ib_udata *udata);
int tbv_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc);
int tbv_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);

struct ib_mr *tbv_get_dma_mr(struct ib_pd *pd, int access);
struct ib_mr *tbv_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			 u64 virt_addr, int access,
#ifdef TBV_KERNEL_HAS_IB_DMAH
			 struct ib_dmah *dmah,
#endif
			 struct ib_udata *udata);
int tbv_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata);

#ifdef CONFIG_TBV_GPU_DIRECT
struct ib_mr *tbv_reg_dmabuf_mr(struct ib_pd *pd, u64 offset, u64 length,
				u64 virt_addr, int fd, int access,
#ifdef TBV_KERNEL_HAS_IB_DMAH
				struct ib_dmah *dmah,
#endif
				struct uverbs_attr_bundle *attrs);
#endif

int tbv_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *init_attr,
		  struct ib_udata *udata);
int tbv_destroy_qp(struct ib_qp *qp, struct ib_udata *udata);
int tbv_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr, int attr_mask,
		  struct ib_udata *udata);
int tbv_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr);
int tbv_post_send(struct ib_qp *qp, const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr);
int tbv_post_recv(struct ib_qp *qp, const struct ib_recv_wr *wr,
		  const struct ib_recv_wr **bad_wr);

#endif
