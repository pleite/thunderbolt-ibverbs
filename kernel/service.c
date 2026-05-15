// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/thunderbolt.h>
#include <linux/uuid.h>

#include "tbv.h"

static const uuid_t tbv_native_service_uuid =
	UUID_INIT(0x7c2c8f1e, 0x5b4d, 0x4a01, 0x9f, 0x3a,
		  0x2b, 0x8e, 0x6d, 0x4c, 0x1a, 0x07);

static const uuid_t tbv_apple_service_uuid =
	UUID_INIT(0x49bf223e, 0xd4aa, 0x44d7, 0x87, 0x91,
		  0x50, 0x44, 0x5a, 0xc5, 0x2d, 0x5e);

static const char tbv_apple_ca_key[TB_PROPERTY_KEY_SIZE + 1] = {
	(char)0xff, (char)0xff, (char)0xff, (char)0xff,
	(char)0xff, (char)0xff, 'C', 'A', '\0',
};

static struct tb_property_dir *
tbv_service_create_dir_common(const uuid_t *uuid, u32 prtcid, u32 prtcvers,
			      u32 prtcrevs, u32 prtcstns)
{
	struct tb_property_dir *dir;
	int ret;

	dir = tb_property_create_dir(uuid);
	if (!dir)
		return ERR_PTR(-ENOMEM);

	ret = tb_property_add_immediate(dir, "prtcid", prtcid);
	ret = ret ?: tb_property_add_immediate(dir, "prtcvers", prtcvers);
	ret = ret ?: tb_property_add_immediate(dir, "prtcrevs", prtcrevs);
	ret = ret ?: tb_property_add_immediate(dir, "prtcstns", prtcstns);
	if (ret) {
		tb_property_free_dir(dir);
		return ERR_PTR(ret);
	}

	return dir;
}

struct tb_property_dir *tbv_service_create_native_dir(void)
{
	return tbv_service_create_dir_common(&tbv_native_service_uuid,
					     TBV_NATIVE_PRTCID,
					     TBV_NATIVE_PRTCVERS,
					     TBV_NATIVE_PRTCREVS, 0);
}

struct tb_property_dir *tbv_service_create_apple_dir(u32 prtcstns)
{
	struct tb_property_dir *dir;
	int ret;

	dir = tbv_service_create_dir_common(&tbv_apple_service_uuid,
					    TBV_APPLE_PRTCID,
					    TBV_APPLE_PRTCVERS,
					    TBV_APPLE_PRTCREVS,
					    prtcstns);
	if (IS_ERR(dir))
		return dir;

	ret = tb_property_add_immediate(dir, tbv_apple_ca_key, 1);
	if (ret) {
		tb_property_free_dir(dir);
		return ERR_PTR(ret);
	}

	return dir;
}
