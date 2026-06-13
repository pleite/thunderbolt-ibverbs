#!/usr/bin/env bash
# 06-automated-testing.sh
# Roadmap step 6 — Automated end-to-end and regression testing.
#
# Usage:
#   ./06-automated-testing.sh
#   DRY_RUN=1 ./06-automated-testing.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="06"
STEP_SLUG="automated-testing"
STEP_TITLE="Automated end-to-end and regression testing"
STEP_LABELS="testing,ci"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
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
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Roadmap step 6 — Automated end-to-end and regression testing

This PR implements roadmap step 6 (see `docs/ROADMAP.md`).

### What this solves
Turns ad-hoc two-node manual testing into a single repeatable regression suite
so transport/verb regressions are caught before release.

### Planned changes
- Extend `tbv_vllm_smoke.sh` / `tbv_perftest_runner` into a regression suite.
- One-command runner that fails on regression and records results.
- CI wiring (or documented self-hosted runner) where feasible.

### Acceptance criteria
- [ ] One command runs verb + transport smoke and fails on regression.
- [ ] Per-run results recorded.
EOF

STEP_HANDOFF="Implement roadmap step 6: extend the packaged smoke helpers (tbv_vllm_smoke.sh, tbv_perftest_runner) into a repeatable one-command two-node regression suite that records results and fails on regression, and wire it into CI or a documented self-hosted runner."

run_roadmap_step
