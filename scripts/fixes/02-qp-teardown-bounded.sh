#!/usr/bin/env bash
# 02-qp-teardown-bounded.sh
# Fix for finding R2 — QP teardown can hang / race (P0).
#
# Usage:
#   ./02-qp-teardown-bounded.sh
#   DRY_RUN=1 ./02-qp-teardown-bounded.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="02"
STEP_SLUG="qp-teardown-bounded"
STEP_TITLE="Make QP teardown bounded and race-free (R2)"
STEP_LABELS="reliability,kernel"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix R2.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
QP destroy ends in an untimed `wait_for_completion(&tqp->refs_zero)`
(`kernel/ibdev.c:2718`); a late RDMA-READ response in flight can hang teardown
indefinitely, and there is a window between marking the QP closing and disarming
its timeout work.

### Scope
- Put a bound on the final wait (timeout + diagnostic on expiry).
- Fence the `closing`/timeout-disarm window so no work re-arms after close.
- Drain in-flight READ responses deterministically before completing destroy.

### Acceptance criteria
- [ ] QP destroy cannot block indefinitely.
- [ ] A teardown-while-in-flight test exercises the drained path.

### Labels
`reliability`, `kernel`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix R2 — bounded, race-free QP teardown

This PR addresses finding **R2** in `docs/FINDINGS.md`.

### What this solves
Prevents QP destroy from hanging on a late RDMA-READ response and closes the
race between marking the QP closing and disarming its timeout work.

### Planned changes
- Bound the final teardown wait and report on timeout.
- Fence the closing/disarm window; drain in-flight READ responses.
- Add a teardown-while-in-flight test.

### Acceptance criteria
- [ ] Destroy is bounded.
- [ ] Test covers in-flight teardown.
EOF

STEP_HANDOFF="Implement fix R2 from docs/FINDINGS.md: bound the final wait_for_completion in QP destroy (kernel/ibdev.c ~2718), fence the closing/timeout-disarm race, drain in-flight RDMA-READ responses, and add a teardown-while-in-flight test."

run_fix_step
