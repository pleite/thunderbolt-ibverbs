# Performance Tuning Guide

This page covers the three module parameters that most affect
`thunderbolt_ibverbs` bandwidth and latency, with recommended values and
guidance on when to change them.  The numbers come from the parametric sweep
documented in
[`bench/results/strix-2p-noiommu-2x40g/tuning.md`](../bench/results/strix-2p-noiommu-2x40g/tuning.md).

---

## Quick reference

```sh
# Throughput-optimised load (recommended for ML workloads):
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  native_fragment_striping=0   # set 1 if you have ≥4 rails

# Low-latency load (HPC / tightly-coupled compute):
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=0 \
  native_fragment_striping=0
```

To make a configuration persistent across reboots:

```sh
cat > /etc/modprobe.d/thunderbolt-ibverbs.conf <<'EOF'
options thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000
EOF
```

Parameters can also be changed at runtime without reloading (they are `0644`):

```sh
# Disable throttling on-the-fly:
echo 0 | sudo tee /sys/module/thunderbolt_ibverbs/parameters/nhi_interrupt_throttle_ns
# Re-enable at 50 µs:
echo 50000 | sudo tee /sys/module/thunderbolt_ibverbs/parameters/nhi_interrupt_throttle_ns
```

`native_fragment_striping` requires a module reload — it controls ring
allocation at path-open time.

---

## `nhi_interrupt_throttle_ns`

**Type**: `uint`, writable at runtime (`0644`).  
**Default**: `0` (throttling disabled).  
**Recommended for throughput**: `50000` (50 µs).

### What it does

Sets the NHI interrupt coalescing interval on the Thunderbolt data rings
(both TX and RX) by calling `tb_ring_throttling()` in the kernel.  With
coalescing active, the NHI fires one interrupt per interval rather than one
per DMA frame, reducing per-packet interrupt overhead.

> **Kernel dependency**: `tb_ring_throttling()` was added to the USB4/Thunderbolt
> maintainer tree.  If your running kernel does not export this symbol, the
> parameter is accepted but ignored (a `pr_warn_once` note appears in `dmesg`).
> Check: `grep tb_ring_throttling /proc/kallsyms`.

### Tuning guidance

| Value | Effect |
|---|---|
| `0` (default) | Minimum latency; maximum interrupt rate; best for latency-critical workloads |
| `25000` (25 µs) | Mild coalescing; ~6% BW gain; ~0.3 µs added latency |
| `50000` (50 µs) | Sweet spot for throughput; ~10% BW gain; ~0.5 µs added latency |
| `100000` (100 µs) | Marginal extra BW vs 50 µs; ~1 µs added latency |
| `200000` (200 µs) | Diminishing BW returns; ~2.4 µs added latency; not recommended |

**Rule of thumb**: use `50000` for ML inference/training and file transfer.
Use `0` for MPI-style all-reduce or any workload where small-message latency
dominates.

### How to measure the effect on your hardware

```sh
# Server side:
ib_write_lat -d usb4_rdma0 -x 1 -s 64 -n 1000

# Client side:
ib_write_lat -d usb4_rdma0 -x 1 -s 64 -n 1000 <server-ip>

# Then compare with throttle enabled:
echo 50000 | sudo tee /sys/module/thunderbolt_ibverbs/parameters/nhi_interrupt_throttle_ns
```

---

## `native_fragment_striping`

**Type**: `bool`, load-time only.  
**Default**: `0` (off).  
**Recommended for ≥4-rail topologies**: `1`.

### What it does

When enabled, the driver distributes the 4 KiB DMA frames of each large SEND
message across all active native rails in round-robin order instead of sending
all frames of a message on the same rail.  This saturates multiple rails for a
single large transfer rather than requiring multiple QPs.

### Tuning guidance

| Value | When to use |
|---|---|
| `0` (default) | 1 or 2 rails; latency-sensitive; or when reliability (retransmit) is more important than peak BW |
| `1` | 4-rail topology (two Thunderbolt cables); pure throughput workloads; `ib_write_bw` / RDMA WRITE saturating a single rail |

