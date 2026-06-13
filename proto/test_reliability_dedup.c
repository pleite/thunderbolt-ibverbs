// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Fault-injection test: duplicate-frame detection across the full
 * TBV_REL_ACK_HISTORY_SIZE (== TBV_REL_ORDER_MAX) outstanding-WR range.
 *
 * Build and run:
 *   make -C proto test
 * or manually:
 *   cc -Wall -Wextra -std=c11 -o test_reliability_dedup \
 *      test_reliability_dedup.c reliability.c && ./test_reliability_dedup
 */
#include <assert.h>
#include <stdio.h>
#include "reliability.h"

#define CONN_ID  42ULL
#define PAYLOAD  4096U

/* Drive a single-frame op to completion.  Asserts no error, no duplicate. */
static void drive_op(struct tbv_rel_rx_op *rx, tbv_rel_u32 op_id)
{
	struct tbv_rel_data_frame frame = {
		.conn_id         = CONN_ID,
		.op_id           = op_id,
		.frame_seq       = 0,
		.retry_generation = 0,
		.offset          = 0,
		.length          = 64,
		.frag_index      = 0,
		.frag_count      = 1,
		.op              = TBV_REL_OP_SEND,
	};
	struct tbv_rel_rx_event event;
	int ret;

	ret = tbv_rel_rx_on_data(rx, &frame, /*recv_available=*/true, &event);
	assert(ret == 0);
	assert(!event.duplicate);
	assert(event.completion_valid);
	assert(event.ack_valid);
	assert(event.ack.op_id == op_id);
}

/* Inject a duplicate frame for op_id and assert it is recognised. */
static void assert_dup(struct tbv_rel_rx_op *rx, tbv_rel_u32 op_id)
{
	struct tbv_rel_data_frame frame = {
		.conn_id         = CONN_ID,
		.op_id           = op_id,
		.frame_seq       = 0,
		.retry_generation = 0,
		.offset          = 0,
		.length          = 64,
		.frag_index      = 0,
		.frag_count      = 1,
		.op              = TBV_REL_OP_SEND,
	};
	struct tbv_rel_rx_event event;
	int ret;

	ret = tbv_rel_rx_on_data(rx, &frame, /*recv_available=*/true, &event);
	assert(ret == 0);
	assert(event.duplicate);
	assert(event.ack_valid);
	assert(event.ack.op_id == op_id);
}

/*
 * test_basic_duplicate — a single completed op must be detected as a
 * duplicate on retransmission.
 */
static void test_basic_duplicate(void)
{
	struct tbv_rel_rx_op rx;

	tbv_rel_rx_init(&rx, CONN_ID, PAYLOAD);
	drive_op(&rx, 0);
	assert_dup(&rx, 0);
	printf("PASS: test_basic_duplicate\n");
}

/*
 * test_full_window_duplicate — after TBV_REL_ACK_HISTORY_SIZE - 1 further
 * ops complete (filling every slot in the history table), a duplicate of the
 * very first op must still be detected.
 *
 * With the old 16-entry linear ring this failed once > 16 ops had completed;
 * the new hash-indexed table (size == TBV_REL_ORDER_MAX) prevents wrap-around
 * within the outstanding-WR window.
 */
static void test_full_window_duplicate(void)
{
	struct tbv_rel_rx_op rx;
	tbv_rel_u32 i;

	tbv_rel_rx_init(&rx, CONN_ID, PAYLOAD);

	drive_op(&rx, 0);

	/* Fill the remaining TBV_REL_ACK_HISTORY_SIZE - 1 slots. */
	for (i = 1; i < TBV_REL_ACK_HISTORY_SIZE; i++)
		drive_op(&rx, i);

	/*
	 * Slot for op_id 0 is still present (op_id 0 maps to slot 0 %
	 * TBV_REL_ACK_HISTORY_SIZE == 0, untouched by ops 1..127).
	 */
	assert_dup(&rx, 0);
	printf("PASS: test_full_window_duplicate\n");
}

/*
 * test_window_eviction — once op_id N occupies the same slot as op_id 0
 * (i.e. N == TBV_REL_ACK_HISTORY_SIZE), op_id 0 is evicted.  A retransmit
 * of op_id 0 must NOT be treated as a duplicate, while a retransmit of op_id
 * N must still be detected.
 */
static void test_window_eviction(void)
{
	struct tbv_rel_rx_op rx;
	struct tbv_rel_data_frame frame;
	struct tbv_rel_rx_event event;
	tbv_rel_u32 i;
	int ret;

	tbv_rel_rx_init(&rx, CONN_ID, PAYLOAD);

	/* Fill all TBV_REL_ACK_HISTORY_SIZE slots (op_id 0 .. SIZE-1). */
	for (i = 0; i < TBV_REL_ACK_HISTORY_SIZE; i++)
		drive_op(&rx, i);

	/* op_id == TBV_REL_ACK_HISTORY_SIZE lands in slot 0, evicting op_id 0. */
	drive_op(&rx, TBV_REL_ACK_HISTORY_SIZE);

	/*
	 * op_id 0 is evicted: retransmit is not recognised as a duplicate.
	 * With recv_available=false the engine issues an RNR instead.
	 */
	frame = (struct tbv_rel_data_frame){
		.conn_id         = CONN_ID,
		.op_id           = 0,
		.frame_seq       = 0,
		.retry_generation = 0,
		.offset          = 0,
		.length          = 64,
		.frag_index      = 0,
		.frag_count      = 1,
		.op              = TBV_REL_OP_SEND,
	};
	ret = tbv_rel_rx_on_data(&rx, &frame, /*recv_available=*/false, &event);
	assert(ret == 0);
	assert(!event.duplicate);
	assert(event.rnr);

	/* op_id TBV_REL_ACK_HISTORY_SIZE is in slot 0 → still detectable. */
	assert_dup(&rx, TBV_REL_ACK_HISTORY_SIZE);
	printf("PASS: test_window_eviction\n");
}

int main(void)
{
	test_basic_duplicate();
	test_full_window_duplicate();
	test_window_eviction();
	printf("All dedup tests passed.\n");
	return 0;
}
