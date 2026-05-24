// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#include <stdio.h>
#include <string.h>

#include "proto/native_data.h"

static int hello_equal(const struct tbv_native_wire_hello *a,
		       const struct tbv_native_wire_hello *b)
{
	return a->capabilities == b->capabilities &&
	       a->rail_id == b->rail_id &&
	       a->route == b->route &&
	       a->tx_hop == b->tx_hop &&
	       a->rx_hop == b->rx_hop &&
	       a->transmit_path == b->transmit_path &&
	       a->tx_ring_size == b->tx_ring_size &&
	       a->rx_ring_size == b->rx_ring_size &&
	       a->path_flags == b->path_flags;
}

static int data_header_equal(const struct tbv_native_data_header *a,
			     const struct tbv_native_data_header *b)
{
	return a->opcode == b->opcode &&
	       a->flags == b->flags &&
	       a->dest_qp == b->dest_qp &&
	       a->src_qp == b->src_qp &&
	       a->psn == b->psn &&
	       a->length == b->length &&
	       a->imm_data == b->imm_data &&
	       a->remote_addr == b->remote_addr &&
	       a->rkey == b->rkey;
}

int main(void)
{
	unsigned char hello_buf[TBV_NATIVE_WIRE_HELLO_MSG_SIZE];
	unsigned char data_buf[TBV_NATIVE_DATA_HDR_SIZE];
	struct tbv_native_wire_hello hello = {
		.capabilities = TBV_NATIVE_WIRE_CAP_UC |
				TBV_NATIVE_WIRE_CAP_RC |
				TBV_NATIVE_WIRE_CAP_MULTI_RAIL,
		.rail_id = 7,
		.route = 0x1122334455667788ull,
		.tx_hop = 8,
		.rx_hop = 9,
		.transmit_path = 10,
		.tx_ring_size = 128,
		.rx_ring_size = 256,
		.path_flags = TBV_NATIVE_WIRE_PATH_FRAME |
			      TBV_NATIVE_WIRE_PATH_E2E,
	};
	struct tbv_native_wire_hello parsed_hello;
	struct tbv_native_wire_info info;
	struct tbv_native_data_header hdr = {
		.opcode = TBV_NATIVE_DATA_OP_RDMA_WRITE_IMM,
		.flags = TBV_NATIVE_DATA_F_LAST | TBV_NATIVE_DATA_F_SOLICITED,
		.dest_qp = 11,
		.src_qp = 12,
		.psn = 13,
		.length = 64,
		.imm_data = 0x58494f01u,
		.remote_addr = 0x8877665544332211ull,
		.rkey = 14,
	};
	struct tbv_native_data_header parsed_hdr;
	int ret;

	ret = tbv_native_wire_build_hello(hello_buf, sizeof(hello_buf), &hello,
					  TBV_NATIVE_WIRE_OP_HELLO, 0, 123,
					  0xaabbccddeeff0011ull, 2);
	if (ret != (int)sizeof(hello_buf))
		return 1;

	memset(&parsed_hello, 0, sizeof(parsed_hello));
	memset(&info, 0, sizeof(info));
	ret = tbv_native_wire_parse_hello(hello_buf, sizeof(hello_buf),
					  &parsed_hello, &info);
	if (ret)
		return 2;
	if (!hello_equal(&hello, &parsed_hello))
		return 3;
	if (info.op != TBV_NATIVE_WIRE_OP_HELLO || info.seq != 123 ||
	    info.xdomain_sequence != 2)
		return 4;

	ret = tbv_native_data_build_header(data_buf, sizeof(data_buf), &hdr);
	if (ret != (int)sizeof(data_buf))
		return 5;

	memset(&parsed_hdr, 0, sizeof(parsed_hdr));
	ret = tbv_native_data_parse_header(data_buf, sizeof(data_buf),
					   &parsed_hdr);
	if (ret)
		return 6;
	if (!data_header_equal(&hdr, &parsed_hdr))
		return 7;

	puts("protocol header smoke OK");
	return 0;
}
