# Performance Tuning Guide

This page covers the three module parameters that most affect
`thunderbolt_ibverbs` bandwidth and latency, with recommended values and
guidance on when to change them.

**⚠️ Important caveat:** The recommendations below are based on parametric
sweeps on **strix-2p-noiommu-2x40g** (a four-rail, dual-socket NUMA system).
**Results may not apply to other hardware**, particularly single/dual-rail UMA
systems or different generation Thunderbolt controllers. Always test on your
target hardware before deploying to production.

The full sweep methodology is documented in
[`bench/results/strix-2p-noiommu-2x40g/tuning.md`](../bench/results/strix-2p-noiommu-2x40g/tuning.md).

---

## Quick reference

```sh
# Throughput-optimised load (recommended for ML workloads on multi-rail systems):
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

# Conservative defaults for Strix Halo or other UMA-heavy systems (test before deploying):
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  zcopy_min_bytes=0 \
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
**Recommended for throughput (multi-rail NUMA systems)**: `50000` (50 µs).  
**Recommended for single/dual-rail or UMA systems**: `0` (test empirically).

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
| `50000` (50 µs) | Sweet spot for throughput on multi-rail NUMA; ~10% BW gain; ~0.5 µs added latency |
| `100000` (100 µs) | Marginal extra BW vs 50 µs; ~1 µs added latency |
| `200000` (200 µs) | Diminishing BW returns; ~2.4 µs added latency; not recommended |

**Rule of thumb for multi-rail NUMA (strix-2p-noiommu-2x40g):**
Use `50000` for ML inference/training and file transfer.
Use `0` for MPI-style all-reduce or any workload where small-message latency
dominates.

**⚠️ Note on single/dual-rail or UMA systems (e.g., Strix Halo):**
Interrupt coalescing trades latency for throughput. The upstream maintainer
reported that `50000` ns reduced performance on certain systems because:
- UMA memory means tiny copies aren't expensive.
- Single/dual-rail topologies don't benefit from per-packet interrupt batching
  the same way multi-rail systems do.
- Throttling's ~0.5 µs latency cost may outweigh bandwidth gains on workloads
  dominated by small messages.

**Always benchmark your actual workload** with both `0` and `50000` before
deciding. Use the measurement guide below.

### How to measure the effect on your hardware

```sh
# Server side:
ib_write_lat -d usb4_rdma0 -x 1 -s 64 -n 1000

# Client side:
ib_write_lat -d usb4_rdma0 -x 1 -s 64 -n 1000 <server-ip>

# Compare without throttle:
echo 0 | sudo tee /sys/module/thunderbolt_ibverbs/parameters/nhi_interrupt_throttle_ns
# Re-run latency test

