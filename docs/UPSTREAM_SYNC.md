# Upstream synchronization — v0.3.1 through post-v0.3.4 merges

> **Document status:** living reference — update each time upstream releases or
> a new batch of changes is evaluated.
>
> **Baseline:** this fork diverged from `hellas-ai/thunderbolt-ibverbs` at
> **v0.3.0** (commit `3b62b8bb`, 2026-06-11). All downstream work is tracked in
> [`docs/FINDINGS.md`](FINDINGS.md) (S/R/T fixes) and the open PR
> [#52 — GPU-direct Phase 1](https://github.com/pleite/thunderbolt-ibverbs/pull/52).
>
> **Purpose:** prevent regression by cataloguing every upstream change, assessing
> whether it applies to this fork, and specifying the exact integration path
> before any merge is attempted.

> **2026-06-24 addendum (new upstream merges):**
> upstream PRs **#44**, **#46**, **#49**, and **#50** are now merged and should
> be treated as active integration targets. The execution plan and per-feature
> launch specs are tracked in [`docs/upstream-sync/README.md`](upstream-sync/README.md)
> and launched with [`scripts/upstream-sync/`](../scripts/upstream-sync/).

---

## 1. Overview

Upstream released four patch versions in a single day (2026-06-16) containing
Apple transport correctness fixes, bench/tools hardening, and kernel-workflow
cleanup. Two additional PRs are open and awaiting hardware validation.

| Upstream version | Tag date | Merged PRs in batch | Theme |
|---|---|---|---|
| **v0.3.1** | 2026-06-16 | #35 | Apple UC stress + reload hardening |
| **v0.3.2** | 2026-06-16 | #36, #38 | Apple raw RX CRC verification + raw RX safety disable |
| **v0.3.3** | 2026-06-16 | #39, #40, #41, #42 | Apple TX window + login cadence + bench caps + predicate refactor |
| **v0.3.4** | 2026-06-16 | #43, #45, #47 | Tool guard + Apple gate preset + preflight |
| **open** | — | #44, #46 | Apple serialize all SENDs; deferred service publication retry |

Our downstream fork is at **v0.3.0pl** (tag `v0.3.0pl`, commit `568a666`).
The downstream-only PR **#52** (GPU-direct Phase 1) is a draft, cleanly rebased
on `main`, and adds code in new files / new struct fields that do not conflict
with any upstream Apple change.

---

## 2. Comprehensive change table

Each row covers one upstream PR (or one logical commit where a PR was not
opened). Columns:

- **Upstream PR / commit** — link and short title
- **Files changed** — files in the upstream repo
- **Our equivalent file(s)** — where the change lands in this fork (accounts for
  the R7 `ibdev.c` split from PR #45)
- **Applicability** — whether the change is relevant and safe to integrate
- **Conflict risk** — likelihood of a non-trivial merge conflict with our
  downstream additions
- **Integration path** — recommended action

### 2.1 v0.3.1 — bench/tools hardening (#35)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#35 bench/tools: harden Apple UC stress and reloads](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/35) |
| **Upstream commit** | `c604bc31` |
| **Files changed (upstream)** | `kernel/ibdev.c`, `kernel/tbnet_minimal.c`, `tools/tbv-target-module.sh`, `userspace/bench/tbv_uc_stress.py`, `kernel-workflow/patches/0113-…`, `kernel-workflow/patches/0122-…` (removed) |
| **Our equivalent file(s)** | Same paths (ibdev.c Apple TX section still in our `kernel/ibdev.c`; R7 moved Apple _rx dispatch_ into `kernel/ibdev_apple.c` but Apple TX admission stays in `kernel/ibdev.c`) |
| **What changed** | Conservative Apple framed-TX backpressure default (`apple_tx_max_inflight_wr` kept at 1); tbnet_minimal retry partial improvements; reload preflight added to `tools/tbv-target-module.sh`; `tbv_uc_stress.py` queue-depth caps; 0122 kernel patch removed |
| **Applicability** | ✅ **Applicable** — our fork has all these files; the Apple TX default (1) is unchanged by this commit and already matches our current default |
| **Conflict risk** | 🟡 **Low–medium** — `tbnet_minimal.c` was not modified by our downstream work; `tools/tbv-target-module.sh` may have minor contextual differences; the 0122 patch removal is clean |
| **Integration path** | Cherry-pick `c604bc31`; resolve any context-only conflicts in `tbnet_minimal.c` and `tbv-target-module.sh`. The `ibdev.c` hunk is a no-op (default kept at 1, same as our file). |

---

### 2.2 v0.3.2 — Apple raw RX CRC verification (#36)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#36 apple: verify raw RX chunk CRC](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/36) |
| **Upstream commit** | `4113742b` |
| **Files changed (upstream)** | `kernel/ibdev.c`, `kernel/tbv.h`, `kernel/debugfs.c`, `tools/ci/proto-smoke.c` |
| **Our equivalent file(s)** | `kernel/ibdev.c` (CRC functions), `kernel/tbv.h` (counter field), `kernel/debugfs.c` (summary), `tools/ci/proto-smoke.c` (new test) |
| **What changed** | Added `tbv_apple_rx_raw_crc_trailer()` helper; `tbv_apple_rx_verify_raw_crc()` function; `apple_rx_raw_crc_error` debugfs counter; `test_apple_raw_crc_model()` in proto-smoke |
| **Applicability** | ✅ **Applicable** — correctness fix for the raw-RX code path; all four target files exist in our fork. **Note:** this is only meaningful while `apple_rx_raw_mode` is honoured; it must be applied **before or together** with the raw-RX safety disable in #38 |
| **Conflict risk** | 🟡 **Medium** — `kernel/ibdev.c` CRC functions sit near `tbv_apple_rx_wire_user_len` (line 4844 in our file); `kernel/debugfs.c` was modified by our S4 security work (changed file modes, removed sensitive fields) — verify context lines; `tools/ci/proto-smoke.c` was extended by our downstream (reliability dedup tests in R1 — confirm no return-code collision) |
| **Integration path** | Cherry-pick `4113742b` and immediately follow with #38 (`87ac678c`). In `debugfs.c` confirm the new `seq_printf` line lands between the existing `apple_rx_len_overrun` and `apple_rx_resync_dropped` entries. In `proto-smoke.c` verify the new test returns `17` (upstream uses `17`; check our highest return value first — currently ≤16). |

---

### 2.3 v0.3.2 — Disable unsafe Apple raw RX mode (#38)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#38 apple: disable unsafe raw RX mode](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/38) |
| **Upstream commit** | `87ac678c` |
| **Files changed (upstream)** | `kernel/path.c` |
| **Our equivalent file(s)** | `kernel/path.c` |
| **What changed** | `MODULE_PARM_DESC(apple_rx_raw_mode, …)` updated to say it is a no-op; `tbv_path_apple_rx_raw_mode()` now emits `pr_warn_once` and returns `false` unconditionally |
| **Applicability** | ✅ **Strongly recommended** — safety hardening; raw descriptor boundaries are not proven message-safe. Our fork exposes the same param. Aligns with our security posture (fail-closed). |
| **Conflict risk** | 🟢 **Low** — `kernel/path.c` was not modified by our downstream work |
| **Integration path** | Cherry-pick `87ac678c`; expect clean apply. |

---

### 2.4 v0.3.3 — Apple single-frame TX fills frame window (#39)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#39 apple: let single-frame TX fill frame window](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/39) |
| **Upstream commit** | `6ea5714a` |
| **Files changed (upstream)** | `kernel/ibdev.c` |
| **Our equivalent file(s)** | `kernel/ibdev.c` |
| **What changed** | `static uint apple_tx_max_inflight_wr = 1` → `= TBV_APPLE_TX_MAX_INFLIGHT_FRAMES_DEFAULT` (4). Multi-frame SENDs remain on the exclusive window; only self-delimiting single-frame SENDs benefit. |
| **Applicability** | ✅ **Applicable** — performance improvement; does not touch security or reliability paths. **Dependency:** PR #44 (open, upstream) will override this by removing the fast path entirely. Evaluate in context of #44. |
| **Conflict risk** | 🟢 **Low** — only one line changes in `ibdev.c` at an area we did not modify |
| **Integration path** | Cherry-pick `6ea5714a`. **Requires** `TBV_APPLE_TX_MAX_INFLIGHT_FRAMES_DEFAULT` to be defined — check that the constant exists in `kernel/tbv.h` or `kernel/ibdev.c` in our fork. If absent, define it (currently the `apple_tx_max_inflight_frames` default is 64, but the `_WR` default is a separate constant in upstream). |

---

### 2.5 v0.3.3 — Shorten minimal login retry cadence (#40)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#40 apple: shorten minimal login retry cadence](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/40) |
| **Upstream commit** | `05c5bd1a` |
| **Files changed (upstream)** | `kernel/tbnet_minimal.c` |
| **Our equivalent file(s)** | `kernel/tbnet_minimal.c` |
| **What changed** | `TBV_TBNET_MIN_LOGIN_DELAY_MS` macro changed from `4500` → `1000`. Reduces Apple rail publication delay after module reload (macOS keeps half-open state; 4.5 s backoff was excessive). |
| **Applicability** | ✅ **Directly applicable** — our file defines the same macro at line 39 with value `4500`. Clean patch. Validated: 5/5 reload cycles at ~2 s vs prior ~5.5 s. |
| **Conflict risk** | 🟢 **Low** — single constant definition change; `tbnet_minimal.c` was not touched by our downstream work |
| **Integration path** | Cherry-pick `05c5bd1a` or apply manually: change `4500` → `1000` in the macro + update the comment above it. |

---

### 2.6 v0.3.3 — Respect actual QP WR caps in `uc_oneway` (#41)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#41 bench: respect actual uc_oneway QP WR caps](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/41) |
| **Upstream commit** | `b806442d` |
| **Files changed (upstream)** | `userspace/bench/uc_oneway.c` |
| **Our equivalent file(s)** | `userspace/bench/uc_oneway.c` |
| **What changed** | Introduced `send_depth` variable (separate from `wr_depth`); clamps `send_depth` and `initial_recvs` to provider-reported `max_send_wr`/`max_recv_wr` after QP creation; graceful partial-accept in `ibv_post_recv` loop; guards for `max_send_wr == 0` and `max_recv_wr == 0`. Fixes `ibv_post_recv[63/128] ret=-12` observed on macOS. |
| **Applicability** | ✅ **Applicable** — our fork ships `userspace/bench/uc_oneway.c`; our version currently has no WR-cap clamping (line 837 posts `o.depth` unclamped). The fix is purely additive and improves interoperability with Apple hardware. |
| **Conflict risk** | 🟢 **Low** — bench tools were not modified by our downstream security/reliability work |
| **Integration path** | Cherry-pick `b806442d`. Large diff (+59 lines) but concentrated; no interaction with our changes. |

---

### 2.7 v0.3.3 — Share TX window admission predicate (#42)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#42 apple: share TX window admission predicate](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/42) |
| **Upstream commit** | `d5095e37` |
| **Files changed (upstream)** | `kernel/ibdev.c` |
| **Our equivalent file(s)** | `kernel/ibdev.c` |
| **What changed** | Extracted `tbv_qp_apple_tx_window_ok_locked()` helper from the duplicated inline logic inside `tbv_qp_try_acquire_apple_tx_window()` and `tbv_qp_apple_tx_window_available()`. Pure refactor — no behaviour change. **Dependency for PR #44.** |
| **Applicability** | ✅ **Applicable** — our fork has both functions in `kernel/ibdev.c` (lines 5218–5295). Refactor reduces duplication. |
| **Conflict risk** | 🟡 **Medium** — the target functions are in a section of `ibdev.c` that still exists in our fork; line numbers may differ from upstream due to our QP teardown fix (R2). Confirm context before cherry-pick. |
| **Integration path** | Cherry-pick `d5095e37`. If context lines differ, apply manually: add the new `tbv_qp_apple_tx_window_ok_locked()` static function immediately before `tbv_qp_try_acquire_apple_tx_window()` and replace the duplicated `wr_ok`/`frames_ok` logic in both callers. |

---

### 2.8 v0.3.4 — Guard minimal-packet target reloads (#43)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#43 tools: guard minimal-packet target reloads](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/43) |
| **Upstream commit** | `a712d2dd` |
| **Files changed (upstream)** | `tools/tbv-target-module.sh` |
| **Our equivalent file(s)** | `tools/tbv-target-module.sh` |
| **What changed** | `tools/tbv-target-module.sh` gains reload preflight guards to prevent silent failure when the module target is not ready. |
| **Applicability** | ✅ **Applicable** — our fork ships the same script; this is an operational hardening improvement. |
| **Conflict risk** | 🟢 **Low** — script was not modified by our downstream work |
| **Integration path** | Cherry-pick `a712d2dd`. Script is 386 lines in our fork; verify line numbers match or do a manual context diff. |

---

### 2.9 v0.3.4 — Apple UC gate preset (#45)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#45 bench: add Apple UC gate preset](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/45) |
| **Upstream commit** | `b92dc812` |
| **Files changed (upstream)** | `userspace/bench/tbv_uc_stress.py` |
| **Our equivalent file(s)** | `userspace/bench/tbv_uc_stress.py` |
| **What changed** | Adds `--apple-gate` preset to `tbv_uc_stress.py`; runs a defined test matrix against Apple transport peers. |
| **Applicability** | ✅ **Applicable** — we have Apple transport and the same stress script. Useful for CI gate. |
| **Conflict risk** | 🟢 **Low** — our bench tools are unchanged from upstream v0.3.0 baseline |
| **Integration path** | Cherry-pick `b92dc812`. Apply after or together with #47 (preflight). |

---

### 2.10 v0.3.4 — Preflight Apple UC gate devices (#47)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#47 bench: preflight Apple UC gate devices](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/47) |
| **Upstream commit** | `a3b549c0` |
| **Files changed (upstream)** | `userspace/bench/tbv_uc_stress.py` |
| **Our equivalent file(s)** | `userspace/bench/tbv_uc_stress.py` |
| **What changed** | Adds `--preflight` / `--preflight-only`; verifies RDMA devices are listed by `ibv_devices` and report `PORT_ACTIVE` before starting traffic; adds `--ssh-config` / repeated `--ssh-option` for jump-host topologies. |
| **Applicability** | ✅ **Applicable** — operational improvement; prevents wasted runs against stale device names. |
| **Conflict risk** | 🟢 **Low** |
| **Integration path** | Cherry-pick `a3b549c0`. Should apply after or together with #45. |

---

### 2.11 Open — Serialize all Apple UC SENDs per QP (#44)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#44 apple: serialize all UC SENDs per QP](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/44) — **OPEN, not yet merged** |
| **Upstream commit** | `a14a175` (head of `codex/apple-serialize-all-send`) |
| **Files changed (upstream)** | `kernel/ibdev.c`, `proto/apple_tx.h` (new), `tools/ci/proto-smoke.c`, `kernel/Makefile` |
| **What changed** | Removes the single-frame fast path added in #39; routes **all** non-empty Apple UC SENDs through the exclusive per-QP window. FA57 has no message sequence/incarnation; short burst corruption was observed. Factors admission rule into a new `proto/apple_tx.h`. Deprecates `apple_tx_max_inflight_wr`. |
| **Applicability** | ⚠️ **Hold pending hardware validation** — upstream itself is waiting for a Linux↔macOS path to validate. If/when merged upstream, this is a **correctness fix** (corruption observed) and should be applied. It supersedes #39 and #42. |
| **Conflict risk** | 🔴 **High** — significantly reworks the Apple TX admission area in `kernel/ibdev.c` that overlaps with R2 (QP teardown) changes we made; adds a new `proto/` header; modifies `proto-smoke.c` return codes |
| **Integration path** | **Do not cherry-pick until upstream merges.** When upstream merges #44: (1) fetch the merged commit, (2) rebase our fork's main, (3) resolve conflicts in `kernel/ibdev.c` (our R2 QP teardown and R7 split context), (4) confirm `proto-smoke.c` return-code sequence is contiguous. Apply on top of #42 (predicate refactor). |

---

### 2.12 Open — Retry deferred Apple service publication (#46)

| Attribute | Detail |
|---|---|
| **Upstream PR** | [#46 apple: retry deferred service publication](https://github.com/hellas-ai/thunderbolt-ibverbs/pull/46) — **OPEN, not yet merged** |
| **Upstream commit** | `b1a6a76b` (head of `codex/apple-service-publish-retry`) |
| **Files changed (upstream)** | `kernel/service.c`, `kernel/tbv.h` (likely), `kernel/apple.c` or similar |
| **What changed** | Makes Apple rail publication worker-owned; retries transient `tb_xdomain_enable_paths()` failures instead of leaving rail stranded. Adds `READ_ONCE`/`WRITE_ONCE` on `apple_rails_pending`. |
| **Applicability** | ✅ **Applicable when merged** — robustness fix for Apple paths; no conflict with our peer-auth or security work |
| **Conflict risk** | 🟡 **Medium** — touches `kernel/service.c` / `kernel/apple.c`; our R4 (graceful hot-unplug) modified QP flush but not service publication |
| **Integration path** | **Do not cherry-pick until upstream merges.** Watch the PR; when merged, cherry-pick and confirm no interaction with our `kernel/service.c` changes. |

---

### 2.13 Downstream — GPU-direct Phase 1 (PR #52)

| Attribute | Detail |
|---|---|
| **Our PR** | [#52 Phase 1: gated kernel reg_user_mr_dmabuf op for GPU-direct dma-buf MRs](https://github.com/pleite/thunderbolt-ibverbs/pull/52) — **OPEN draft** |
| **Files changed** | `kernel/ibdev_mr.c`, `kernel/ibdev_internal.h`, `kernel/ibdev_split.h`, `kernel/main.c`, `kernel/Makefile`, `kernel/Kconfig`, `docs/MODULE_PARAMETERS.md`, `docs/gpu-direct-plan.md` |
| **What changed** | Implements GDP-1: `tbv_reg_dmabuf_mr()` behind `CONFIG_TBV_GPU_DIRECT` build gate and `gpu_direct=auto|on|off` load-time param. `struct tbv_mr` gains `umem_dmabuf`+`dmabuf_mr` fields. Data-path copy helpers remain on host-copy path. |
| **Conflict risk with upstream** | 🟢 **None** — touches only MR files (`ibdev_mr.c`, `ibdev_internal.h`); upstream changes are all Apple-transport files |
| **Conflict risk with upstream #44/#46** | 🟢 **None** — Apple TX/publish vs. MR registration are independent subsystems |
| **Regression risk** | 🟢 **Minimal** — all changes are behind build gate (`CONFIG_TBV_GPU_DIRECT=n` default); host-copy path is entirely unchanged. |

---

## 3. Applicability summary and integration priority

| Priority | Upstream PR / commit | Risk | Our action |
|---|---|---|---|
| 🔴 **P0 — Security/safety** | #38 disable raw RX mode (`87ac678c`) | Low | Cherry-pick immediately |
| 🟡 **P1 — Correctness** | #36 Apple raw RX CRC verify (`4113742b`) | Medium | Cherry-pick (pair with #38) |
| 🟡 **P1 — Correctness** | #41 uc_oneway WR cap clamping (`b806442d`) | Low | Cherry-pick |
| 🟢 **P2 — Reliability** | #40 login retry cadence (`05c5bd1a`) | Low | Cherry-pick |
| 🟢 **P2 — Reliability** | *(open)* #46 service publication retry | Medium | Watch; cherry-pick when merged |
| 🟢 **P2 — Performance** | #39 Apple single-frame TX default (`6ea5714a`) | Low | Cherry-pick (**superseded by #44 if merged**) |
| 🔵 **P3 — Refactor** | #42 TX window predicate extract (`d5095e37`) | Medium | Cherry-pick (prerequisite for #44) |
| 🔵 **P3 — Correctness** | *(open)* #44 serialize all UC SENDs | High | Watch; cherry-pick when upstream merges |
| 🔵 **P3 — Tooling** | #35 bench/tools reload hardening (`c604bc31`) | Low | Cherry-pick |
| 🔵 **P3 — Tooling** | #43 target reload guard (`a712d2dd`) | Low | Cherry-pick |
| 🔵 **P3 — Tooling** | #45 Apple UC gate preset (`b92dc812`) | Low | Cherry-pick |
| 🔵 **P3 — Tooling** | #47 preflight gate devices (`a3b549c0`) | Low | Cherry-pick |
| ⬜ **Hold** | *(open)* #44, #46 | High/Medium | Block on upstream hardware validation |

---

## 4. Regression analysis — downstream changes vs. upstream patches

This section maps each of our downstream changes (v0.3.0pl findings) against the
upstream patches to confirm **no regression** is introduced by integrating them.

| Our change | Files affected | Upstream patches that touch same files | Interaction |
|---|---|---|---|
| **S1** CSPRNG rkey/lkey | `kernel/ibdev_mr.c` | None | ✅ No conflict |
| **S2/S3** Peer auth (PSK) | `kernel/native_control.c`, `kernel/peer.c`, `kernel/main.c` | None | ✅ No conflict |
| **S4** Root-gate debug surfaces | `kernel/debugfs.c`, `kernel/configfs.c` | #36 adds one `seq_printf` to debugfs | ⚠️ Apply #36 carefully; new counter line must land between our modified context lines |
| **R1** Dedup window | `proto/reliability.c`, `proto/native_wire.h` | None | ✅ No conflict |
| **R2** QP teardown | `kernel/ibdev_qp.c` | None | ✅ No conflict |
| **R3** Lock ordering | `kernel/ibdev_qp.c`, `kernel/ibdev.c` | #42 refactors Apple TX area of `ibdev.c` | ⚠️ Verify lock ordering annotations survive the `ibdev.c` refactor |
| **R4** Hot-unplug flush | `kernel/ibdev_qp.c`, `kernel/ibdev_native.c` | None | ✅ No conflict |
| **R5** Resource limits | `kernel/ibdev_qp.c`, `kernel/ibdev_mr.c`, `kernel/main.c` | None | ✅ No conflict |
| **R6** Backoff | `proto/reliability.c` | None | ✅ No conflict |
| **R7** `ibdev.c` split | `kernel/ibdev.c` → `ibdev_{cq,mr,qp,native,apple}.c` | All upstream `ibdev.c` patches land in sections **not** moved by R7 (Apple TX/RX admission stayed in `ibdev.c`; `ibdev_apple.c` only contains the RX dispatch entry) | ✅ No conflict — Apple TX functions remain in `kernel/ibdev.c` |
| **T1–T4** CI / docs | `.github/`, `docs/`, `tools/ci/` | #36 modifies `tools/ci/proto-smoke.c` | ⚠️ Verify proto-smoke return-code sequence (see §5 below) |
| **GDP-1** (PR #52) | `kernel/ibdev_mr.c`, `kernel/ibdev_internal.h` | None | ✅ No conflict |

---

## 5. Integration notes and pitfalls

### 5.1 `tools/ci/proto-smoke.c` return-code sequence

Upstream #36 adds `test_apple_raw_crc_model()` returning exit code `17`. Our
current `proto-smoke.c` has tests returning up to exit code `16`. Confirm the
sequence before applying:

```bash
grep "return 1[0-9]" tools/ci/proto-smoke.c
```

Expected: highest is `16` → the new `17` slot is free. If a downstream test was
added at `17`, renumber it to `18` before cherry-picking #36.

### 5.2 `kernel/debugfs.c` — S4 context

Our S4 fix changed file modes and removed sensitive fields from the `summary`
output. The upstream #36 `seq_printf` for `apple_rx_raw_crc_error` must be
inserted **between** the existing `apple_rx_len_overrun` and
`apple_rx_resync_dropped` entries — confirm neither of those was moved by S4.

```bash
grep -n "apple_rx_len_overrun\|apple_rx_resync_dropped" kernel/debugfs.c
```

### 5.3 `TBV_APPLE_TX_MAX_INFLIGHT_FRAMES_DEFAULT` constant (#39)

Upstream #39 references `TBV_APPLE_TX_MAX_INFLIGHT_FRAMES_DEFAULT`. In the
upstream repository this constant is defined in `kernel/tbv.h` or `kernel/ibdev.c`.
Confirm it exists in our fork before cherry-picking:

```bash
grep -rn "TBV_APPLE_TX_MAX_INFLIGHT_FRAMES_DEFAULT" kernel/
```

If absent: the constant equals the existing `apple_tx_max_inflight_frames`
default (4 in upstream v0.3.3). Add `#define TBV_APPLE_TX_MAX_INFLIGHT_FRAMES_DEFAULT 4`
adjacent to `apple_tx_max_inflight_frames` in `kernel/ibdev.c`.

### 5.4 Open PR #44 supersedes #39 and #42

If upstream merges #44 (serialize all UC SENDs), it reverses the `apple_tx_max_inflight_wr`
default change from #39 and makes `apple_tx_max_inflight_wr` deprecated.
Integration order matters:

```
#36 → #38 → #40 → #41 → #42 → #39 → [if #44 merges] #44
```

Do **not** apply #39 after #44 has landed upstream.

### 5.5 kernel-workflow patch 0122 removal (#35)

Upstream #35 removes `kernel-workflow/patches/0122-thunderbolt-xdomain-property-fixes.patch`
(148 lines). Our fork has this file. The removal is safe because the patch was
merged upstream into the main kernel tree and is no longer needed in the
out-of-tree patch stack. Confirm via:

```bash
git show hellas-ai/v0.3.1:kernel-workflow/patches/local-portable.nix 2>/dev/null | grep 0122
```

If the patch is no longer referenced in `local-portable.nix`, delete it.

---

## 6. Recommended merge order (resolved)

Apply in this exact order to avoid dependency failures and minimise conflicts:

```
Step 1 — Safety (apply together as one PR):
  a. #38  87ac678c  apple: disable unsafe raw RX mode
  b. #36  4113742b  apple: verify raw RX chunk CRC
  (apply #38 first so the CRC code is gated on a now-unconditionally-false predicate)

Step 2 — Reliability/performance (can be one PR):
  c. #40  05c5bd1a  apple: shorten minimal login retry cadence
  d. #41  b806442d  bench: respect actual uc_oneway QP WR caps
  e. #42  d5095e37  apple: share TX window admission predicate
  f. #39  6ea5714a  apple: let single-frame TX fill frame window
  (f after e; skip f entirely if upstream merges #44 before this step)

Step 3 — Tooling (one PR):
  g. #35  c604bc31  bench/tools: harden Apple UC stress and reloads
  h. #43  a712d2dd  tools: guard minimal-packet target reloads
  i. #45  b92dc812  bench: add Apple UC gate preset
  j. #47  a3b549c0  bench: preflight Apple UC gate devices

Step 4 — Hold (integrate only after upstream hardware validation + merge):
  k. #44  (open)   apple: serialize all UC SENDs per QP
  l. #46  (open)   apple: retry deferred service publication

Step 5 — Downstream (already open, independent):
  m. PR #52  GPU-direct Phase 1 (merge after #44/#46 decision is known)
```

---

## 7. What to verify after integration

Run these checks after each step above to confirm no regression:

```bash
# Build
make -C kernel clean
make -C kernel modules KDIR=/lib/modules/$(uname -r)/build

# Proto smoke (includes new CRC test after step 1)
make -C proto test

# CI functional (requires soft RDMA backend)
tools/ci/datapath-functional.sh

# Kernel hygiene
scripts/checkpatch.pl --strict -f kernel/ibdev.c kernel/path.c \
  kernel/tbnet_minimal.c kernel/debugfs.c

# Perf regression gate (requires two-node hardware)
nix run .#tbv-regression
```

Performance baseline in `bench/perftest-smoke-baseline.csv` is not affected by
any upstream Apple-transport patch (all changes are latency/correctness on the
Apple path, not the native Linux-to-Linux path that the baseline covers).

---

## 8. Document maintenance

| Event | Action |
|---|---|
| Upstream releases v0.3.5+ | Add rows in §2; update §3 priority table; re-run §7 checks after integration |
| Upstream merges PR #44 or #46 | Move from "Hold" to appropriate priority; update §4 conflict analysis |
| Downstream PR #52 merges | Update §2.13 status; add GDP-2/GDP-3 rows when planned |
| New downstream finding | Add row to §4 regression table |

> Last reviewed against upstream HEAD `a3b549c0` (v0.3.4, 2026-06-16) and
> downstream HEAD `568a666` (v0.3.0pl, 2026-06-16).
