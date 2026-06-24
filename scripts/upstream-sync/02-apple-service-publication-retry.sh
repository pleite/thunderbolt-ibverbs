#!/usr/bin/env bash
# 02-apple-service-publication-retry.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="02"
STEP_SLUG="apple-service-publication-retry"
STEP_TITLE="Backport upstream #46: retry deferred Apple service publication"
STEP_LABELS="upstream-sync,apple,kernel,refactor"

read -r -d '' STEP_ISSUE_BODY <<'BODY' || true
Backport upstream **hellas-ai/thunderbolt-ibverbs#46** (`825eb388`) to prevent Apple rail publication from getting stranded after transient tunnel setup failures.

### Why this matters
- Our current worker exits on first publish failure and does not requeue, leaving `apple_rails_pending=1` until some external event.
- Upstream moved this to a worker-owned publish path with fixed retry cadence.

### Scope
- Convert Apple rail work item to delayed work and add retry scheduling helper.
- Switch pending-flag access to `READ_ONCE`/`WRITE_ONCE` where lockless readers exist.
- Keep permanent registration-failure handling semantics unchanged.

### Fork-specific checks
- Ensure this remains compatible with downstream hot-unplug behavior (R4) and existing service stop lifecycle.
- Keep debugfs summary in sync with `READ_ONCE(state->apple_rails_pending)` access.

### Acceptance criteria
- [ ] No probe-time direct publish path remains for Apple rails.
- [ ] Transient tunnel publish failure triggers delayed retry.
- [ ] Services stop path cancels delayed work cleanly.
BODY

read -r -d '' STEP_PR_BODY <<'BODY' || true
## Upstream sync step 02 — backport #46 (Apple publish retry)

Backport upstream Apple rail publication robustness fix from `hellas-ai/thunderbolt-ibverbs#46`.

### Planned changes
- Move Apple publish flow to worker-owned path.
- Add retry scheduling for transient failures.
- Tighten lockless pending-flag reads/writes with `READ_ONCE`/`WRITE_ONCE`.

### Validation
- `make -C kernel modules KDIR=/lib/modules/$(uname -r)/build`
- `make -C tools/ci test`
BODY

STEP_HANDOFF="Backport upstream #46 (825eb388): convert Apple rail publish flow to delayed worker-owned retries with READ_ONCE/WRITE_ONCE pending flag access, while preserving this fork's R4/hot-unplug and service stop semantics."

run_upstream_step
