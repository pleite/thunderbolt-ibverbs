#!/usr/bin/env bash
# 04-kernel-compatibility.sh
# Roadmap step 4 — Broaden kernel version compatibility.
#
# Usage:
#   ./04-kernel-compatibility.sh
#   DRY_RUN=1 ./04-kernel-compatibility.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="04"
STEP_SLUG="kernel-compatibility"
STEP_TITLE="Broaden kernel version compatibility"
STEP_LABELS="kernel,compatibility"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Roadmap step 4.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
The module needs Linux 6.14+ (or the flake's `linux-thunderbolt`) due to
maintainer-tree Thunderbolt/USB4 changes, limiting adoption on stock distro
kernels.

### Scope
- Isolate the maintainer-tree dependencies behind compatibility shims.
- Feature-detect `tb_ring_throttling()` and friends.
- Document the minimum viable kernel per feature.

### Acceptance criteria
- [ ] A compatibility matrix in docs.
- [ ] The module builds and loads on at least one older stock kernel with
      degraded-but-documented features.

### Labels
`kernel`, `compatibility`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Roadmap step 4 — Broaden kernel version compatibility

This PR implements roadmap step 4 (see `docs/ROADMAP.md`).

### What this solves
Lets the module build and load on stock distro kernels older than 6.14 by
isolating maintainer-tree dependencies behind feature-detected shims.

### Planned changes
- Compatibility shims around maintainer-tree Thunderbolt/USB4 APIs.
- Feature detection for `tb_ring_throttling()` and similar symbols.
- A documented kernel/feature compatibility matrix.

### Acceptance criteria
- [ ] Builds/loads on an older stock kernel (degraded features documented).
- [ ] Compatibility matrix added to docs.
EOF

STEP_HANDOFF="Implement roadmap step 4: isolate maintainer-tree Thunderbolt/USB4 dependencies behind compatibility shims, feature-detect symbols like tb_ring_throttling(), and add a kernel/feature compatibility matrix to docs so the module builds on older stock kernels."

run_roadmap_step
