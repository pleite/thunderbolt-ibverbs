#!/usr/bin/env bash
# 02-native-transport-hardening.sh
# Roadmap step 2 — Native Linux-to-Linux transport hardening.
#
# Usage:
#   ./02-native-transport-hardening.sh
#   DRY_RUN=1 ./02-native-transport-hardening.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="02"
STEP_SLUG="native-transport-hardening"
STEP_TITLE="Native Linux-to-Linux transport hardening"
STEP_LABELS="kernel,reliability,epic"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Roadmap step 2.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
The native Linux transport is the main path and most of the benchmark story
depends on it; bugs here block everyone.

### Scope
- Harden ring setup/teardown, error and disconnect handling, and the verb
  completion paths.
- Add stress and fault-injection coverage.
- Reduce known buggy edge cases under cable pulls and module reload.

### Acceptance criteria
- [ ] Repeated connect/disconnect and module reload cycles pass without leaks
      or oopses.
- [ ] perftest verbs (read/write/send) run clean in CI or a documented manual
      matrix.

### Labels
`kernel`, `reliability`, `epic`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Roadmap step 2 — Native Linux-to-Linux transport hardening

This PR implements roadmap step 2 (see `docs/ROADMAP.md`).

### What this solves
Makes the primary native transport robust under disconnects and reloads, so the
read/write/send verb paths stop oopsing or leaking on the path everyone relies on.

### Planned changes
- Harden ring setup/teardown and disconnect/error handling.
- Add stress + fault-injection coverage (cable pull, module reload).
- Fix known buggy edge cases surfaced by the above.

### Acceptance criteria
- [ ] Connect/disconnect and reload cycles are leak/oops free.
- [ ] perftest read/write/send pass in a documented matrix.
EOF

STEP_HANDOFF="Implement roadmap step 2: harden the native Linux transport (ring setup/teardown, disconnect and error handling, verb completion paths), add stress/fault-injection coverage for cable pulls and module reload, and fix the edge cases those surface."

run_roadmap_step
