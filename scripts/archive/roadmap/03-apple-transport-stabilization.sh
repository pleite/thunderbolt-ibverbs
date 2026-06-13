#!/usr/bin/env bash
# 03-apple-transport-stabilization.sh
# Roadmap step 3 — Apple-compatible transport stabilization.
#
# Usage:
#   ./03-apple-transport-stabilization.sh
#   DRY_RUN=1 ./03-apple-transport-stabilization.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="03"
STEP_SLUG="apple-transport-stabilization"
STEP_TITLE="Apple-compatible transport stabilization"
STEP_LABELS="kernel,apple,epic"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Roadmap step 3.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
The Apple-compatible transport exists but is experimental; stabilizing it
widens the usable hardware matrix.

### Scope
- Finish the `mac_compat` profile path.
- Document supported macOS/Apple hardware.
- Add an interop smoke test against a Linux peer.

### Acceptance criteria
- [ ] Documented working Apple<->Linux verb exchange.
- [ ] The experimental caveat in the README narrowed to known limitations.

### Labels
`kernel`, `apple`, `epic`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Roadmap step 3 — Apple-compatible transport stabilization

This PR implements roadmap step 3 (see `docs/ROADMAP.md`).

### What this solves
Moves the `mac_compat` transport from experimental toward usable, so Apple/macOS
hardware can interoperate with a Linux peer over verbs.

### Planned changes
- Complete the `mac_compat` profile path.
- Document supported Apple hardware and known limitations.
- Add an Apple<->Linux interop smoke test.

### Acceptance criteria
- [ ] Apple<->Linux verb exchange works and is documented.
- [ ] README caveat narrowed to specific known limitations.
EOF

STEP_HANDOFF="Implement roadmap step 3: complete the mac_compat profile path, document supported Apple/macOS hardware and its limitations, and add an Apple<->Linux interop smoke test."

run_roadmap_step
