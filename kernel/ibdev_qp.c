// SPDX-License-Identifier: GPL-2.0

#include "tbv.h"
#include "ibdev_split.h"

int tbv_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *init_attr,
		  struct ib_udata *udata)
{
	return tbv_create_qp_impl(qp, init_attr, udata);
}

int tbv_destroy_qp(struct ib_qp *qp, struct ib_udata *udata)
{
	return tbv_destroy_qp_impl(qp, udata);
}

int tbv_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr, int attr_mask,
		  struct ib_udata *udata)
{
	return tbv_modify_qp_impl(qp, attr, attr_mask, udata);
}

int tbv_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	return tbv_query_qp_impl(qp, qp_attr, qp_attr_mask, qp_init_attr);
}

int tbv_post_send(struct ib_qp *qp, const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr)
{
	return tbv_post_send_impl(qp, wr, bad_wr);
}

int tbv_post_recv(struct ib_qp *qp, const struct ib_recv_wr *wr,
		  const struct ib_recv_wr **bad_wr)
{
	return tbv_post_recv_impl(qp, wr, bad_wr);
}
