#!/usr/bin/env bash
# 03-rawhide-ib-umem-get-va.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="03"
STEP_SLUG="rawhide-ib-umem-get-va"
STEP_TITLE="Backport upstream #49: handle Rawhide ib_umem_get_va API"
STEP_LABELS="upstream-sync,compatibility,kernel"

read -r -d '' STEP_ISSUE_BODY <<'BODY' || true
Backport upstream **hellas-ai/thunderbolt-ibverbs#49** (`97f29a51`) to keep Fedora Rawhide / Linux >= 7.2 builds working.

### Why this matters
- Upstream switched user-MR registration to `ib_umem_get_va()` for kernels >= 7.2 where `ib_umem_get()` is no longer declared.
- This fork currently still calls `ib_umem_get()` unconditionally.

### Scope
- Add kernel-version-guarded `ib_umem_get_va()` path and keep existing fallback for older kernels.
- Update nearby comment text to refer to generic rdma umem helpers, not just `ib_umem_get()`.

### Fork-specific adaptation required
- In upstream the change landed in `kernel/ibdev.c`; in this fork the MR path is in `kernel/ibdev_mr.c` due R7 split.

### Acceptance criteria
- [ ] User-MR registration compiles on both pre-7.2 and >=7.2 headers.
- [ ] No regression in existing user-MR behavior on current kernels.
BODY

read -r -d '' STEP_PR_BODY <<'BODY' || true
## Upstream sync step 03 — backport #49 (ib_umem_get_va)

Backport upstream kernel compatibility fix from `hellas-ai/thunderbolt-ibverbs#49`.

### Planned changes
- Use `ib_umem_get_va()` under Linux >= 7.2.
- Keep `ib_umem_get()` path for older kernels.
- Apply in split MR implementation file (`kernel/ibdev_mr.c`) in this fork.

### Validation
- `make -C kernel modules KDIR=/lib/modules/$(uname -r)/build`
- Fedora Rawhide packaging/install checks (if available in the environment)
BODY

STEP_HANDOFF="Backport upstream #49 (97f29a51): add version-gated ib_umem_get_va() usage for Linux>=7.2 and fallback ib_umem_get() for older kernels, adapting patch location to kernel/ibdev_mr.c because this fork split ibdev.c in R7."

run_upstream_step
