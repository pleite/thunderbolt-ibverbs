// SPDX-License-Identifier: GPL-2.0

#include "tbv.h"
#include "ibdev_split.h"

int tbv_create_cq(struct ib_cq *cq, const struct ib_cq_init_attr *attr,
		  struct uverbs_attr_bundle *attrs)
{
	return tbv_create_cq_impl(cq, attr, attrs);
}

int tbv_destroy_cq(struct ib_cq *cq, struct ib_udata *udata)
{
	return tbv_destroy_cq_impl(cq, udata);
}

int tbv_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
	return tbv_poll_cq_impl(cq, num_entries, wc);
}

int tbv_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags)
{
	return tbv_req_notify_cq_impl(cq, flags);
}
