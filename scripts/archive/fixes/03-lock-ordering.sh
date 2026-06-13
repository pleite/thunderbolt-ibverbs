#!/usr/bin/env bash
# 03-lock-ordering.sh
# Fix for finding R3 — lock-hierarchy inconsistency (P0).
#
# Usage:
#   ./03-lock-ordering.sh
#   DRY_RUN=1 ./03-lock-ordering.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="03"
STEP_SLUG="lock-ordering"
STEP_TITLE="Document and enforce a single lock ordering (R3)"
STEP_LABELS="reliability,kernel"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix R3.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
Some paths take `peer->control_lock` then a state lock (`kernel/ibdev.c:2783`),
while others take only an owner lock — an inconsistent hierarchy that risks
deadlock and is hard to review.

### Scope
- Define one lock order (e.g. `peer->control_lock` → `owner->lock` → `tqp->lock`).
- Annotate it (lockdep / comments) and fix paths that violate it.

### Acceptance criteria
- [ ] The lock order is documented in the source.
- [ ] All offending paths follow it; lockdep is clean under stress.

### Labels
`reliability`, `kernel`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix R3 — single, enforced lock ordering

This PR addresses finding **R3** in `docs/FINDINGS.md`.

### What this solves
Removes deadlock risk from the inconsistent lock acquisition order across the
peer/owner/QP locks.

### Planned changes
- Document the canonical lock hierarchy in-source.
- Reorder the offending acquisition paths; add lockdep annotations.

### Acceptance criteria
- [ ] Documented order.
- [ ] Lockdep clean under stress.
EOF

STEP_HANDOFF="Implement fix R3 from docs/FINDINGS.md: define and document one lock order (peer->control_lock -> owner->lock -> tqp->lock), annotate with lockdep, and fix the inconsistent paths (kernel/ibdev.c ~2783 vs owner-only paths)."

run_fix_step
