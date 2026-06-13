# Driver performance & security findings

This document records the findings from the performance/security analysis of the
`thunderbolt-ibverbs` driver done on the `analyze-driver-performance-security`
branch. It turns the README's "buggy, insecure, not for production" caveat
(`README.md`) into a concrete, cited, and prioritized list of work.

Each finding has a stable ID (`S*` security, `R*` robustness/correctness,
`T*` testing/CI/docs). The numbered automation in
[`scripts/fixes/`](../scripts/fixes/) files one issue + branch + draft PR per
**open** finding, the same way the (now archived) roadmap automation did per
roadmap step.

## How to use this file

- Reference a finding by its ID in issues, PRs, and code comments.
- When a finding is addressed, flip its status here to `fixed` and note the PR.
- The prioritized list in [§4](#4-prioritized-fix-list) is the source of truth
  for the `scripts/fixes/` step scripts.

## 1. Scope reviewed

The kernel module (`kernel/`, dominated by the ~9.8k-line `kernel/ibdev.c`), the
freestanding protocol/reliability code (`proto/`), and the CI/packaging surface
(`.github/`, `tools/ci/`, `packaging/`, `flake.nix`).

## 2. Status legend

- `open` — not yet addressed.
- `partial` — mitigated in one path but still open elsewhere.
- `fixed` — addressed on this branch or a merged PR (citation given).

## 3. Findings

### Security

#### S1 — Predictable rkey/lkey  ·  CRITICAL  ·  `fixed` (this branch, PR #1)
Memory keys were a global incrementing counter (`atomic_inc_return`), so a peer
could guess a valid `rkey` and perform arbitrary remote DMA. They are now drawn
from a CSPRNG with collision-retry and distinct `rkey`/`lkey`.
- Fixed: `kernel/ibdev.c:58` (`TBV_MR_KEY_MAX_ATTEMPTS`),
  `kernel/ibdev.c:9097-9098` (`get_random_u32()` with retry).
- Note: RDMA target bounds/overflow checks were already correct
  (`kernel/ibdev.c:6555-6557`, `6579-6585`), and DMA-MRs / implicit DMA lkey are
  already blocked. The weakness was key *unpredictability*, now resolved.

#### S2 — No peer authentication  ·  CRITICAL  ·  `open`
Trust is purely Thunderbolt topology: peers match on backend + `tb_xdomain`
only, with no challenge-response, shared secret, or per-peer ACL.
- `kernel/peer.c:45-49` (`tbv_peer_matches` checks `backend` and `xd` only).

#### S3 — Endpoint validation is plaintext-only  ·  HIGH  ·  `open`
`tbv_qp_validate_native_endpoint` checks only QPN equality; QPNs travel in
plaintext and can be observed/spoofed at the link layer.
- `kernel/ibdev.c:1142` (`tbv_qp_validate_native_endpoint`).

#### S4 — debugfs/configfs information exposure  ·  MEDIUM  ·  `open`
The debug surfaces expose QPNs, link IDs, route and proxy-IP state, and the
configfs link model is broadly accessible. These should be root-gated (or
compiled out) in a production build.
- `kernel/debugfs.c`, `kernel/configfs.c`.

### Robustness / correctness

#### R1 — Dedup/ACK window can wrap  ·  CRITICAL  ·  `partial`
The kernel ack-history is now sized to the outstanding-WR limit and indexed by
PSN, but the freestanding `proto/` reliability path still uses a 16-entry window
scanned linearly while up to `TBV_IBDEV_MAX_QP_WR` (1024) WRs can be outstanding,
so under heavy retransmission it can wrap and accept duplicates as new.
- Kernel side addressed: `kernel/ibdev.c:70` (`TBV_ACK_HISTORY_SIZE` =
  `TBV_IBDEV_MAX_QP_WR`), `kernel/ibdev.c:1240-1251`.
- Still open in `proto/`: `proto/reliability.h:23`
  (`TBV_REL_ACK_HISTORY_SIZE 16u`), `proto/reliability.c:52-54` (linear scan).

#### R2 — QP teardown can hang / race  ·  HIGH  ·  `open`
Destroy ends in an *untimed* `wait_for_completion`; a late RDMA-READ response in
flight can hang teardown, and there is a window between marking the QP closing
and disarming the timeout work.
- `kernel/ibdev.c:2718` (`wait_for_completion(&tqp->refs_zero)`).

#### R3 — Lock-hierarchy inconsistency  ·  HIGH  ·  `open`
Some paths take `peer->control_lock` then a state lock; others take only an
owner lock. Needs one documented, lockdep-annotated ordering.
- `kernel/ibdev.c:2783-2798`.

#### R4 — Hot-unplug drops in-flight WRs silently  ·  HIGH  ·  `open`
`rail->removing` is checked before queueing, but WRs queued just afterward are
dropped with no flush/error completion, and Apple tunnel teardown is warn-only.
- `kernel/ibdev.c:1795`, `2102-2125`, `2787`.

#### R5 — Weakly-bounded allocations  ·  MEDIUM  ·  `open`
No enforcement of advertised `max_qp`/`max_cq`/`max_mr` at create time, no
per-peer rate limiting / credit reservation; Apple pending-RX bytes are bounded
per QP but not globally per device.
- `kernel/ibdev.c:5606-5616` (per-QP pending-bytes overflow check).

#### R6 — Reliability retry has no backoff  ·  MEDIUM  ·  `open`
The retry interval ignores the retry budget (fixed interval, no exponential
backoff + jitter), inviting synchronized-retry collapse under congestion.
- `proto/reliability.c:216-219` (`tbv_rel_retry_interval` discards
  `retry_budget`).

#### R7 — `ibdev.c` monolith  ·  MEDIUM  ·  `open`
A ~9.8k-line file mixes native transport, Apple transport, CQ, MR, and the QP
state machine, which makes the security-critical paths hard to review.
- `kernel/ibdev.c` (whole file).

### Testing / CI / docs

#### T1 — CI is build-only  ·  `open`
The release/build workflow installs the DKMS module + provider across distros
but runs no RDMA data-path test; the smoke tests only exercise wire parsing.
- `.github/workflows/release-artefacts.yml`, `tools/ci/*-smoke.c`.

#### T2 — No kernel memory-safety / static analysis  ·  `open`
No checkpatch/sparse/smatch/clang-analyzer, no KASAN/KCSAN build, no fuzzing of
the `proto/` wire parsers; smoke builds are not `-Werror`.

#### T3 — Functional validation is manual & out-of-band  ·  `open`
Correctness today means "counters didn't move/err" on two physical machines,
not bit-verified transfers in CI.

#### T4 — Missing project docs  ·  `open`
No `SECURITY.md`, `CONTRIBUTING.md`, threat model, `proto/` wire-protocol spec,
or module-parameter reference.

## 4. Prioritized fix list

These map one-to-one onto the `scripts/fixes/NN-*.sh` automation. S1 is omitted
because it already shipped on this branch.

### P0 — safety/security blockers
| # | Fix | Findings | Script |
|---|-----|----------|--------|
| 01 | Bound the dedup/ACK window to outstanding WRs in `proto/` | R1 | `01-reliability-dedup-window.sh` |
| 02 | Make QP teardown bounded & race-free | R2 | `02-qp-teardown-bounded.sh` |
| 03 | Document and enforce a single lock ordering | R3 | `03-lock-ordering.sh` |

### P1 — production hardening
| # | Fix | Findings | Script |
|---|-----|----------|--------|
| 04 | Peer authentication + per-peer ACLs | S2, S3 | `04-peer-authentication.sh` |
| 05 | Graceful hot-unplug (flush in-flight WRs) | R4 | `05-graceful-hot-unplug.sh` |
| 06 | Enforce resource limits & rate limiting | R5 | `06-resource-limits.sh` |
| 07 | Root-gate debug/config surfaces | S4 | `07-root-gate-debug-surfaces.sh` |

### P2 — test/CI/quality infrastructure
| # | Fix | Findings | Script |
|---|-----|----------|--------|
| 08 | Automated data-path functional test in CI | T1, T3 | `08-datapath-functional-test.sh` |
| 09 | Fault-injection coverage for the reliability engine | T1 | `09-fault-injection-coverage.sh` |
| 10 | Kernel static analysis & memory-safety in CI | T2 | `10-kernel-static-analysis.sh` |
| 11 | Performance regression gating | T1 | `11-perf-regression-gating.sh` |

### P3 — maintainability & documentation
| # | Fix | Findings | Script |
|---|-----|----------|--------|
| 12 | Add reliability backoff + jitter | R6 | `12-reliability-backoff.sh` |
| 13 | Author project docs (SECURITY/CONTRIBUTING/protocol/params) | T4 | `13-project-docs.sh` |
| 14 | Split `ibdev.c` into reviewable modules | R7 | `14-split-ibdev.sh` |
