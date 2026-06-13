/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "proto/native_wire.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct tbv_native_wire_hello hello;
	struct tbv_native_wire_info info;
	uint8_t out[TBV_NATIVE_WIRE_HELLO_MSG_SIZE];

	memset(&hello, 0, sizeof(hello));
	memset(&info, 0, sizeof(info));
	if (tbv_native_wire_parse_hello(data, size, &hello, &info) == 0)
		(void)tbv_native_wire_build_hello(out, sizeof(out), &hello,
						  info.op, info.flags, info.seq,
						  info.route, info.xdomain_sequence);
	return 0;
}