**Important**: fragment striping applies only to SEND operations. For RDMA
WRITE, the verbs layer chooses a path per QP at open time, so striping is most
effective when your workload issues many SENDs or uses multiple QPs.  RC WRITE
zero-copy (`zcopy_min_bytes > 0`) uses a single path per WRITE to preserve
stream ordering; striping is automatically suppressed in that case.

**With SRQ**: `odd.srq.ib_send_bw` currently fails on the native transport
with striping enabled (SRQ + 4 QPs) — leave `native_fragment_striping=0` if
your application uses SRQ.

### Sweep result (strix-2p-noiommu-2x40g, four rails)

| `native_fragment_striping` | bw_avg_gbps (1 MiB, qps=4) | lat_typical_us (64 B) |
|---|---:|---:|
| `0` | 10.43 | 7.89 |
| `1` | 11.17 | 8.14 |

~7% BW gain at ~0.25 µs latency cost.

---

## `zcopy_min_bytes`

**Type**: `uint`, writable at runtime (`0644`).  
**Default**: `4096` (4 KiB).  
**Recommended**: `4096` for all workloads.

### What it does

Sets the message size threshold above which the driver attempts raw zero-copy
streaming instead of copying the payload into the 4 KiB DMA frame pool.  For
an RC WRITE, zero-copy streams the user buffer's pages directly into the NHI
ring without an intermediate kernel copy.  If the streaming attempt fails
(e.g. due to page-alignment constraints or ring pressure), the driver falls
back to framed copies — there is no data loss.

Setting `zcopy_min_bytes=0` disables zero-copy entirely; every message goes
through framed copies regardless of size.

### Tuning guidance

| Value | Effect |
|---|---|
| `0` | Zero-copy disabled; all messages copied into 4 KiB frames; safer but ~17% slower at 1 MiB |
| `4096` (default) | Zero-copy for messages ≥ 4 KiB; recommended; virtually no cost below threshold |
| `16384` | Same peak throughput as 4096; delays the mode switch to 16 KiB messages |
| `65536` | Same peak throughput as 4096 beyond 64 KiB; framed copies for 4–64 KiB range |

**Diagnostic counters** — check fallback rate in `debugfs`:

```sh
cat /sys/kernel/debug/thunderbolt_ibverbs/summary | grep zcopy
# data_wr_zcopy:                    <n>   ← zero-copy succeeded
# data_wr_zcopy_fallback:           <n>   ← fell back to framed copy
# data_wr_zcopy_fallback_striping:  <n>   ← striping suppressed zcopy
# data_wr_zcopy_fallback_unsafe_sge: <n>  ← non-contiguous SGE
```

If `data_wr_zcopy_fallback` is high relative to `data_wr_zcopy`, your
workload has many non-contiguous SGEs or misaligned buffers; consider setting
`zcopy_min_bytes=0` to avoid the fallback overhead.

### Sweep result (strix-2p-noiommu-2x40g, four rails)

| `zcopy_min_bytes` | bw_avg_gbps (1 MiB, qps=4) | lat_typical_us (64 B) |
|---|---:|---:|
| `0`     | 8.91  | 7.82 |
| `4096`  | 10.43 | 7.89 |
| `16384` | 10.51 | 7.91 |
| `65536` | 10.47 | 7.87 |

Zero-copy at 4 KiB threshold gives the full benefit; thresholds above 4 KiB
offer no additional gain.

---

## Reproducing the sweep

See
[`bench/results/strix-2p-noiommu-2x40g/tuning.md`](../bench/results/strix-2p-noiommu-2x40g/tuning.md)
for the full recreate command and all raw numbers.  The sweep script is at
`userspace/bench/tbv_tuning_sweep.py` and is designed to run against the same
hardware topology used by the full `tbv-perftest` suite.

---

## See also

- [`README.md — Useful Parameters`](../README.md#useful-parameters) — full parameter list.
- [`bench/README.md`](../bench/README.md) — how benchmarks are stored and reproduced.
- [`docs/roadmap/steps/05-performance-tuning.md`](roadmap/steps/05-performance-tuning.md) — roadmap context.
