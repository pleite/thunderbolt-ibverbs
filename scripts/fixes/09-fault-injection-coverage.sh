#!/usr/bin/env bash
# 09-fault-injection-coverage.sh
# Fix for finding T1 — no fault-injection coverage for the reliability engine (P2).
#
# Usage:
#   ./09-fault-injection-coverage.sh
#   DRY_RUN=1 ./09-fault-injection-coverage.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="09"
STEP_SLUG="fault-injection-coverage"
STEP_TITLE="Add fault-injection coverage for the reliability engine (T1)"
STEP_LABELS="testing,ci"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix T1 (fault injection).** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The reliability engine (`proto/reliability.c`) has no automated fault-injection
coverage. This matches the upstream PR #22 "still pending" list: lost
ACK/credit/data frame, duplicate retry, RNR exhaustion, READ-response retry, and
teardown-while-in-flight are untested.

### Scope
- Drive `proto/reliability.c` with injected faults for each scenario above.
- Run the suite in CI; it exercises the protocol, no hardware required.

### Acceptance criteria
- [ ] Tests for lost ACK/credit/data, duplicate retry, RNR exhaustion,
      READ-response retry, and teardown-while-in-flight.
- [ ] They run in CI and gate merges.

### Labels
`testing`, `ci`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix T1 — reliability fault-injection coverage

This PR addresses finding **T1** (fault injection) in `docs/FINDINGS.md`.

### What this solves
Adds the missing negative-path coverage for the reliability engine so retransmit
/ dedup / RNR / teardown bugs are caught in CI.

### Planned changes
- Fault-injection tests over `proto/reliability.c` for each scenario.
- Wire them into CI.

### Acceptance criteria
- [ ] All listed scenarios covered and gating.
EOF

STEP_HANDOFF="Implement fix T1 (fault injection) from docs/FINDINGS.md: add fault-injection tests over proto/reliability.c for lost ACK/credit/data frame, duplicate retry, RNR exhaustion, READ-response retry, and teardown-while-in-flight, and run them in CI."

run_fix_step
