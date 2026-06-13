/* SPDX-License-Identifier: GPL-2.0 */

#ifndef TBV_COMPAT_H
#define TBV_COMPAT_H

#include <linux/types.h>

struct tb_ring;

void tbv_compat_init(void);
void tbv_compat_exit(void);
bool tbv_compat_has_ring_throttling(void);
int tbv_compat_ring_throttling(struct tb_ring *ring, unsigned int interval_nsec);
u8 tbv_compat_phy_port_from_link(u8 link);

#endif /* TBV_COMPAT_H */
