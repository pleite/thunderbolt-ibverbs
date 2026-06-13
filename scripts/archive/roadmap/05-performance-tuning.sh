#!/usr/bin/env bash
# 05-performance-tuning.sh
# Roadmap step 5 — Performance tuning and tunable defaults.
#
# Usage:
#   ./05-performance-tuning.sh
#   DRY_RUN=1 ./05-performance-tuning.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="05"
STEP_SLUG="performance-tuning"
STEP_TITLE="Performance tuning and tunable defaults"
STEP_LABELS="performance,benchmarks"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Roadmap step 5.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
Parameters like `nhi_interrupt_throttle_ns`, write/fragment striping, and
`zcopy_min_bytes` materially affect bandwidth/latency but lack guidance and
good defaults.

### Scope
- Sweep the key tunables and capture results in `bench/`.
- Pick sensible defaults.
- Document the tuning knobs and when to change them.

### Acceptance criteria
- [ ] Reproducible benchmark sweep checked into `bench/`.
- [ ] Updated default values.
- [ ] A tuning section in the docs.

### Labels
`performance`, `benchmarks`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Roadmap step 5 — Performance tuning and tunable defaults

This PR implements roadmap step 5 (see `docs/ROADMAP.md`).

### What this solves
Replaces guesswork around the perf-critical module parameters with a measured
sweep, sensible defaults, and tuning guidance.

### Planned changes
- Reproducible sweep over `nhi_interrupt_throttle_ns`, striping, `zcopy_min_bytes`.
- Results captured under `bench/`.
- Updated default parameter values and a docs tuning section.

### Acceptance criteria
- [ ] Sweep + results committed under `bench/`.
- [ ] Defaults updated and documented.
EOF

STEP_HANDOFF="Implement roadmap step 5: sweep the key tunables (nhi_interrupt_throttle_ns, write/fragment striping, zcopy_min_bytes), capture reproducible results in bench/, choose sensible default values, and add a tuning section to the docs."

run_roadmap_step
