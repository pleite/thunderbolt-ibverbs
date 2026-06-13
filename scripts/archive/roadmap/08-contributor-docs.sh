#!/usr/bin/env bash
# 08-contributor-docs.sh
# Roadmap step 8 — User and contributor documentation.
#
# Usage:
#   ./08-contributor-docs.sh
#   DRY_RUN=1 ./08-contributor-docs.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="08"
STEP_SLUG="contributor-docs"
STEP_TITLE="User and contributor documentation"
STEP_LABELS="documentation"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Roadmap step 8.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
Onboarding depends on the README and the blog post; deeper architecture and
troubleshooting docs are missing.

### Scope
- Add an architecture overview (kernel module <-> provider <-> verbs).
- Add a troubleshooting guide.
- Add a contributing guide that points back to the roadmap.

### Acceptance criteria
- [ ] `docs/` contains architecture, troubleshooting, and contributing pages.
- [ ] Each is linked from the README.

### Labels
`documentation`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Roadmap step 8 — User and contributor documentation

This PR implements roadmap step 8 (see `docs/ROADMAP.md`).

### What this solves
Fills the onboarding gap beyond the README/blog with architecture,
troubleshooting, and contributing docs.

### Planned changes
- Architecture overview: kernel module <-> provider <-> verbs.
- Troubleshooting guide.
- Contributing guide linking back to `docs/ROADMAP.md`.
- README links to each new page.

### Acceptance criteria
- [ ] Architecture, troubleshooting, and contributing pages exist under `docs/`.
- [ ] All three are linked from the README.
EOF

STEP_HANDOFF="Implement roadmap step 8: add an architecture overview (kernel module <-> provider <-> verbs), a troubleshooting guide, and a contributing guide that references docs/ROADMAP.md, and link all three from the README."

run_roadmap_step
