# Automated end-to-end and regression testing

**Roadmap step 6.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
Much validation is manual two-node testing; regressions are easy to miss
between releases.

### Scope
- Extend the packaged smoke helpers (`tbv_vllm_smoke.sh`, `tbv_perftest_runner`)
  into a repeatable two-node regression suite.
- Wire as much as possible into CI (or a documented self-hosted runner).

### Acceptance criteria
- [ ] A single command runs the verb + transport smoke and fails on regression.
- [ ] Results are recorded per run.

### Labels
`testing`, `ci`
