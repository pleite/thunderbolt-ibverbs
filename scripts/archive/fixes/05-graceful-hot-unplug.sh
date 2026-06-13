#!/usr/bin/env bash
# 05-graceful-hot-unplug.sh
# Fix for finding R4 — hot-unplug drops in-flight WRs silently (P1).
#
# Usage:
#   ./05-graceful-hot-unplug.sh
#   DRY_RUN=1 ./05-graceful-hot-unplug.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="05"
STEP_SLUG="graceful-hot-unplug"
STEP_TITLE="Flush in-flight WRs on hot-unplug instead of silent drops (R4)"
STEP_LABELS="reliability,kernel"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix R4.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
`rail->removing` is checked before queueing (`kernel/ibdev.c:1795`, `2102-2125`),
but WRs queued just afterward are dropped with no flush/error completion, and the
Apple tunnel teardown is warn-only. Callers can hang waiting for completions that
never arrive.

### Scope
- On `rail->removing`, flush outstanding WRs to flush/error completions.
- Convert Apple tunnel warn-only teardown into deterministic teardown.

### Acceptance criteria
- [ ] Every posted WR gets a completion (success or flush/error) on unplug.
- [ ] A hot-unplug-with-in-flight-traffic test passes.

### Labels
`reliability`, `kernel`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix R4 — graceful hot-unplug

This PR addresses finding **R4** in `docs/FINDINGS.md`.

### What this solves
Ensures in-flight WRs are flushed to error completions on device removal rather
than silently dropped, so callers don't hang.

### Planned changes
- Flush outstanding WRs when `rail->removing`.
- Deterministic Apple tunnel teardown.
- Hot-unplug test with in-flight traffic.

### Acceptance criteria
- [ ] No completion is lost on unplug.
EOF

STEP_HANDOFF="Implement fix R4 from docs/FINDINGS.md: when rail->removing (kernel/ibdev.c ~1795, ~2102), flush outstanding WRs to flush/error completions instead of dropping them, make Apple tunnel teardown deterministic, and add a hot-unplug-with-in-flight-traffic test."

run_fix_step
