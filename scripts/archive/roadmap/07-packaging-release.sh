#!/usr/bin/env bash
# 07-packaging-release.sh
# Roadmap step 7 — Packaging and release automation.
#
# Usage:
#   ./07-packaging-release.sh
#   DRY_RUN=1 ./07-packaging-release.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="07"
STEP_SLUG="packaging-release"
STEP_TITLE="Packaging and release automation"
STEP_LABELS="packaging,ci,release"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Roadmap step 7.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
Debian, Fedora, Arch, and Nix builds already exist; keeping them in lockstep
and releasing cleanly is ongoing maintenance.

### Scope
- Verify DKMS + provider packages across the supported distros each release.
- Keep `packaging/` and `nix/` in sync.
- Tighten the release-artefact workflow.

### Acceptance criteria
- [ ] A release produces working DKMS and provider packages for every supported
      distro from one workflow.
- [ ] Validated by an install smoke.

### Labels
`packaging`, `ci`, `release`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Roadmap step 7 — Packaging and release automation

This PR implements roadmap step 7 (see `docs/ROADMAP.md`).

### What this solves
Keeps the Debian/Fedora/Arch/Nix packages in lockstep and makes a release
produce validated DKMS + provider artefacts from a single workflow.

### Planned changes
- Cross-distro DKMS + provider package verification in the release workflow.
- Sync of `packaging/` and `nix/` sources.
- Install-smoke validation of the produced artefacts.

### Acceptance criteria
- [ ] One workflow builds working packages for every supported distro.
- [ ] Install smoke passes on the artefacts.
EOF

STEP_HANDOFF="Implement roadmap step 7: verify DKMS and userspace-provider packages across Debian/Fedora/Arch/Nix in the release-artefact workflow, keep packaging/ and nix/ in sync, and add an install smoke that validates the produced artefacts."

run_roadmap_step
