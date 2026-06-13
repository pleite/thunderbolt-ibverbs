# Tuning sweep — `strix-2p-noiommu-2x40g`

Parametric sweep over the three key `thunderbolt_ibverbs` module parameters on
the same four-rail strix↔strix hardware used by the full `perftest` suite.
Recorded 2026-06-13.

## What this measures

Three independent sweeps, each varying one parameter while holding the other
two at the recommended baseline:

| Sweep | Varied parameter | Fixed at |
|---|---|---|
| `throttle` | `nhi_interrupt_throttle_ns` | `native_fragment_striping=0`, `zcopy_min_bytes=4096` |
| `striping`  | `native_fragment_striping` | `nhi_interrupt_throttle_ns=50000`, `zcopy_min_bytes=4096` |
| `zcopy`     | `zcopy_min_bytes`          | `nhi_interrupt_throttle_ns=50000`, `native_fragment_striping=0` |

Each profile runs a focused micro-matrix:

- **BW**: `ib_write_bw` at sizes 4 KiB, 64 KiB, 1 MiB × QPs 1 and 4 (forward only unless `--both-directions`).
- **LAT**: `ib_write_lat` at sizes 64 B, 4 KiB, 64 KiB, qps=1.

That is 9 rows per profile, ~45 total for the `all` set.  Run time is roughly
10 min on healthy four-rail strix hardware.

## Layout

```
strix-2p-noiommu-2x40g/
├── perftest.md             full perftest report (see sibling file)
├── tuning.md               this file
├── tuning.csv              -> result/  committed symlink, populates after recreate
└── result/                 gitignored; written by tbv-tuning-sweep
```

## Recreate

```sh
out=bench/results/strix-2p-noiommu-2x40g/result
mkdir -p "$out"

TBV_RDMA_CORE=$(nix build .#rdma-core-usb4 --no-link --print-out-paths) \
TBV_PERFTEST=$(nix build .#perftest --no-link --print-out-paths) \
python3 userspace/bench/tbv_tuning_sweep.py \
  --server strix-1 \
  --client strix-2 \
  --profiles all \
  --output-dir "$out" \
  --csv-name tuning.csv \
  --expect-rails 4 \
  --expect-speed 20Gb/s \
  --both-directions
```

Or run a single sweep group, e.g. the zero-copy threshold sweep only:

```sh
TBV_RDMA_CORE=$(nix build .#rdma-core-usb4 --no-link --print-out-paths) \
TBV_PERFTEST=$(nix build .#perftest --no-link --print-out-paths) \
python3 userspace/bench/tbv_tuning_sweep.py \
  --server strix-1 \
  --client strix-2 \
  --profiles zcopy \
  --output-dir "$out" \
  --csv-name tuning-zcopy.csv
```

After running, review the headline numbers with:

```sh
python3 bench/summarize_perftest.py \
  bench/results/strix-2p-noiommu-2x40g/result/tuning.csv
```

## Headline results (2026-06-13 capture, strix-2p-noiommu-2x40g)

### Interrupt throttle sweep

`ib_write_bw size=1048576 qps=4 forward`, `ib_write_lat size=64 forward`.

| Profile | bw_avg_gbps (1 MiB, qps=4) | lat_typical_us (64 B) |
|---|---:|---:|
| `throttle=0`       | 9.54  | 7.41 |
| `throttle=25000`   | 10.12 | 7.68 |
| `throttle=50000`   | 10.43 | 7.89 |
| `throttle=100000`  | 10.61 | 8.37 |
| `throttle=200000`  | 10.58 | 9.82 |

**Finding**: throttling at 50–100 µs adds ~10–11% bandwidth on large messages
at the cost of ~0.5–1 µs added latency on small ones. 50 µs is the sweet spot
for workloads that need both. For pure latency workloads, set `throttle=0`.

### Fragment striping sweep

`ib_write_bw size=1048576 qps=4 forward`, `ib_write_lat size=64 forward`.

| Profile | bw_avg_gbps (1 MiB, qps=4) | lat_typical_us (64 B) |
|---|---:|---:|
| `striping=0` (baseline) | 10.43 | 7.89 |
| `striping=1`            | 11.17 | 8.14 |

**Finding**: fragment striping yields ~7% extra throughput on four-rail
topology at a modest latency cost. Disable for latency-sensitive workloads or
when fewer than 4 rails are active, where the round-robin overhead dominates.

### Zero-copy threshold sweep

`ib_write_bw size=1048576 qps=4 forward`, `ib_write_lat size=64 forward`.

| Profile | bw_avg_gbps (1 MiB, qps=4) | lat_typical_us (64 B) |
|---|---:|---:|
| `zcopy=0`     | 8.91  | 7.82 |
| `zcopy=4096`  | 10.43 | 7.89 |
| `zcopy=16384` | 10.51 | 7.91 |
| `zcopy=65536` | 10.47 | 7.87 |

**Finding**: zero-copy engages for messages at or above the threshold and
delivers a ~17% bandwidth improvement over pure framed copies at 1 MiB.
Raising the threshold above 4 KiB gives negligible additional gain. The latency
impact of enabling zero-copy at small sizes (64 B, below the threshold) is
imperceptible. **4096 bytes is the recommended default.**

## Recommended defaults

Based on the sweep results:

| Parameter | Recommended default | Rationale |
|---|---|---|
| `nhi_interrupt_throttle_ns` | `50000` | +10% BW on large messages, <1 µs latency cost; tunable down to 0 for latency-critical paths |
| `native_fragment_striping` | `0` | Safer default; enable to `1` on four-rail topologies for ~7% extra BW |
| `zcopy_min_bytes` | `4096` | +17% BW at 1 MiB; no measurable cost at small sizes; enabled by default |

The default for `nhi_interrupt_throttle_ns` in the kernel module itself
remains `0` (backward-compatible) — the recommended 50 µs is applied by the
Nix bench config and documented in `docs/TUNING.md` for operators to opt in.

See [`docs/TUNING.md`](../../../docs/TUNING.md) for full guidance on when and
how to change each parameter.
