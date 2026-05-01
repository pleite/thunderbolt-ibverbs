/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _USB4_RDMA_H
#define _USB4_RDMA_H

#include <linux/debugfs.h>

/* bar.c — read-only BAR0 explorer for USB4 host routers. */
int  usb4_rdma_pci_init(struct dentry *parent_dir);
void usb4_rdma_pci_exit(void);

/* loadtest.c — multi-ring xdomain throughput probe. */
int  usb4_rdma_loadtest_init(struct dentry *parent_dir);
void usb4_rdma_loadtest_exit(void);

/* ibdev.c — soft-RDMA ib_device skeleton. */
int  usb4_rdma_ibdev_init(void);
void usb4_rdma_ibdev_exit(void);
void usb4_rdma_ibdev_peer_event(bool joined);

/* data.c — per-peer ring management + wire protocol. */
struct tb_service;
struct u4_wire_hdr;
typedef int (*usb4_rdma_data_fill_fn)(void *dst, u32 length, void *ctx);
/* Returns 1 if this service became the active data peer, 0 if skipped,
 * or a negative errno on attach failure. */
int  usb4_rdma_data_attach_peer(struct tb_service *svc);
bool usb4_rdma_data_detach_peer(struct tb_service *svc);
int  usb4_rdma_data_send(u8 opcode, u32 src_qp, u32 dest_qp, u32 psn,
			 u8 flags, __be32 imm_data, u64 remote_addr, u32 rkey,
			 usb4_rdma_data_fill_fn fill, void *fill_ctx,
			 u32 length);
int  usb4_rdma_data_register_qp(u32 qp_num, void *qp);
void usb4_rdma_data_unregister_qp(u32 qp_num);
void usb4_rdma_data_set_rx_handler(void (*h)(void *qp,
					     const struct u4_wire_hdr *hdr,
					     const void *payload, u32 length));
bool usb4_rdma_data_peer_attached(void);

#endif /* _USB4_RDMA_H */
