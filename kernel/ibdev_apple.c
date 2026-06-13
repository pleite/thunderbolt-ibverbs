// SPDX-License-Identifier: GPL-2.0

#include "tbv.h"
#include "ibdev_split.h"

void tbv_ibdev_rx_apple_frame(struct tbv_state *state,
			      const struct tbv_path *path,
			      const void *payload, u32 len, u8 sof, u8 eof)
{
	tbv_ibdev_rx_apple_frame_impl(state, path, payload, len, sof, eof);
}
