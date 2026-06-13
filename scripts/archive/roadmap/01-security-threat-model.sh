#!/usr/bin/env bash
# 01-security-threat-model.sh
# Roadmap step 1 — Security threat model and hardening.
#
# Creates (idempotently): a tracking issue, a seeded branch, a draft PR linked
# to the issue, and a Copilot @mention handing off the implementation.
#
# Usage:
#   ./01-security-threat-model.sh
#   DRY_RUN=1 ./01-security-threat-model.sh    # preview without changing anything

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="01"
STEP_SLUG="security-threat-model"
STEP_TITLE="Security threat model and hardening"
STEP_LABELS="security,kernel,epic"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Roadmap step 1.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
The README flags the driver as insecure. RDMA over a DMA fabric exposes host
memory to a peer; without a threat model this stays unfit for any shared or
untrusted setting.

### Scope
- Document the trust boundary between connected hosts.
- Audit memory registration / remote key handling.
- Bound what a remote peer can read/write (key scoping, length checks).
- Add optional peer allow-listing.

### Acceptance criteria
- [ ] `docs/SECURITY.md` (or a section) describing the threat model.
- [ ] Identified concrete issues filed as sub-issues of this epic.
- [ ] At least the highest-risk memory-access paths bounded and covered by tests.

### Labels
`security`, `kernel`, `epic`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Roadmap step 1 — Security threat model and hardening

This PR implements roadmap step 1 (see `docs/ROADMAP.md`).

### What this solves
Establishes the security trust boundary for RDMA-over-USB4 and bounds what a
remote peer can do to host memory, turning the README's "insecure" caveat into
concrete, tested guardrails.

### Planned changes
- Add `docs/SECURITY.md` with the threat model and trust boundary.
- Audit and bound memory-registration / remote-key handling in the kernel module.
- Add key scoping, length checks, and optional peer allow-listing.
- Tests for the highest-risk memory-access paths.

### Acceptance criteria
- [ ] Threat model documented.
- [ ] Remote read/write bounded and validated.
- [ ] Tests cover the hardened paths.
EOF

STEP_HANDOFF="Implement roadmap step 1: write the RDMA-over-USB4 threat model in docs/SECURITY.md, audit and bound remote-key / memory-registration handling in the kernel module (key scoping, length checks, optional peer allow-listing), and add tests for the highest-risk memory-access paths."

run_roadmap_step
