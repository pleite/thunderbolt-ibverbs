#!/usr/bin/env bash
# 14-split-ibdev.sh
# Fix for finding R7 — ibdev.c monolith (P3).
#
# Usage:
#   ./14-split-ibdev.sh
#   DRY_RUN=1 ./14-split-ibdev.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="14"
STEP_SLUG="split-ibdev"
STEP_TITLE="Split ibdev.c into reviewable modules (R7)"
STEP_LABELS="kernel,refactor"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix R7.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
`kernel/ibdev.c` is a ~9.8k-line file mixing native transport, Apple transport,
CQ, MR, and the QP state machine. Its size makes the security-critical paths hard
to review, which compounds every other finding here.

### Scope
- Split `ibdev.c` into focused translation units (native transport / Apple
  transport / CQ / MR / QP state machine) with no behaviour change.
- Land it after the P0/P1 behavioural fixes so tests pin behaviour first.

### Acceptance criteria
- [ ] `ibdev.c` is split into smaller modules.
- [ ] No functional change; existing tests still pass.

### Labels
`kernel`, `refactor`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix R7 — split ibdev.c

This PR addresses finding **R7** in `docs/FINDINGS.md`.

### What this solves
Breaks the ~9.8k-line `ibdev.c` monolith into reviewable modules so the
security-critical paths can actually be audited.

### Planned changes
- Mechanical split into native/Apple/CQ/MR/QP units, no behaviour change.

### Acceptance criteria
- [ ] Split landed; tests green; no functional change.
EOF

STEP_HANDOFF="Implement fix R7 from docs/FINDINGS.md: mechanically split kernel/ibdev.c into focused translation units (native transport, Apple transport, CQ, MR, QP state machine) with no behaviour change, after the behavioural P0/P1 fixes are in and tested."

run_fix_step
