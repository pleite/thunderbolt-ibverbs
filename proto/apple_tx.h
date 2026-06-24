/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Apple FA57 TX admission policy helpers.
 *
 * Shared between the kernel module (kernel/ibdev.c) and userspace proto-smoke
 * tests (tools/ci/proto-smoke.c).  Backport of upstream hellas-ai/thunderbolt-
 * ibverbs#44 (a14a175): serialize all non-empty Apple UC SENDs per QP.
 */
#ifndef TBV_APPLE_TX_H
#define TBV_APPLE_TX_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u32 tbv_apple_u32;
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t tbv_apple_u32;
#endif

/*
 * tbv_apple_tx_frame_charge - frame slots consumed by a SEND of @frames frames.
 *
 * Returns the number of frame-inflight slots that should be charged against the
 * per-QP frame window.  Returns 0 when the frame window is disabled
 * (@max_frames == 0) so callers can unconditionally add the result.
 */
static inline tbv_apple_u32 tbv_apple_tx_frame_charge(tbv_apple_u32 frames,
						      unsigned int max_frames)
{
	if (!max_frames)
		return 0;
	return frames < (tbv_apple_u32)max_frames ? frames
						  : (tbv_apple_u32)max_frames;
}

/*
 * tbv_apple_tx_requires_exclusive_window - must this SEND hold the exclusive
 * per-QP TX window?
 *
 * Returns true for every non-empty SEND (frames > 0).
 *
 * Upstream #44 (a14a175): previously only multi-frame SENDs (frames > 1)
 * required exclusivity.  Single-frame SENDs could overlap in flight and
 * interleave at the macOS peer, which carries no message sequence number,
 * causing short-send corruption and peer-side timeouts.  Requiring exclusivity
 * for all non-empty SENDs closes this window while keeping the parameter
 * apple_tx_max_inflight_wr as a no-op backward-compatibility knob.
 */
static inline bool tbv_apple_tx_requires_exclusive_window(tbv_apple_u32 frames)
{
	return frames > 0;
}

#endif /* TBV_APPLE_TX_H */
