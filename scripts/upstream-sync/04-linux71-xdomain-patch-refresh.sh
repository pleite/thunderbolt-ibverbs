#!/usr/bin/env bash
# 04-linux71-xdomain-patch-refresh.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="04"
STEP_SLUG="linux71-xdomain-patch-refresh"
STEP_TITLE="Backport upstream #50: refresh kernel-workflow xdomain patches for Linux 7.1"
STEP_LABELS="upstream-sync,compatibility,kernel-workflow"

read -r -d '' STEP_ISSUE_BODY <<'BODY' || true
Backport upstream **hellas-ai/thunderbolt-ibverbs#50** (`da6b1ef9`) to keep kernel-workflow patch stacks aligned with Linux 7.1.

### Why this matters
- Upstream refreshed `0009-thunderbolt-xdomain-match-properties-by-identity.patch` against Linux 7.1 and removed hunks now carried by upstream kernel.
- Upstream added integration-only prep patch `0006-thunderbolt-xdomain-bound-response-copy.patch` so local integration stack matches portable stack context.

### Scope
- Add new `0006` patch file under `kernel-workflow/patches/`.
- Refresh `0009` patch content to the Linux 7.1-compatible form.
- Update `kernel-workflow/patches/local-integration-debug.nix` to include `0006`.

### Acceptance criteria
- [ ] Portable and integration patch stacks apply cleanly in workflow builds.
- [ ] No obsolete hunks remain in refreshed `0009` patch.
BODY

read -r -d '' STEP_PR_BODY <<'BODY' || true
## Upstream sync step 04 — backport #50 (Linux 7.1 xdomain patch refresh)

Backport upstream kernel-workflow patch-stack update from `hellas-ai/thunderbolt-ibverbs#50`.

### Planned changes
- Add integration prep patch `0006-thunderbolt-xdomain-bound-response-copy.patch`.
- Refresh `0009-thunderbolt-xdomain-match-properties-by-identity.patch` for Linux 7.1 baseline.
- Wire `0006` into `local-integration-debug.nix` patch list.

### Validation
- Run repository kernel-workflow patch-stack checks currently used in CI/Nix.
BODY

STEP_HANDOFF="Backport upstream #50 (da6b1ef9): add kernel-workflow patch 0006, refresh patch 0009 for Linux 7.1, and update local-integration-debug.nix patch list so integration and portable stacks stay aligned."

run_upstream_step