# Then compare with throttle enabled:
echo 50000 | sudo tee /sys/module/thunderbolt_ibverbs/parameters/nhi_interrupt_throttle_ns
# Re-run latency test
```

If your workload shows latency increase > 1 µs or throughput drop with `50000`,
revert to `0`.

---

## `native_fragment_striping`

**Type**: `bool`, load-time only.  
**Default**: `0` (off).  
**Recommended for ≥4-rail NUMA topologies**: `1`.  
**Recommended for single/dual-rail or UMA systems**: `0`.

### What it does

When enabled, the driver distributes the 4 KiB DMA frames of each large SEND
message across all active native rails in round-robin order instead of sending
all frames of a message on the same rail.  This saturates multiple rails for a
single large transfer rather than requiring multiple QPs.

### Tuning guidance

| Value | When to use |
|---|---|
| `0` (default) | 1 or 2 rails; latency-sensitive; UMA systems; when reliability (retransmit) is more important than peak BW |
| `1` | 4-rail topology (two Thunderbolt cables); pure throughput workloads; `ib_write_bw` / RDMA WRITE saturating a single rail |

**Important:** fragment striping applies only to SEND operations. For RDMA
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

~7% BW gain at ~0.25 µs latency cost on four-rail topology.

**⚠️ Note on single/dual-rail systems:** Round-robin overhead can exceed
throughput gains on systems with fewer active rails. Test empirically.

---

## `zcopy_min_bytes`

**Type**: `uint`, writable at runtime (`0644`).  
**Default**: `4096` (4 KiB).  
**Recommended for multi-rail systems**: `4096`.  
**Recommended for UMA-heavy systems (e.g., Strix Halo)**: `0` (disable; test with your workload).

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
| `0` | Zero-copy disabled; all messages copied into 4 KiB frames; ~17% slower at 1 MiB on multi-rail systems; safer for UMA with non-contiguous buffers |
| `4096` (default) | Zero-copy for messages ≥ 4 KiB; recommended for multi-rail; virtually no cost below threshold |
| `16384` | Same peak throughput as 4096; delays the mode switch to 16 KiB messages |
| `65536` | Same peak throughput as 4096 beyond 64 KiB; framed copies for 4–64 KiB range |

**Rule of thumb:**
- **Multi-rail NUMA systems** (strix-2p-noiommu-2x40g): use `4096` for the ~17% BW gain.
- **Single/dual-rail or UMA systems** (e.g., Strix Halo): the upstream maintainer
  recommends starting with `0` because tiny copies are already cheap on UMA and
  the zero-copy fallback path has overhead. Only enable if your benchmarks show
  measurable gain on real workloads.

**Diagnostic counters** — check fallback rate in `debugfs`:

```sh
cat /sys/kernel/debug/thunderbolt_ibverbs/summary | grep zcopy
# data_wr_zcopy:                    <n>   ← zero-copy succeeded
# data_wr_zcopy_fallback:           <n>   ← fell back to framed copy
# data_wr_zcopy_fallback_striping:  <n>   ← striping suppressed zcopy
# data_wr_zcopy_fallback_unsafe_sge: <n>  ← non-contiguous SGE
```

If `data_wr_zcopy_fallback` is high relative to `data_wr_zcopy` (>10%), your
workload has many non-contiguous SGEs or misaligned buffers; consider setting
`zcopy_min_bytes=0` to avoid the fallback overhead.

### Sweep result (strix-2p-noiommu-2x40g, four rails)

| `zcopy_min_bytes` | bw_avg_gbps (1 MiB, qps=4) | lat_typical_us (64 B) |
|---|---:|---:|
| `0`     | 8.91  | 7.82 |
| `4096`  | 10.43 | 7.89 |
| `16384` | 10.51 | 7.91 |
| `65536` | 10.47 | 7.87 |

Zero-copy at 4 KiB threshold gives the full benefit on multi-rail systems;
thresholds above 4 KiB offer no additional gain.

**⚠️ Note:** These results are from a four-rail NUMA system. On UMA or
single/dual-rail systems, the cost/benefit may be different. Always test with
your workload.

---

## Hardware-Specific Tuning Profiles

### Strix 2P (NUMA, four-rail)

**Configuration:**
```sh
zcopy_min_bytes=4096          # enables 17% BW gain at 1 MiB
native_fragment_striping=0    # disable (causes SRQ issues)
nhi_interrupt_throttle_ns=50000  # adds 10% BW; test for latency impact
```

### Strix Halo (UMA, single/dual-rail)

**Configuration (baseline; test before deploying):**
```sh
zcopy_min_bytes=0             # UMA: tiny copies cheap, zero-copy fallback overhead high
native_fragment_striping=0    # disable (single/dual-rail; overhead > gain)
nhi_interrupt_throttle_ns=0   # disable (throttle may hurt UMA latency)
```

**Why?** Strix Halo is a single-socket UMA system. Memory copies are cheap,
and small-message latency dominates on typical ML workloads. The upstream
maintainer reported that the `50000` ns throttle and `4096` zero-copy threshold
reduced performance on Strix Halo.

**Always benchmark your real workload** on both configurations before choosing.

---

## Troubleshooting: When defaults hurt performance

1. **If throughput drops significantly with `nhi_interrupt_throttle_ns=50000`:**
   - Revert to `0` and re-run benchmarks.
   - The throttle trades latency for throughput; if your workload is latency-bound,
     the ~0.5 µs added latency may outweigh the BW gain.

2. **If throughput is disappointing with `zcopy_min_bytes=4096`:**
   - Check debugfs counters:
     ```sh
     cat /sys/kernel/debug/thunderbolt_ibverbs/summary | grep zcopy
     ```
   - If `data_wr_zcopy_fallback` is high (>10% of total), your buffers are
     non-contiguous or misaligned. Set `zcopy_min_bytes=0` to skip the fallback overhead.

3. **If you have only one or two rails:**
   - Disable `native_fragment_striping` (`striping=0`).
   - The round-robin overhead dominates on fewer lanes.

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
