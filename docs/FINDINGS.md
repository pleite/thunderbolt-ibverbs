# Driver performance & security findings

This document records the findings from the performance/security analysis of the
`thunderbolt-ibverbs` driver done on the `analyze-driver-performance-security`
branch. It turns the README's "buggy, insecure, not for production" caveat
(`README.md`) into a concrete, cited, and prioritized list of work.

Each finding has a stable ID (`S*` security, `R*` robustness/correctness,
`T*` testing/CI/docs). Every finding below has now been addressed; the numbered
automation in [`scripts/archive/fixes/`](../scripts/archive/fixes/) filed one
issue + branch + draft PR per finding, and all of those PRs have merged. The
toolkit is therefore archived alongside the (earlier) roadmap automation.

## How to use this file

- Reference a finding by its ID in issues, PRs, and code comments.
- All findings are now `fixed`; each entry cites the PR that addressed it.
- The prioritized list in [§4](#4-prioritized-fix-list) records how the work was
  sequenced for the (now archived) `scripts/archive/fixes/` step scripts.

## 1. Scope reviewed

The kernel module (`kernel/`, at analysis time dominated by the ~9.8k-line
`kernel/ibdev.c`, since split per R7), the
freestanding protocol/reliability code (`proto/`), and the CI/packaging surface
(`.github/`, `tools/ci/`, `packaging/`, `flake.nix`).

## 2. Status legend

- `open` — not yet addressed.
- `partial` — mitigated in one path but still open elsewhere.
- `fixed` — addressed on this branch or a merged PR (citation given).

> **All findings below are now `fixed`.** The analysis is complete and every
> item has shipped; the `fixed` citations point at the merged work.

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

#### S2 — No peer authentication  ·  CRITICAL  ·  `fixed` (PR #25)
Native peers now require `peer_auth_acl=<uuid=32hexpsk[,...]>`; the ACL both
authorizes the remote UUID and supplies the PSK used for session
authentication, replacing pure Thunderbolt-topology trust.
- Fixed: `kernel/main.c:135-269` (`peer_auth_acl` param + parsing),
  `kernel/peer.c:189-205`, `docs/SECURITY.md:41-60`.
- Original: `kernel/peer.c:45-49` matched on `backend` and `xd` only.

#### S3 — Endpoint validation is plaintext-only  ·  HIGH  ·  `fixed` (PR #25)
`tbv_qp_validate_native_endpoint` now gates the plaintext QPN check behind
`tbv_qp_native_session_matches()`, binding acceptance to an authenticated
native session rather than the QPN alone.
- Fixed: `kernel/ibdev.c:753-779` (session match before QPN/peer checks).

#### S4 — debugfs/configfs information exposure  ·  MEDIUM  ·  `fixed` (PR #31)
The debug/config surfaces are root-gated: debugfs attributes are created mode
`0400` and configfs attributes are `0400`/`0600`, so unprivileged users can no
longer read QPNs, link IDs, or route/proxy state.
- Fixed: `kernel/debugfs.c:511-515`, `kernel/configfs.c:454-495`.

### Robustness / correctness

#### R1 — Dedup/ACK window can wrap  ·  CRITICAL  ·  `fixed` (PR #19)
The freestanding `proto/` reliability path now sizes its ack-history to
`TBV_REL_ORDER_MAX` and indexes by `op_id`, matching the outstanding-WR limit so
it can no longer wrap and accept duplicates as new. The kernel side was already
sized to the outstanding-WR limit.
- Fixed: `proto/reliability.h:26-30` (`TBV_REL_ACK_HISTORY_SIZE` =
  `TBV_REL_ORDER_MAX`), `proto/reliability.h:131`.
- Kernel side: `kernel/ibdev.c:70` (`TBV_ACK_HISTORY_SIZE`),
  `kernel/ibdev.c:1240-1251`.

#### R2 — QP teardown can hang / race  ·  HIGH  ·  `fixed` (PR #21)
QP destroy no longer blocks on an untimed completion behind in-flight RDMA work;
sends, reads, and read-responses are flushed/cancelled with error completions so
teardown is bounded and race-free.
- Fixed: `kernel/ibdev_qp.c:189-235` (flush sends/reads/read-resps on destroy).

#### R3 — Lock-hierarchy inconsistency  ·  HIGH  ·  `fixed` (PR #23)
A single documented ordering (`peer->control_lock` → owner `lock`) is now
enforced with `lockdep_assert_held()` annotations on the shared paths.
- Fixed: `kernel/ibdev.c:2171-2172`, `kernel/ibdev.c:2225-2226`.

#### R4 — Hot-unplug drops in-flight WRs silently  ·  HIGH  ·  `fixed` (PR #27)
In-flight WRs are now flushed with error completions on hot-unplug instead of
being dropped silently.
- Fixed: `kernel/ibdev_qp.c:189-235` (flush list drained to error completions).

#### R5 — Weakly-bounded allocations  ·  MEDIUM  ·  `fixed` (PR #29)
Advertised `max_qp`/`max_cq`/`max_mr` are now enforced at create time via atomic
device counters, with per-peer rate limiting, rejecting over-budget allocations.
- Fixed: `kernel/ibdev_qp.c:70-73` (atomic `verbs_qps` vs `max_qp`).

#### R6 — Reliability retry has no backoff  ·  MEDIUM  ·  `fixed` (issue #40)
`tbv_rel_retry_interval` now derives an exponential backoff from the retry
budget and applies bounded jitter, avoiding synchronized-retry collapse, with a
test asserting the schedule.
- Fixed: `proto/reliability.c:209-233` (`backoff_shift`, jitter, overflow cap).

#### R7 — `ibdev.c` monolith  ·  MEDIUM  ·  `fixed` (PR #45)
The monolith is split into per-subsystem units (MR/CQ/QP/native/apple) sharing
internals via `kernel/ibdev_internal.h` and public verb-op prototypes via
`kernel/ibdev_split.h`, making the security-critical paths reviewable.
- Fixed: `kernel/ibdev_mr.c`, `kernel/ibdev_cq.c`, `kernel/ibdev_qp.c`,
  `kernel/ibdev_native.c`, `kernel/ibdev_apple.c`, `kernel/ibdev_internal.h`,
  `kernel/ibdev_split.h`.

### Testing / CI / docs

#### T1 — CI is build-only  ·  `fixed` (PRs #33, #35, #39)
The release workflow now runs a virtual two-node software-RDMA data-path test
(`ib_write_bw`/`ib_send_bw` + `rping`), fault-injection coverage for the
reliability engine, and performance-regression gating in addition to the build.
- Fixed: `tools/ci/datapath-functional.sh`,
  `.github/workflows/release-artefacts.yml`, `bench/perftest-smoke-baseline.csv`.

#### T2 — No kernel memory-safety / static analysis  ·  `fixed` (PR #37)
CI adds a kernel-hygiene job: diff-based checkpatch, sparse (warnings as
errors), optional smatch, sanitizer-targeted module builds, and optional
`proto/` wire-parser fuzzing.
- Fixed: `.github/workflows/release-artefacts.yml:135-252`.

#### T3 — Functional validation is manual & out-of-band  ·  `fixed` (PR #33)
Functional correctness is now validated in CI by the automated data-path test
with error-counter delta checks, rather than manual two-machine runs.
- Fixed: `tools/ci/datapath-functional.sh:1-249`.

#### T4 — Missing project docs  ·  `fixed` (PR #43)
The project now ships `SECURITY.md`, `CONTRIBUTING.md`, a `proto/` wire-protocol
spec, and a module-parameter reference.
- Fixed: `SECURITY.md`, `CONTRIBUTING.md`, `proto/WIRE_PROTOCOL.md`,
  `docs/MODULE_PARAMETERS.md`.

## 4. Prioritized fix list

These mapped one-to-one onto the `scripts/archive/fixes/NN-*.sh` automation (now
archived). All have shipped. S1 is omitted because it already shipped on this
branch.

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
