// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "thunderbolt_ibverbs: " fmt

#include <linux/errno.h>

#include "tbv.h"

static const struct tbv_backend_ops tbv_native_backend = {
	.type = TBV_BACKEND_NATIVE,
	.name = "native-linux",
	.supports_rc = true,
	.supports_uc = true,
	.needs_tbnet_identity = false,
};

static const struct tbv_backend_ops tbv_apple_backend = {
	.type = TBV_BACKEND_APPLE,
	.name = "apple-fa57",
	.supports_rc = false,
	.supports_uc = true,
	.needs_tbnet_identity = true,
};

const struct tbv_backend_ops *tbv_backend_get(enum tbv_backend_type type)
{
	switch (type) {
	case TBV_BACKEND_NATIVE:
		return &tbv_native_backend;
	case TBV_BACKEND_APPLE:
		return &tbv_apple_backend;
	default:
		return NULL;
	}
}
