// SPDX-License-Identifier: GPL-2.0

#include "tbv.h"
#include "ibdev_split.h"

void tbv_ibdev_rx_frame(struct tbv_state *state, struct tbv_path *rx_path,
			const void *data, u32 len)
{
	tbv_ibdev_rx_frame_impl(state, rx_path, data, len);
}

void tbv_ibdev_rx_native_frame(struct tbv_state *state, struct tbv_path *rx_path,
			       const struct tbv_native_data_header *hdr,
			       const void *payload)
{
	tbv_ibdev_rx_native_frame_impl(state, rx_path, hdr, payload);
}
