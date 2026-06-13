#!/usr/bin/env bash
# 12-reliability-backoff.sh
# Fix for finding R6 — reliability retry has no backoff (P3).
#
# Usage:
#   ./12-reliability-backoff.sh
#   DRY_RUN=1 ./12-reliability-backoff.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="12"
STEP_SLUG="reliability-backoff"
STEP_TITLE="Add exponential backoff + jitter to reliability retries (R6)"
STEP_LABELS="reliability,performance"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix R6.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
`tbv_rel_retry_interval` ignores the retry budget and returns a fixed interval
(`proto/reliability.c:216-219`), so under congestion all senders retry in
lockstep — a synchronized-retry collapse.

### Scope
- Compute the retry interval with exponential backoff and jitter.
- Keep it deterministic enough to test.

### Acceptance criteria
- [ ] Retry interval grows with successive retries (with jitter).
- [ ] A test asserts the backoff schedule.

### Labels
`reliability`, `performance`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix R6 — reliability backoff + jitter

This PR addresses finding **R6** in `docs/FINDINGS.md`.

### What this solves
Replaces the fixed retry interval with exponential backoff + jitter so retries
don't synchronize and collapse under congestion.

### Planned changes
- Backoff/jitter in `tbv_rel_retry_interval`.
- A test covering the schedule.

### Acceptance criteria
- [ ] Backoff verified by test.
EOF

STEP_HANDOFF="Implement fix R6 from docs/FINDINGS.md: make tbv_rel_retry_interval (proto/reliability.c ~216) use exponential backoff with jitter based on the retry count/budget, and add a test asserting the backoff schedule."

run_fix_step
