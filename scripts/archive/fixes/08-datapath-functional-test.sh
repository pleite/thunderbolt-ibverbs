#!/usr/bin/env bash
# 08-datapath-functional-test.sh
# Fix for findings T1 + T3 — CI is build-only / no functional data-path test (P2).
#
# Usage:
#   ./08-datapath-functional-test.sh
#   DRY_RUN=1 ./08-datapath-functional-test.sh    # preview without changes

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

STEP_NUM="08"
STEP_SLUG="datapath-functional-test"
STEP_TITLE="Add an automated data-path functional test to CI (T1, T3)"
STEP_LABELS="testing,ci"

read -r -d '' STEP_ISSUE_BODY <<'EOF' || true
**Driver fix T1 + T3.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
CI builds/installs the module + provider across distros but runs no RDMA
data-path test (`.github/workflows/release-artefacts.yml`); the smoke tests only
exercise wire parsing. Functional correctness is validated manually on two
physical machines, not in CI.

### Scope
- Add a loopback/virtual two-node harness (QEMU or a software backend) in CI.
- Run `ib_write_bw`/`ib_send_bw` plus a bit-verified transfer.
- Assert zero error counters.

### Acceptance criteria
- [ ] CI runs a real verbs transfer and bit-verifies the result.
- [ ] The job fails on any error-counter movement.

### Labels
`testing`, `ci`
EOF

read -r -d '' STEP_PR_BODY <<'EOF' || true
## Fix T1 + T3 — automated data-path functional test

This PR addresses findings **T1** and **T3** in `docs/FINDINGS.md`.

### What this solves
Turns CI from build-only into something that actually moves RDMA bytes and
verifies them, catching data-path regressions automatically.

### Planned changes
- Two-node loopback/virtual harness in CI.
- Bit-verified `ib_write_bw`/`ib_send_bw` run with zero-error assertions.

### Acceptance criteria
- [ ] Verbs transfer bit-verified in CI.
EOF

STEP_HANDOFF="Implement fixes T1/T3 from docs/FINDINGS.md: add a CI loopback/virtual two-node harness (QEMU or software backend) that runs ib_write_bw/ib_send_bw plus a bit-verified transfer and fails on any error-counter movement, extending .github/workflows/release-artefacts.yml and tools/ci/."

run_fix_step
