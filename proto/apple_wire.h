/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef TBV_APPLE_WIRE_H
#define TBV_APPLE_WIRE_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 tbv_apple_u8;
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t tbv_apple_u8;
#endif

#define TBV_APPLE_QPN_SHIFT 8u
#define TBV_APPLE_FRAME_SIZE 4096u
#define TBV_APPLE_MAX_MSG_SIZE (16u * 1024u * 1024u)

#define TBV_APPLE_SOF_FRAME 1u
#define TBV_APPLE_EOF_CONTINUE 2u
#define TBV_APPLE_EOF_FRAME 3u

static inline bool tbv_apple_rx_markers_valid(tbv_apple_u8 sof,
					      tbv_apple_u8 eof, bool raw)
{
	if (raw)
		return true;

	return (sof == 0 || sof == TBV_APPLE_SOF_FRAME) &&
	       (eof == TBV_APPLE_EOF_CONTINUE ||
		eof == TBV_APPLE_EOF_FRAME);
}

static inline bool tbv_apple_rx_idle_frame_valid(bool active, tbv_apple_u8 sof,
						 tbv_apple_u8 eof, bool raw)
{
	if (active || raw || sof)
		return true;

	return eof == TBV_APPLE_EOF_FRAME;
}

#endif /* TBV_APPLE_WIRE_H */
