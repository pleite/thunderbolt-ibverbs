# Architecture Overview

This document describes the layered architecture of `thunderbolt-ibverbs`: how
a raw Thunderbolt/USB4 DMA ring becomes a standard InfiniBand RDMA verb device
that NCCL, UCX, and perftest can use without modification.

## Layer diagram

```
┌─────────────────────────────────────────────────────────────────┐
│  Application (vLLM / NCCL / perftest / your code)              │
└─────────────────────────┬───────────────────────────────────────┘
                          │  libibverbs API
┌─────────────────────────▼───────────────────────────────────────┐
│  rdma-core  (libibverbs + uverbs ioctl)                         │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  usb4_rdma provider  (userspace/usb4_rdma/)              │   │
│  │  • opens /dev/infiniband/uverbsN                         │   │
│  │  • implements ibv_alloc_pd, ibv_create_qp, …            │   │
│  │  • forwards data-plane ops to kernel via uverbs ioctl    │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────┬───────────────────────────────────────┘
                          │  uverbs / ib_core
┌─────────────────────────▼───────────────────────────────────────┐
│  Kernel module  thunderbolt_ibverbs  (kernel/)                  │
│                                                                  │
│  ibdev.c   — registers an ib_device; implements verbs           │
│  rail.c    — per-lane QP → Thunderbolt ring mapping             │
│  native.c  — Linux-to-Linux wire protocol (native transport)   │
│  apple.c   — Apple AD/FA57 wire protocol (mac_compat)           │
│  peer.c    — peer discovery, UUID allow-list                    │
│  path.c    — Thunderbolt path setup / teardown                  │
│  service.c — tb_service driver bind / unbind                    │
│  backend.c — common send/receive engine                         │
│  profile.c — selects transports based on profile= parameter     │
└─────────────────────────┬───────────────────────────────────────┘
                          │  kernel Thunderbolt subsystem
┌─────────────────────────▼───────────────────────────────────────┐
│  Linux Thunderbolt/USB4 subsystem  (drivers/thunderbolt/)       │
│  tb_ring — DMA ring pair (TX + RX per lane)                     │
│  tb_service — XDomain service enumeration                       │
│  tb_xdomain — cross-domain control protocol                     │
└─────────────────────────┬───────────────────────────────────────┘
                          │  PCIe / NHI registers
┌─────────────────────────▼───────────────────────────────────────┐
│  Thunderbolt 4 / USB4 cable + NHI controller                    │
└─────────────────────────────────────────────────────────────────┘
```

## Kernel module

The kernel module lives under `kernel/` and is the only piece that requires a
GPL-2.0 licence.  At a high level it does four things:

1. **Service binding** (`service.c`, `peer.c`).  The Thunderbolt XDomain layer
   discovers remote peers automatically.  The module registers as a `tb_service`
   driver for both the native Linux UUID and the Apple AD/FA57 UUID.  Peers that
   are not in `peer_allowlist` are rejected before any DMA ring is allocated.
   Native Linux peers must also match `peer_auth_acl`, which binds each admitted
   remote UUID to a pre-shared key used during native control-plane
   authentication.

2. **Path and ring setup** (`path.c`, `rail.c`).  For each accepted peer the
   module negotiates a set of Thunderbolt paths (one TX + one RX DMA ring per
   lane).  The number of lanes is controlled by the `lanes=` module parameter.
   Each ring pair is tracked as a *rail*.

3. **Wire protocols** (`native.c`, `apple.c`, `backend.c`).  The native Linux
   transport framing lives in `native.c`; the Apple-compatible path in
   `apple.c`.  Both funnel into the shared send/receive engine in `backend.c`
   which handles zero-copy, scatter-gather, reliability, and PSN sequencing.
   Native rails are not considered data-ready until the peer completes the PSK
   handshake and establishes a per-peer authenticated session.

4. **IB device registration** (`ibdev.c`).  After the transport is ready the
   module calls `ib_register_device()` and exposes a single-port InfiniBand
   device (`usb4_rdma0`, `usb4_rdma1`, …).  The verbs layer sits on top of the
   rail layer: each Queue Pair maps to one or more rails depending on the
   striping configuration.

### Module parameters

The most important parameters at load time:

| Parameter | Values | Effect |
|-----------|--------|--------|
| `profile` | `linux_perf`, `mac_compat`, `mixed`, `auto` | Select transport |
| `bind_services` | `0` / `1` | Enable service driver registration |
| `allocate_rings` | `0` / `1` | Allocate DMA rings on probe |
| `start_rings` | `0` / `1` | Enable DMA on allocated rings |
| `register_verbs` | `0` / `1` | Expose the ib_device to userspace |
| `lanes` | `auto`, `N`, `MIN-MAX` | Lane count for DMA ring allocation |
| `peer_allowlist` | comma-separated UUIDs | Restrict which remote peers are accepted |
| `peer_auth_acl` | `uuid=32hexpsk[,uuid=32hexpsk...]` | Native peer ACL + PSK handshake material |

Run `make -C kernel help` for the full list.

## Userspace provider

The userspace provider lives under `userspace/usb4_rdma/` and is built as
`libusb4_rdma-rdmav*.so`.  It is discovered by `libibverbs` through the
`usb4_rdma.driver` configuration file.

The provider matches devices by the fixed node GUIDs embedded in `ibdev.c`
(`0x0200544256524253` for Linux devices, `0x0200544256524254` for Apple).
It implements the thin `ibv_ops` table required by rdma-core:

- Management: `query_device`, `query_port`, `alloc_pd`, `dealloc_pd`
- Memory: `reg_mr`, `dereg_mr`
- Queues: `create_cq`, `destroy_cq`, `create_qp`, `modify_qp`, `destroy_qp`
- Data path: `post_send`, `post_recv`, `poll_cq`

All operations are forwarded to the kernel via the standard `uverbs` ioctl
interface — the provider contains no custom data-plane code.

## Data path (native Linux transport)

```
post_send (userspace)
  │
  ▼
uverbs ioctl → kernel ibdev.c tbv_post_send()
  │
  ▼
rail.c — select rail(s), handle WR striping
  │
  ▼
native.c — frame header, CRC, PSN assignment
  │
  ▼
backend.c — scatter-gather, zero-copy threshold check
  │
  ▼
tb_ring TX DMA → cable → remote NHI RX DMA
  │
  ▼
backend.c (remote) — reassemble, reorder window
  │
  ▼
native.c (remote) — validate CRC, deliver to QP
  │
  ▼
poll_cq (remote userspace)
```

RDMA READ and WRITE verbs additionally involve a request/response exchange
handled within `native.c` and `native_control.c` before the completion is
posted.

## Debugfs

With the module loaded, live counters are available under:

```
/sys/kernel/debug/thunderbolt_ibverbs/summary
/sys/kernel/debug/thunderbolt_ibverbs/<peer>/rail/<N>/stats
```

These are the canonical source of truth for whether RDMA traffic is actually
flowing through the Thunderbolt rings rather than falling back to a software
path.
