// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "thunderbolt_ibverbs: compat: " fmt

#include <linux/errno.h>
#include <linux/module.h>

#include <linux/thunderbolt.h>

#include "tbv_compat.h"

typedef int (*tbv_ring_throttling_fn)(struct tb_ring *ring,
				      unsigned int interval_nsec);

extern int tb_ring_throttling(struct tb_ring *ring, unsigned int interval_nsec)
	__attribute__((weak));

static tbv_ring_throttling_fn tbv_ring_throttling;

void tbv_compat_init(void)
{
	tbv_ring_throttling = symbol_get(tb_ring_throttling);
	if (tbv_ring_throttling)
		pr_info("using optional tb_ring_throttling() helper\n");
	else
		pr_info("optional tb_ring_throttling() helper unavailable; using stock NHI interrupt throttling\n");

}

void tbv_compat_exit(void)
{
	if (tbv_ring_throttling) {
		symbol_put(tb_ring_throttling);
		tbv_ring_throttling = NULL;
	}
}

bool tbv_compat_has_ring_throttling(void)
{
	return tbv_ring_throttling != NULL;
}

int tbv_compat_ring_throttling(struct tb_ring *ring, unsigned int interval_nsec)
{
	if (!tbv_ring_throttling)
		return -EOPNOTSUPP;

	return tbv_ring_throttling(ring, interval_nsec);
}

u8 tbv_compat_phy_port_from_link(u8 link)
{
	if (!link)
		return 0;

	return (link - 1) / TB_LINKS_PER_PHY_PORT;
}
