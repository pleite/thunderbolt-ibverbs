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
- [x] A single command runs the verb + transport smoke and fails on regression.
- [x] Results are recorded per run.

### Implemented
- Added `nix run .#tbv-regression` (backed by
  `userspace/bench/tbv_regression_suite.py`) as a one-command two-node suite.
- Runs both `tbv_vllm_smoke.sh` (transport smoke) and a filtered
  `tbv-perftest` verbs smoke.
- Records per-run artifacts in `thunderbolt-ibverbs/results/regression/<run-id>/`
  including `manifest.json`, `regression.json`, logs, and perftest CSV/JSONL.
- Fails non-zero on smoke failures and on metric regressions against baseline.
- Added self-hosted CI entrypoint:
  `.github/workflows/regression-self-hosted.yml`.

### Labels
`testing`, `ci`
