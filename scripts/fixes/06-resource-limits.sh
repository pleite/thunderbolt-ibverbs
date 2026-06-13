#!/usr/bin/env bash
# 06-resource-limits.sh
# Fix for finding R5 — weakly-bounded allocations / no rate limiting (P1).
#
# Usage:
#   ./06-resource-limits.sh
#   DRY_RUN=1 ./06-resource-limits.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="06"
STEP_SLUG="resource-limits"
STEP_TITLE="Enforce resource limits and per-peer rate limiting (R5)"
STEP_LABELS="reliability,kernel"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix R5.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The advertised `max_qp`/`max_cq`/`max_mr` limits are not enforced at create time,
there is no per-peer rate limiting or credit reservation, and Apple pending-RX
bytes are bounded per QP (`kernel/ibdev.c:5606-5616`) but not globally per
device. A peer can exhaust host resources.

### Scope
- Enforce advertised object limits at create time.
- Cap Apple pending-RX bytes per device (not just per QP).
- Add per-peer rate limiting / credit reservation.

### Acceptance criteria
- [ ] Exceeding an advertised limit fails the create call cleanly.
- [ ] Per-device memory pressure from one peer is bounded.

### Labels
`reliability`, `kernel`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix R5 — enforce resource limits

This PR addresses finding **R5** in `docs/FINDINGS.md`.

### What this solves
Bounds what a single peer can allocate so it cannot exhaust host memory or
object tables.

### Planned changes
- Enforce max_qp/cq/mr at create time.
- Global per-device pending-RX byte cap.
- Per-peer rate limiting / credit reservation.

### Acceptance criteria
- [ ] Limits enforced and tested.
EOF

STEP_HANDOFF="Implement fix R5 from docs/FINDINGS.md: enforce advertised max_qp/max_cq/max_mr at create time, add a global per-device Apple pending-RX byte cap (extending kernel/ibdev.c ~5606-5616), and add per-peer rate limiting / credit reservation."

run_fix_step
