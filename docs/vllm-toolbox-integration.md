# Strix Halo vLLM Toolbox Integration

This guide explains how to use the
[`kyuz0/amd-strix-halo-vllm-toolboxes`](https://github.com/kyuz0/amd-strix-halo-vllm-toolboxes)
`rdma_cluster` toolbox as the cluster orchestration and inference layer while
using **thunderbolt-ibverbs** as the RDMA communication substrate — in place of
the toolbox's default Intel E810 RoCE NIC.

> **Research driver warning.** The thunderbolt-ibverbs README states: *"this is
> a research driver. It is buggy, it is insecure, it is not for production."*
> The `peer_auth_acl` PSK is the current hardening boundary — treat the link as
> trusted-LAN-only. See [SECURITY.md](SECURITY.md) for the full threat model.

---

## Overview — why it composes

The toolbox separates the data plane (Ray orchestration + RCCL collectives →
`libibverbs`) from the transport (NIC/link). `thunderbolt-ibverbs` plugs into
the exact same boundary the E810 occupies: a standard libibverbs RDMA device
(`usb4_rdma*`). Only the provider, kernel module, and physical link change;
Ray, RCCL, and `libibverbs` are unchanged.

| Layer | E810 path (toolbox default) | thunderbolt-ibverbs path |
|---|---|---|
| Orchestration | Ray | Ray (unchanged) |
| Collectives | RCCL (`librccl.so`) | RCCL (unchanged) |
| Verbs API | `libibverbs.so` | `libibverbs.so` (unchanged) |
| Provider | in-box `irdma` | `usb4-rdma-provider` (drop-in) |
| Kernel module | `ice` + `irdma` | `thunderbolt_ibverbs` |
| Physical link | E810 + DAC cable | USB4/Thunderbolt cable |

Three pieces of evidence that this is an intended path:

1. The toolbox's `refresh_toolbox.sh` auto-detects `/dev/infiniband` and adds
   `--device /dev/infiniband --group-add rdma --ulimit memlock=-1` — exactly
   the container flags this repo's README documents.
2. This repo's README documents dropping the `usb4-rdma-provider` into a stock
   container so `ibv_devices` enumerates `usb4_rdma*`, and states "NCCL / UCX /
   perftest inside the container then see `usb4_rdma*` as a normal IB device."
3. The toolbox cluster scripts set the same NCCL/RCCL IB env vars
   (`NCCL_IB_DISABLE=0`, `NCCL_IB_GID_INDEX`, `NCCL_SOCKET_IFNAME`) that this
   repo's own `userspace/bench/tbv_vllm_smoke.sh` sets.

### Two required adjustments

1. **Interface auto-detection**: `configure_cluster.sh` and
   `cluster_manager.py` derive the network interface via
   `ip -o addr show to <subnet>` from a `/24` and fall back to `eth0`. You must
   assign the Thunderbolt netdev IPs in the `192.168.2.0/24` range so detection
   resolves to `thunderbolt0`.
2. **HCA selection**: the toolbox sets `NCCL_IB_GID_INDEX=1` but never sets
   `NCCL_IB_HCA`. With multiple HCAs (this repo references up to `usb4_rdma5`),
   RCCL may bind the wrong device. Export `NCCL_IB_HCA=usb4_rdmaN` (and
   `RCCL_IB_HCA`) explicitly, matching the actual device from `ibv_devices`.

---

## Deployment model — host-DKMS vs container-DKMS

The kernel module and the userspace provider live in **different places**:

| Component | Where it runs | Recommended install |
|---|---|---|
| `thunderbolt_ibverbs` kernel module | **Host only** | Host DKMS (`thunderbolt-ibverbs-dkms` package or `sudo make dkms-*`) |
| `usb4-rdma-provider` (libibverbs plugin) | **Host and container** | Host package; copy into the container if its libibverbs can't see the host's |

**Host-DKMS is the blessed model and the only one needed for a working
cluster.** Loading a module, supplying the `peer_auth_acl` PSK, and configuring
`/etc/modprobe.d/` are host operations regardless of how you run vLLM, so the
module is always built and loaded on the host (kernel ≥ 6.14). The container
only needs the userspace provider so its `libibverbs` enumerates `usb4_rdma*`.

Building the module **inside** a container via DKMS is possible (bind-mount
`/lib/modules`, install `kernel-devel-$(uname -r)` on the host, run the
container `--privileged`), but it is an **opt-in convenience**, not the
recommended path — the loaded module still affects the host kernel. Prefer host
DKMS unless you have a specific reason to build in the container.

> The `dkms-*` Makefile targets derive their version from `dkms.conf`, so
> `sudo make dkms-add dkms-build dkms-install` always builds the version the
> packaging declares.

---

## Prerequisites

- **Linux 6.14+** on both nodes. The toolbox's tested Fedora 43 / 6.18.x
  kernels satisfy this. Verify with `uname -r`.
- This repo's DKMS module (`thunderbolt-ibverbs-dkms`) and
  `usb4-rdma-provider` installed on both hosts.
- A USB4/Thunderbolt cable connected directly between the two nodes (no switch).
- Peer enumerated under `/sys/bus/thunderbolt/devices/` on both sides.

---

## Phase 1 — Install host packages

On each node:

```sh
sudo dnf install dkms gcc git kernel-devel kernel-headers kmod make \
    rdma-core perftest libibverbs-utils

sudo dnf install \
    ./thunderbolt-ibverbs-dkms-<ver>-1.noarch.rpm \
    ./usb4-rdma-provider-<ver>-1.x86_64.rpm
```

Verify the Thunderbolt peer is enumerated after connecting the cable:

```sh
ls /sys/bus/thunderbolt/devices/
# expect domain0/, 0-0, 0-1, ...
```

---

## Phase 2 — Load the module and authenticate peers

### 2.1 Capture each host's Thunderbolt UUID

Run on each node:

```sh
cat /sys/bus/thunderbolt/devices/0-1/unique_id
```

Record both UUIDs — each node's config lists the **other** node's UUID.

### 2.2 Generate a shared PSK

Generate one 16-byte PSK (32 hex chars) and copy it to both hosts:

```sh
openssl rand -hex 16
# example: 00112233445566778899aabbccddeeff
```

### 2.3 Persist the module configuration

Native peer authentication via `peer_auth_acl` is required — without it native
peers are rejected before a usable RDMA session is published (see
[TROUBLESHOOTING.md](TROUBLESHOOTING.md#symptom-native-peer-authentication-rejects-an-expected-peer)).
Both sides must share the same PSK.

A ready-to-edit drop-in ships at
[`packaging/modprobe.d/thunderbolt-ibverbs.conf.example`](../packaging/modprobe.d/thunderbolt-ibverbs.conf.example)
— copy it to `/etc/modprobe.d/thunderbolt-ibverbs.conf` on both nodes and fill
in the two placeholders (the peer UUID and the shared PSK):

**Node 1** `/etc/modprobe.d/thunderbolt-ibverbs.conf`:

```text
options thunderbolt_ibverbs profile=linux_perf bind_services=1 allocate_rings=1 start_rings=1 negotiate_native=1 enable_tunnels=1 register_verbs=1 roce_netdev=thunderbolt0 peer_auth_acl=<NODE2_UUID>=<32hexpsk>
```

**Node 2** `/etc/modprobe.d/thunderbolt-ibverbs.conf` (same PSK, Node 1's UUID):

```text
options thunderbolt_ibverbs profile=linux_perf bind_services=1 allocate_rings=1 start_rings=1 negotiate_native=1 enable_tunnels=1 register_verbs=1 roce_netdev=thunderbolt0 peer_auth_acl=<NODE1_UUID>=<32hexpsk>
```

Key parameters explained:

| Parameter | Purpose |
|---|---|
| `profile=linux_perf` | Fastest native Linux↔Linux path |
| `register_verbs=1` | Required — without it `ibv_devices` shows no `usb4_rdma*` |
| `negotiate_native=1` + `enable_tunnels=1` | Native HELLO + Thunderbolt path setup |
| `roce_netdev=thunderbolt0` | Supplies a netdev for RoCE GID metadata |
| `peer_auth_acl=<uuid>=<32hex>` | Authorizes the remote UUID and supplies the session PSK |

See [MODULE_PARAMETERS.md](MODULE_PARAMETERS.md) for the full parameter catalog.

### 2.4 Load and verify

On both nodes:

```sh
sudo modprobe thunderbolt_ibverbs
dmesg | grep thunderbolt_ibverbs   # expect: "peer <UUID>: native path ready, N lanes"
ibv_devices                         # expect: usb4_rdma0 (or higher index)
rdma link
```

> ⚠️ Record the exact device name (e.g. `usb4_rdma0`) — you need it for
> `NCCL_IB_HCA` in Phase 6.

---

## Phase 3 — Host networking

Bring up the Thunderbolt netdev with static IPs so the toolbox's subnet-based
interface detection resolves to `thunderbolt0`.

**Node 1 (Head) — `192.168.2.1/24`:**

```sh
sudo nmcli connection add type ethernet ifname thunderbolt0 con-name thunderbolt0 \
    ipv4.method manual ipv4.addresses 192.168.2.1/24 ipv4.gateway "" \
    802-3-ethernet.mtu 9000
sudo nmcli connection up thunderbolt0
sudo firewall-cmd --permanent --zone=trusted --add-interface=thunderbolt0
sudo firewall-cmd --reload
```

**Node 2 (Worker) — `192.168.2.2/24`:** same commands, replace the address.

> MTU 9000 (jumbo frames) may be unsupported on some Thunderbolt host
> controllers. Drop to 1500 if `nmcli connection up` fails.

---

## Phase 4 — Passwordless SSH

The toolbox's `cluster_manager.py` SSHes into the worker to start Ray. SSH must
use the same `192.168.2.x` addresses the cluster uses:

```sh
ssh-keygen -t ed25519          # if you don't already have a key
ssh-copy-id 192.168.2.2        # Node 1 → Node 2
ssh-copy-id 192.168.2.1        # Node 2 → Node 1
ssh 192.168.2.2 date           # must print date with no password prompt
```

---

## Phase 5 — Install and verify the toolbox

On both nodes:

```sh
git clone https://github.com/kyuz0/amd-strix-halo-vllm-toolboxes.git
cd amd-strix-halo-vllm-toolboxes
./refresh_toolbox.sh            # choose "latest"
```

Because `/dev/infiniband` now exists, the script auto-adds RDMA flags. Confirm
it prints:

```text
🔎 InfiniBand devices detected! Adding RDMA support...
```

### 5.1 Verify the device is visible inside the container

```sh
toolbox run -c vllm -- ibv_devices
# must list usb4_rdma0
```

> If `ibv_devices` is empty inside the container but works on the host, the
> `usb4-rdma-provider` isn't visible to the container's libibverbs. Use the
> helper to copy the host provider in:
>
> ```sh
> tools/ci/install-provider-into-container.sh <container-name>
> ```
>
> It copies the `.driver` hint into the container's `/etc/libibverbs.d/` and the
> `libusb4_rdma-rdmav*.so` into its libibverbs dir. If devices are still missing
> afterward, the container's libibverbs PABI differs from the host's — rebuild
> the provider against the container's rdma-core (see
> [§ Provider PABI](#provider-pabi-fedora-43) below).

### 5.2 Provider PABI (Fedora 43)

The `usb4-rdma-provider` is a **libibverbs plugin**, so its private ABI (PABI)
must match the `libibverbs` it loads against. The provider is built from
upstream rdma-core pinned by `RDMA_CORE_TAG` (default `v62.0`) in
[`tools/ci/distro-package-rdma.sh`](../tools/ci/distro-package-rdma.sh).

- **Host install (RPM/DEB):** the provider package is built against the same
  rdma-core the host’s `libibverbs` ships, so no tuning is needed.
- **Container use:** the toolbox image is **Fedora 43**. If the container’s
  stock `libibverbs` PABI differs from `v62.0`, copying the host `.so` in (5.1)
  may load but fail to enumerate. In that case rebuild the provider against the
  container’s rdma-core by setting `RDMA_CORE_TAG` to the tag matching Fedora
  43’s `libibverbs` before running the builder:

  ```sh
  # Find the rdma-core version Fedora 43 ships, then pin the matching tag:
  dnf info libibverbs | grep -i version
  RDMA_CORE_TAG=v<matching-tag> tools/ci/distro-package-rdma.sh fedora
  ```

  Install the resulting `usb4-rdma-provider-*.rpm` (or copy its `.so` +
  `.driver` in with the 5.1 helper). Pin one `RDMA_CORE_TAG` per Fedora release
  so the provider PABI tracks the container’s `libibverbs`.

---

## Phase 6 — Toolbox configuration

### 6.1 Validate the link with perftest

The bundled `compare_eth_vs_rdma.sh -i` benchmark hardcodes
`HOST_ROCE=192.168.100.2` and auto-picks the third `ibv_devices` row, so it
won't match a `192.168.2.x` / `usb4_rdma0` setup. Validate with perftest
directly instead:

```sh
# Node 2 (server):
ib_write_bw -d usb4_rdma0

# Node 1 (client):
ib_write_bw -d usb4_rdma0 192.168.2.2
```

Then confirm RDMA counters moved:

```sh
cat /sys/kernel/debug/thunderbolt_ibverbs/summary
```

### 6.2 Point the cluster at the Thunderbolt subnet

Export the head and worker IPs before launching the TUI so that interface
auto-detection resolves to `thunderbolt0`:

```sh
export VLLM_HEAD_IP=192.168.2.1
export VLLM_WORKER_IP=192.168.2.2
start-vllm-cluster
```

In the TUI choose **Option 1 (Configure IPs)** and set Head `192.168.2.1`,
Worker `192.168.2.2`. The scripts compute `RDMA_IFACE` via
`ip -o addr show to 192.168.2.0/24`, which returns `thunderbolt0`, and
export `NCCL_SOCKET_IFNAME=thunderbolt0` automatically.

### 6.3 Set the correct HCA — the critical missing piece

The stock toolbox scripts never set `NCCL_IB_HCA`. Export it before launching:

```sh
export NCCL_IB_HCA=usb4_rdma0       # match the device name from Phase 2.4
export RCCL_IB_HCA=usb4_rdma0
export NCCL_IB_DISABLE=0            # keep RDMA path on — do NOT enable "Force Ethernet"
# NCCL_IB_GID_INDEX=1 and NCCL_NET_GDR_LEVEL=0 are already set by the toolbox
```

`NCCL_NET_GDR_LEVEL=0` is the **host-staging** (safe default) mode: RCCL
copies GPU buffers through host memory before handing them to the RDMA
transport. This works on any configuration regardless of the `gpu_direct`
kernel module parameter.

If env-exporting before the TUI does not propagate to Ray workers, add these
lines to the toolbox's `scripts/cluster_manager.py` `setup_head_node` /
`setup_worker_node` functions, alongside the existing `NCCL_SOCKET_IFNAME`
exports. Minimal patch:

```sh
# Add to scripts/cluster_manager.py (setup_head_node / setup_worker_node),
# after the existing NCCL_SOCKET_IFNAME export lines:

export NCCL_IB_HCA=usb4_rdma0
export RCCL_IB_HCA=usb4_rdma0
```

Similarly, add to `scripts/configure_cluster.sh` after the existing
`NCCL_IB_GID_INDEX` line:

```sh
# Add to scripts/configure_cluster.sh, after the NCCL_IB_GID_INDEX export:

export NCCL_IB_HCA=usb4_rdma0
export RCCL_IB_HCA=usb4_rdma0
```

> Do **not** vendor or copy large portions of the toolbox's files — the two
> `export` lines above are the only additions required.

### 6.3.1 Host-staging mode (default, always supported)

`NCCL_NET_GDR_LEVEL=0` + `gpu_direct=off` (or compiled out) is the
out-of-the-box configuration. RCCL / rocSHMEM stage GPU buffers through host
memory; the driver uses its standard `ib_umem` copy path.

```sh
# Host-staging — no special module parameter needed:
export NCCL_IB_HCA=usb4_rdma0
export RCCL_IB_HCA=usb4_rdma0
export NCCL_IB_DISABLE=0
export NCCL_NET_GDR_LEVEL=0   # force host-staging (already the toolbox default)
```

`ibv_reg_dmabuf_mr()` returns `EOPNOTSUPP` in this mode; RCCL/rocSHMEM treat
that as "GPU-direct unavailable" and fall back to host-staging automatically.

### 6.3.2 GPU-direct opt-in mode (requires `gpu_direct` enabled)

When the kernel module is built with `CONFIG_TBV_GPU_DIRECT=y` (out-of-tree:
`make tbv_gpu_direct=1`) and loaded with `gpu_direct=auto` or `gpu_direct=on`,
`ibv_reg_dmabuf_mr()` succeeds for ROCm unified-memory buffers. RCCL can then
use `NCCL_NET_GDR_LEVEL ≥ 1` to register GPU buffers directly as RDMA MRs,
avoiding the GPU→CPU staging copy.

**Prerequisites:**

- Kernel module built with GPU-direct support and loaded with
  `gpu_direct=auto` or `gpu_direct=on` (see [MODULE_PARAMETERS.md](MODULE_PARAMETERS.md)).
- ROCm ≥ 6.0 on all nodes (`hipMemGetHandleForAddressRange` with dmabuf fd).
- Linux kernel ≥ 6.2 on all nodes (amdgpu unified-memory in ZONE_NORMAL).

**modprobe configuration** (add `gpu_direct=auto` to the existing line):

```text
options thunderbolt_ibverbs profile=linux_perf ... peer_auth_acl=<UUID>=<PSK> gpu_direct=auto
```

**Pinned vs dynamic dma-buf import (`gpu_direct_dynamic`):**

By default GPU-direct MRs are *pinned* for their lifetime, which can block the
GPU allocator from reclaiming long-lived buffers. Adding `gpu_direct_dynamic=1`
switches to the dynamic move-notify import (Phase 4): the GPU driver may
migrate/reclaim the buffer while the MR is live, and the transport re-maps the
pages under the dma-buf reservation lock for each transfer. This is the
recommended setting for long-lived MRs (e.g. persistent KV-cache registrations)
to avoid pin pressure. The data path is otherwise identical (still a bounded CPU
copy — dma-buf MRs are never streamed directly from the NHI ring), so it remains
safe on migration. Leave it at `0` (pinned) if you prefer the simpler Phase 1
behaviour or register/deregister per transfer.

```text
options thunderbolt_ibverbs profile=linux_perf ... gpu_direct=auto gpu_direct_dynamic=1
```

**Environment for GPU-direct mode:**

```sh
export NCCL_IB_HCA=usb4_rdma0
export RCCL_IB_HCA=usb4_rdma0
export NCCL_IB_DISABLE=0
export NCCL_NET_GDR_LEVEL=1   # enable GPU-direct (requires gpu_direct=auto|on)
# rocSHMEM GDA transport (optional — enables ROCSHMEM dmabuf path):
export RCCL_ROCSHMEM_ENABLE=1
export ROCSHMEM_GDA_PROVIDER=ib
export ROCSHMEM_GDA_ENABLE_DMABUF=1
```

If `gpu_direct` is off or unavailable, `ibv_reg_dmabuf_mr()` returns
`EOPNOTSUPP` and RCCL/rocSHMEM automatically fall back to host-staging. There
is no hard-fail: switching back to `NCCL_NET_GDR_LEVEL=0` or setting
`gpu_direct=off` and reloading the module restores the fully supported
host-staging path immediately.

**Mode summary:**

| `gpu_direct` param | `NCCL_NET_GDR_LEVEL` | `ibv_reg_dmabuf_mr()` | Data path |
|---|---|---|---|
| `off` / compiled out | 0 | `EOPNOTSUPP` | Host-staging (current default) |
| `auto` / `on` (support absent) | 0 | `EOPNOTSUPP` | Host-staging fallback — same behavior as row 1 |
| `auto` / `on` (support present) | 0 | success (not used) | Host-staging |
| `auto` / `on` (support present) | ≥ 1 | success | GPU-direct dmabuf MR |

### 6.4 Start the cluster and launch inference

1. **Option 2 → START CLUSTER** — set Force Ethernet = **NO**. Node 1 starts
   the Ray head; the toolbox SSHes Node 2 to start the worker.
2. **Option 4 (Ray Status)** — confirm **2 nodes**, `2.0 GPU`.
3. **Option 5 → select model** — set **Tensor Parallelism = 2**, **Force Eager
   Mode = YES** (CUDA-graph capture deadlocks on distributed APU clusters),
   **Erase vLLM Cache = YES** on first run, then **LAUNCH SERVER**.

For gated models, export the HF token before launching:

```sh
export HF_TOKEN=your_token_here
```

---

## Per-host configuration summary

| Item | Node 1 (Head) | Node 2 (Worker) |
|---|---|---|
| OS / Kernel | Fedora 43 / ≥ 6.14 (6.18.x tested) | Fedora 43 / ≥ 6.14 |
| Thunderbolt IP | `192.168.2.1/24`, MTU 9000 | `192.168.2.2/24`, MTU 9000 |
| RDMA device | `usb4_rdma0` (from `ibv_devices`) | `usb4_rdma0` |
| Module profile | `linux_perf` | `linux_perf` |
| `peer_auth_acl` | `<NODE2_UUID>=<shared PSK>` | `<NODE1_UUID>=<shared PSK>` |
| Firewall | `thunderbolt0` → trusted zone | `thunderbolt0` → trusted zone |
| SSH | passwordless to Node 2 | passwordless to Node 1 |
| `NCCL_IB_HCA` | `usb4_rdma0` | `usb4_rdma0` |

---

## Caveats

- **Research-grade driver.** The README states: *"this is a research driver. It
  is buggy, it is insecure, it is not for production."* The `peer_auth_acl` PSK
  is the current hardening boundary.
- **Performance expectation.** Native `usb4_rdma` is lower latency than
  RXE-over-`thunderbolt-net` and onboard ethernet, but slower than real RDMA
  hardware such as the E810. Reported result: ~20 tok/s on a 230B MoE (TP=2),
  ~30% faster than TCP-over-Thunderbolt.
- **Multi-rail.** The README references up to `usb4_rdma5` (multiple HCAs /
  cables). If you wire multiple USB4 cables, pass a comma-separated list to
  `NCCL_IB_HCA` and tune `native_fragment_striping=1` / `lanes=` accordingly.
- **Stability environment variables** (optional, mirrors the toolbox's RDMA
  hardening): export `NCCL_IB_TIMEOUT=23` and `NCCL_IB_RETRY_CNT=7`.
- **"Force Ethernet" must be OFF.** The toolbox TUI's "Force Ethernet" toggle
  sets `NCCL_IB_DISABLE=1`, disabling the RDMA verbs path. Leave it unset (the
  default) so RCCL uses `usb4_rdma*`.

---

## References

- Toolbox repository: <https://github.com/kyuz0/amd-strix-halo-vllm-toolboxes>
- Toolbox setup guide: `rdma_cluster/setup_guide.md` in the toolbox repo
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — symptom-by-symptom checklist
- [MODULE_PARAMETERS.md](MODULE_PARAMETERS.md) — full `thunderbolt_ibverbs` parameter catalog
- [README.md](../README.md) — container usage and `usb4-rdma-provider` installation
- [`packaging/modprobe.d/thunderbolt-ibverbs.conf.example`](../packaging/modprobe.d/thunderbolt-ibverbs.conf.example) — ready-to-edit module config drop-in
- [`tools/ci/install-provider-into-container.sh`](../tools/ci/install-provider-into-container.sh) — copy the host provider into a running container
