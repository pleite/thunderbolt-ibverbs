#!/usr/bin/env bash
# 01-reliability-dedup-window.sh
# Fix for finding R1 — proto/ dedup/ACK window can wrap (P0).
#
# Usage:
#   ./01-reliability-dedup-window.sh
#   DRY_RUN=1 ./01-reliability-dedup-window.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="01"
STEP_SLUG="reliability-dedup-window"
STEP_TITLE="Bound the proto/ dedup/ACK window to outstanding WRs (R1)"
STEP_LABELS="reliability,kernel"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix R1.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The freestanding `proto/` reliability path keeps a 16-entry ACK/dedup window
(`proto/reliability.h:23`) scanned linearly (`proto/reliability.c:52-54`), while
the kernel allows up to `TBV_IBDEV_MAX_QP_WR` (1024) outstanding WRs. Under heavy
retransmission the window can wrap and accept duplicates as new. The kernel side
was already widened (`kernel/ibdev.c:70`); the protocol side still needs it.

### Scope
- Size the `proto/` dedup window to the outstanding-WR limit, or switch to a
  PSN bitmap/`xarray`-style tracker instead of a linear scan.
- Keep the freestanding code dependency-free (no kernel/libc-only helpers).

### Acceptance criteria
- [ ] Duplicate frames are rejected across the full outstanding-WR range.
- [ ] A fault-injection test covers duplicate-frame acceptance.

### Labels
`reliability`, `kernel`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix R1 — bound the proto/ dedup/ACK window

This PR addresses finding **R1** in `docs/FINDINGS.md`.

### What this solves
Stops the freestanding reliability engine from wrapping its 16-entry dedup
window and accepting duplicate frames as new when many WRs are outstanding.

### Planned changes
- Grow/replace the `proto/` dedup window so it covers the outstanding-WR range.
- Add a duplicate-frame fault-injection test.

### Acceptance criteria
- [ ] Duplicates rejected across the full window.
- [ ] Test proves it.
EOF

STEP_HANDOFF="Implement fix R1 from docs/FINDINGS.md: size the proto/ reliability dedup window (proto/reliability.h TBV_REL_ACK_HISTORY_SIZE / proto/reliability.c scan) to the outstanding-WR limit or a PSN bitmap, and add a duplicate-frame fault-injection test."

run_fix_step
