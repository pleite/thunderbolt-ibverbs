#!/usr/bin/env bash
# 13-project-docs.sh
# Fix for finding T4 — missing project docs (P3).
#
# Usage:
#   ./13-project-docs.sh
#   DRY_RUN=1 ./13-project-docs.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="13"
STEP_SLUG="project-docs"
STEP_TITLE="Author SECURITY/CONTRIBUTING/protocol/param docs (T4)"
STEP_LABELS="documentation"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix T4.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The project lacks a `SECURITY.md`, `CONTRIBUTING.md`, a documented threat model,
a `proto/` wire-protocol spec, and a module-parameter reference. Onboarding and
responsible disclosure both suffer.

### Scope
- `SECURITY.md` (reporting + threat model, cross-referencing docs/FINDINGS.md).
- `CONTRIBUTING.md`.
- A wire-protocol spec for `proto/`.
- A module-parameter reference.

### Acceptance criteria
- [ ] All four docs exist and are linked from the README.

### Labels
`documentation`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix T4 — project documentation

This PR addresses finding **T4** in `docs/FINDINGS.md`.

### What this solves
Fills the documentation gaps around security reporting, contribution workflow,
the wire protocol, and module parameters.

### Planned changes
- Add SECURITY.md, CONTRIBUTING.md, a proto/ protocol spec, and a param ref.
- Link them from the README.

### Acceptance criteria
- [ ] Docs exist and are linked.
EOF

STEP_HANDOFF="Implement fix T4 from docs/FINDINGS.md: add SECURITY.md (reporting + threat model, referencing docs/FINDINGS.md), CONTRIBUTING.md, a proto/ wire-protocol spec, and a module-parameter reference, all linked from the README."

run_fix_step
