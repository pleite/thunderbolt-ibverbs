#!/usr/bin/env bash
# 10-kernel-static-analysis.sh
# Fix for finding T2 — no kernel memory-safety / static analysis (P2).
#
# Usage:
#   ./10-kernel-static-analysis.sh
#   DRY_RUN=1 ./10-kernel-static-analysis.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="10"
STEP_SLUG="kernel-static-analysis"
STEP_TITLE="Add kernel static analysis and memory-safety to CI (T2)"
STEP_LABELS="testing,ci"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix T2.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
There is no checkpatch/sparse/smatch/clang-analyzer, no KASAN/KCSAN build, and
no fuzzing of the `proto/` wire parsers; smoke builds are not `-Werror`. Memory
bugs in a ~20k-LOC kernel module go uncaught.

### Scope
- Add `checkpatch.pl` and `sparse` (optionally `smatch`) CI jobs.
- Add a KASAN/KCSAN build target.
- Enable `-Werror` on the smoke builds; consider fuzzing `proto/` parsers.

### Acceptance criteria
- [ ] Static-analysis + KASAN/KCSAN jobs run in CI.
- [ ] Smoke builds are `-Werror`.

### Labels
`testing`, `ci`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix T2 — kernel static analysis & memory-safety in CI

This PR addresses finding **T2** in `docs/FINDINGS.md`.

### What this solves
Brings standard kernel hygiene tooling into CI so memory-safety and style
regressions are caught automatically.

### Planned changes
- checkpatch/sparse(/smatch) jobs; KASAN/KCSAN build target.
- `-Werror` smoke builds; optional `proto/` parser fuzzing.

### Acceptance criteria
- [ ] Tooling runs and gates in CI.
EOF

STEP_HANDOFF="Implement fix T2 from docs/FINDINGS.md: add checkpatch.pl, sparse (optionally smatch), and a KASAN/KCSAN build target to CI, enable -Werror on the smoke builds, and consider fuzzing the proto/ wire parsers."

run_fix_step
