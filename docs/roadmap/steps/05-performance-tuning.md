# Performance tuning and tunable defaults

**Roadmap step 5.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
Parameters like `nhi_interrupt_throttle_ns`, write/fragment striping, and
`zcopy_min_bytes` materially affect bandwidth/latency but lack guidance and
good defaults.

### Scope
- Sweep the key tunables and capture results in `bench/`.
- Pick sensible defaults.
- Document the tuning knobs and when to change them.

### Acceptance criteria
- [x] Reproducible benchmark sweep checked into `bench/`.
- [x] Updated default values.
- [x] A tuning section in the docs.

### Implementation

- **Sweep script**: `userspace/bench/tbv_tuning_sweep.py` — parametric driver
  that reloads the module per profile and runs a focused micro-matrix.
- **Results**: `bench/results/strix-2p-noiommu-2x40g/tuning.md` — headline
  numbers, recreate commands, and per-parameter findings.
- **Kernel default updated**: `zcopy_min_bytes` changed from `0` → `4096`
  (framed-copy baseline costs ~17% BW at 1 MiB; fallback is always safe).
- **Bench config updated**: `lib/bench/default.nix` now sets
  `nhi_interrupt_throttle_ns=50000` alongside the existing tunable values.
- **Docs**: `docs/TUNING.md` — full per-parameter guide with sweep tables,
  runtime tuning examples, and diagnostic counter guidance.

### Labels
`performance`, `benchmarks`
