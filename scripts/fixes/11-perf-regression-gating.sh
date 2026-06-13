#!/usr/bin/env bash
# 11-perf-regression-gating.sh
# Fix for finding T1 — performance regression gating (P2).
#
# Usage:
#   ./11-perf-regression-gating.sh
#   DRY_RUN=1 ./11-perf-regression-gating.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="11"
STEP_SLUG="perf-regression-gating"
STEP_TITLE="Gate CI on performance regressions (T1)"
STEP_LABELS="performance,ci"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
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
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix T1 — performance regression gating

This PR addresses finding **T1** (perf gating) in `docs/FINDINGS.md`.

### What this solves
Stops silent bandwidth/latency regressions by comparing benchmark runs against a
committed baseline in CI.

### Planned changes
- Baseline under `bench/`; threshold-based CI comparison.

### Acceptance criteria
- [ ] CI fails when perf regresses past the threshold.
EOF

STEP_HANDOFF="Implement fix T1 (perf gating) from docs/FINDINGS.md: commit a tbv-perftest baseline under bench/ and add a CI check that fails when bandwidth/latency regress beyond a documented threshold (reuse bench/summarize_perftest.py)."

run_fix_step
