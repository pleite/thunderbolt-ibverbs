# Gate CI on performance regressions (T1)

**Driver fix T1 (perf gating).** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
Benchmarks live in `bench/` but nothing fails when a change regresses bandwidth
or latency. The "robust performance" objective needs an automated guard.

### Scope
- Baseline `tbv-perftest` results in `bench/`.
- Add a CI check that fails when results regress beyond a threshold.

### Acceptance criteria
- [ ] A committed baseline and a documented regression threshold.
- [ ] CI fails on regression past the threshold.

### Labels
`performance`, `ci`
