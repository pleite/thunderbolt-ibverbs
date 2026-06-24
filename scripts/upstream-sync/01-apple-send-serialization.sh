#!/usr/bin/env bash
# 01-apple-send-serialization.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="01"
STEP_SLUG="apple-send-serialization"
STEP_TITLE="Backport upstream #44: serialize Apple UC SENDs per QP"
STEP_LABELS="upstream-sync,apple,kernel,testing"

read -r -d '' STEP_ISSUE_BODY <<'BODY' || true
Backport upstream **hellas-ai/thunderbolt-ibverbs#44** (`a14a175`) and adapt it to this fork's current shape.

### Why this matters
- Upstream merged a correctness fix after observed Linux→macOS short-send corruption/timeouts.
- Our fork still uses the single-frame fast path (`frames > 1` exclusive only), so this divergence remains.

### Scope
- Add `proto/apple_tx.h` and wire it into kernel and `tools/ci/proto-smoke.c`.
- Route all non-empty Apple SENDs through exclusive per-QP TX window.
- Keep `apple_tx_max_inflight_wr` as deprecated compatibility knob (do not remove parameter in this step).
- Update `kernel/Makefile` `make help` text for deprecation notice.

### Fork-specific adaptation required
Our tree has R7 split and has **not** landed upstream #42 helper extraction, so backport must refactor local duplicated predicates first or fold that refactor into this patch while preserving R2/R3 behavior.

### Acceptance criteria
- [ ] `proto/apple_tx.h` exists and is shared by kernel + proto-smoke.
- [ ] Apple TX admission uses `frames > 0` exclusive policy.
- [ ] `make -C tools/ci test` passes with new Apple TX policy test.
BODY

read -r -d '' STEP_PR_BODY <<'BODY' || true
## Upstream sync step 01 — backport #44 (Apple SEND serialization)

Backport upstream correctness fix from `hellas-ai/thunderbolt-ibverbs#44` into this fork with local refactor-aware conflict resolution.

### Planned changes
- Introduce shared `proto/apple_tx.h` admission helpers.
- Serialize all non-empty Apple SENDs per QP.
- Preserve compatibility module parameter while documenting deprecation.
- Extend proto smoke coverage for the shared policy helpers.

### Validation
- `make -C kernel modules KDIR=/lib/modules/$(uname -r)/build`
- `make -C tools/ci test`
- `make -C proto test`
BODY

STEP_HANDOFF="Backport upstream #44 (a14a175): add proto/apple_tx.h, switch Apple SEND admission to exclusive for any non-empty SEND, update make-help deprecation text, and add proto-smoke policy tests; adapt carefully because this fork still has duplicated window predicates in kernel/ibdev.c and must preserve downstream R2/R3 behavior."

run_upstream_step
