#!/usr/bin/env bash
# 07-root-gate-debug-surfaces.sh
# Fix for finding S4 — debugfs/configfs information exposure (P1).
#
# Usage:
#   ./07-root-gate-debug-surfaces.sh
#   DRY_RUN=1 ./07-root-gate-debug-surfaces.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="07"
STEP_SLUG="root-gate-debug-surfaces"
STEP_TITLE="Root-gate debugfs/configfs surfaces (S4)"
STEP_LABELS="security,kernel"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix S4.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The debug surfaces (`kernel/debugfs.c`) expose QPNs, link IDs, route and proxy-IP
state to unprivileged users, and the configfs link model (`kernel/configfs.c`) is
broadly accessible — useful information for an attacker.

### Scope
- Restrict debugfs/configfs entries to privileged users (or a build/runtime
  "production" mode).
- Reduce sensitive fields in the `summary` output.

### Acceptance criteria
- [ ] Sensitive debug/config surfaces are not readable/writable unprivileged.
- [ ] Production builds can disable them entirely.

### Labels
`security`, `kernel`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix S4 — root-gate debug/config surfaces

This PR addresses finding **S4** in `docs/FINDINGS.md`.

### What this solves
Stops unprivileged users from reading internal driver state (QPNs, routes, proxy
IPs) and from manipulating the configfs link model.

### Planned changes
- Privilege-gate debugfs/configfs entries.
- Trim sensitive fields from `summary`.
- Add a production switch to compile them out.

### Acceptance criteria
- [ ] Surfaces gated; production build can drop them.
EOF

STEP_HANDOFF="Implement fix S4 from docs/FINDINGS.md: privilege-gate the debugfs (kernel/debugfs.c) and configfs (kernel/configfs.c) surfaces, reduce sensitive fields in summary output, and add a build/runtime production mode that disables them."

run_fix_step
