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
- [ ] Reproducible benchmark sweep checked into `bench/`.
- [ ] Updated default values.
- [ ] A tuning section in the docs.

### Labels
`performance`, `benchmarks`
