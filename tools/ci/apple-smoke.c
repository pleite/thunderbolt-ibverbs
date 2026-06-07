// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#include <stdio.h>

#include "proto/apple_wire.h"

#define CHECK(cond)                                                            \
	do {                                                                   \
		if (!(cond)) {                                                 \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
				__LINE__, #cond);                               \
			return 1;                                              \
		}                                                              \
	} while (0)

static int test_idle_single_frame_send(void)
{
	CHECK(tbv_apple_rx_idle_frame_valid(false, 0, TBV_APPLE_EOF_FRAME,
					    false));
	CHECK(tbv_apple_rx_idle_frame_valid(false, TBV_APPLE_SOF_FRAME,
					    TBV_APPLE_EOF_FRAME, false));

	return 0;
}

static int test_idle_continuation_still_rejected(void)
{
	CHECK(!tbv_apple_rx_idle_frame_valid(false, 0, TBV_APPLE_EOF_CONTINUE,
					     false));
	CHECK(tbv_apple_rx_idle_frame_valid(true, 0, TBV_APPLE_EOF_CONTINUE,
					    false));
	CHECK(tbv_apple_rx_idle_frame_valid(false, 0, TBV_APPLE_EOF_CONTINUE,
					    true));

	return 0;
}

static int test_marker_validation(void)
{
	CHECK(tbv_apple_rx_markers_valid(0, TBV_APPLE_EOF_FRAME, false));
	CHECK(tbv_apple_rx_markers_valid(TBV_APPLE_SOF_FRAME,
					 TBV_APPLE_EOF_CONTINUE, false));
	CHECK(tbv_apple_rx_markers_valid(TBV_APPLE_SOF_FRAME,
					 TBV_APPLE_EOF_FRAME, false));
	CHECK(!tbv_apple_rx_markers_valid(2, TBV_APPLE_EOF_FRAME, false));
	CHECK(!tbv_apple_rx_markers_valid(0, 0, false));
	CHECK(tbv_apple_rx_markers_valid(2, 0, true));

	return 0;
}

int main(void)
{
	CHECK(test_idle_single_frame_send() == 0);
	CHECK(test_idle_continuation_still_rejected() == 0);
	CHECK(test_marker_validation() == 0);

	puts("Apple wire smoke OK");
	return 0;
}
