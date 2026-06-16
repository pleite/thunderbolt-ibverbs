# Release notes — v0.3.1pl (fork of upstream v0.3.0)

Downstream `pleite/thunderbolt-ibverbs`. This is an incremental release on top
of [`v0.3.0pl`](#release-notes--v030pl-fork-of-upstream-v030) that adds optional,
gated GPU-direct RDMA support and a documented upstream-sync path.

> Note: distribution packages (DKMS + userspace provider) are versioned `0.3.1`
> to keep Debian/RPM/Arch/Nix version strings valid; the Git tag and GitHub
> release for this downstream snapshot are named `v0.3.1pl`.

**Full comparison:** `v0.3.0pl...v0.3.1pl`

## 🚀 GPU-direct RDMA (dma-buf) — optional and gated

Brings up GPU-direct RDMA over the Thunderbolt transport using dma-buf memory
regions. The feature is fully optional: disabled unless built with
`CONFIG_TBV_GPU_DIRECT` (out-of-tree `tbv_gpu_direct=1`) and enabled at load
time via `gpu_direct=auto|on|off`. Delivered in four phases:

- **Phase 1** — `tbv_reg_dmabuf_mr()` dma-buf MR registration plumbing.
- **Phase 2** — dma-buf-aware data-path copy helpers.
- **Phase 3** — RCCL/NCCL GPU-direct capability advertising and docs.
- **Phase 4** — dynamic (move-notify) dma-buf via `gpu_direct_dynamic=0|1`,
  mapping under the dma-buf reservation lock, with a guard that excludes
  dma-buf MRs from ring zero-copy.

See [`docs/gpu-direct-plan.md`](docs/gpu-direct-plan.md) for the full design.

## 📚 Documentation

- [`docs/UPSTREAM_SYNC.md`](docs/UPSTREAM_SYNC.md) — catalogues upstream
  `v0.3.1`–`v0.3.4` changes and the downstream integration path.
- [`docs/vllm-toolbox-integration.md`](docs/vllm-toolbox-integration.md) — vLLM
  toolbox integration guide.

## 🧱 Packaging

- Version strings bumped from `0.3.0` to `0.3.1` across `dkms.conf`, `flake.nix`,
  `nix/module.nix`, `nix/bench-tools.nix`, the RPM specs, and the README install
  URL.
- Refreshed `flake.lock` (nixpkgs bump).

---

# Release notes — v0.3.0pl (fork of upstream v0.3.0)

Downstream `pleite/thunderbolt-ibverbs`, diverged from upstream
`hellas-ai/thunderbolt-ibverbs` at **v0.3.0**.

This release takes the research driver from "buggy, insecure, not for
production" toward a security-hardened, tested, and documented state. It
delivers the full 8-step project roadmap **and** a 14-item security/robustness
analysis fix list (findings `S*`/`R*`/`T*`), plus supporting CI, benchmarks,
and documentation.

> Note: distribution packages (DKMS + userspace provider) are versioned `0.3.0`
> to keep Debian/RPM/Arch/Nix version strings valid; the Git tag and GitHub
> release for this downstream snapshot are named `v0.3.0pl`.

**Full comparison:** `v0.3.0...pleite:main`

---

## 🔐 Security hardening

- **Unpredictable memory keys (S1, CRITICAL).** MR `rkey`/`lkey` values are now
  drawn from a CSPRNG with collision-retry and are distinct per MR, instead of a
  global incrementing counter — a remote peer can no longer guess a valid `rkey`
  to perform arbitrary remote DMA.
- **Native peer authentication + per-peer PSK ACLs (S2/S3, CRITICAL/HIGH).** New
  `peer_auth_acl=<uuid=32hexpsk,...>` module parameter. Native Linux peers must
  complete a nonce + PSK HELLO/READY/ACK handshake (SipHash-derived per-peer
  session) before a rail becomes data-ready; endpoint acceptance is bound to the
  authenticated session rather than a plaintext QPN.
- **Optional peer allow-listing.** New `peer_allowlist=<uuid,...>` parameter;
  unlisted peers are rejected (fails closed when the UUID is unavailable).
- **Root-gated debug surfaces (S4, MEDIUM).** debugfs attributes are now `0400`
  and configfs attributes `0400`/`0600`; sensitive fields (proxy IPs, identity
  netdev names) removed from `summary`. New `production_mode=1` disables both
  surfaces at runtime, and `tbv_debug_surfaces=0` /
  `CONFIG_THUNDERBOLT_IBVERBS_DEBUG_SURFACES=n` compiles them out entirely.
- New top-level `SECURITY.md` and `docs/SECURITY.md` documenting the trust
  boundary, threat model, and implemented hardening.

## 🛡️ Robustness & correctness

- **Bounded dedup/ACK window (R1, CRITICAL).** The freestanding `proto/`
  reliability ACK/dedup history is resized to `TBV_REL_ORDER_MAX` and indexed by
  `op_id`, so it can no longer wrap and accept duplicates as new.
- **Bounded, race-free QP teardown (R2, HIGH).** QP destroy no longer waits
  untimed behind in-flight RDMA work; sends/reads/read-responses are
  flushed/cancelled with error completions.
- **Single enforced lock ordering (R3, HIGH).** A documented
  `peer->control_lock → owner lock → qp lock` order is enforced with
  `lockdep_assert_held()` annotations.
- **Graceful hot-unplug (R4, HIGH).** In-flight WRs are flushed to error
  completions on rail removal instead of being silently dropped; new
  `tbv_ibdev_flush_rail_qps()` drives this.
- **Enforced resource limits (R5, MEDIUM).** Advertised `max_qp`/`max_cq`/`max_mr`
  are enforced at create time via atomic counters, with per-peer send-queue
  reservation (`peer_sendq_reserved_max`) and a global per-device Apple
  pending-RX byte cap.
- **Reliability retry backoff (R6, MEDIUM).** `tbv_rel_retry_interval()` now uses
  exponential backoff with bounded jitter to avoid synchronized-retry collapse.
- **`ibdev.c` split (R7, MEDIUM).** The ~9.8k-line monolith is split into
  reviewable units — `ibdev_cq.c`, `ibdev_mr.c`, `ibdev_qp.c`,
  `ibdev_native.c`, `ibdev_apple.c` — sharing `ibdev_internal.h` /
  `ibdev_split.h` with no behavior change.
- Native wire HELLO message size corrected (`40` → `68` bytes) to carry the new
  auth fields and to fix an OOB read found by CI fuzzing.

## ⚙️ Kernel compatibility

- Maintainer-tree Thunderbolt dependencies are now isolated behind a new
  `tbv_compat.{c,h}` shim layer:
  - `tb_ring_throttling()` is feature-detected (parameter accepted but ignored,
    with a `pr_warn_once`, when unavailable).
  - `tb_phy_port_from_link()` replaced by a shimmed
    `(link - 1) / TB_LINKS_PER_PHY_PORT` derivation.
- `reg_user_mr` `ib_dmah` parameter is detected at build time
  (`TBV_KERNEL_HAS_IB_DMAH`), working on both monolithic and split-header
  (Debian/Ubuntu `*-common`) distro layouts.
- Documented kernel/feature compatibility matrix; the module now targets stock
  kernels (tested 6.17) with documented feature degradation below 6.14.

## 🚀 Performance & tuning

- Parametric tuning sweep tool `userspace/bench/tbv_tuning_sweep.py` plus
  committed results under `bench/results/strix-2p-noiommu-2x40g/tuning.md`.
- **Default change:** `zcopy_min_bytes` now defaults to `4096` (was `0`) — ~17%
  bandwidth gain at 1 MiB with safe fallback.
- Nix bench config now sets `nhi_interrupt_throttle_ns=50000` (≈+10%
  large-message BW). `native_wr_striping` parameter removed;
  `native_fragment_striping` retained (recommended `1` on ≥4-rail topologies for
  ~7% BW).
- New `docs/TUNING.md` with per-parameter guidance, sweep tables, and diagnostic
  counter help.

## 🧪 Testing & CI

- **Data-path functional test (T1/T3).** `tools/ci/datapath-functional.sh` runs a
  virtual two-node software-RDMA (`rdma_rxe`/`siw`) suite —
  `ib_write_bw`/`ib_send_bw` + bit-verified `rping` — and fails on any
  error-counter movement.
- **Kernel hygiene (T2).** New CI job: diff-based `checkpatch`, `sparse`
  (warnings-as-errors), optional `smatch`, KASAN/KCSAN build targets, and
  optional `proto/` wire-parser fuzzing; smoke/distro builds now use `-Werror`.
- **Performance regression gating (T1).** Committed
  `bench/perftest-smoke-baseline.csv` baseline; `bench/summarize_perftest.py`
  gains baseline comparison with `--bw-drop-pct` / `--lat-rise-pct` thresholds.
- **Fault-injection coverage (T1).** New `proto/test_reliability_dedup.c` (+
  `proto/Makefile`) and expanded `reliability-smoke.c` covering lost
  ACK/credit/data, duplicate retry, RNR exhaustion, READ-response retry, and
  teardown-while-in-flight.
- One-command regression suite `nix run .#tbv-regression`
  (`userspace/bench/tbv_regression_suite.py`) running transport + verbs smoke
  with per-run artifacts, wired to `.github/workflows/regression-self-hosted.yml`.
- New hot-unplug-with-traffic smoke `userspace/bench/tbv_hot_unplug_inflight.sh`,
  Apple↔Linux interop smoke `tools/ci/apple-linux-interop-smoke.sh`, and
  `tbv_perftest_runner.py` between-repeat fault-injection hooks.
- Packaging-sync CI check `tools/ci/check-packaging-sync.sh`; the release job now
  depends on `[build, nix, datapath-functional, kernel-hygiene]`.

## 🍎 Apple-compatible transport

- `apple_data` is now a tri-state (`-1=auto`, `0=off`, `1=on`) that auto-enables
  under `profile=mac_compat`/`mixed`.
- Apple path explicitly accepts RoCE V1 or V2 GIDs (older macOS interop).
- New `docs/apple-hardware.md` documents supported Apple Silicon hardware, cable
  requirements, known limitations (UC-only, single rail, no IMM WRITE, IOMMU not
  yet validated), and the full interop procedure.

## 📚 Documentation

- New: `CONTRIBUTING.md` + `docs/CONTRIBUTING.md`, `docs/ARCHITECTURE.md`,
  `docs/TROUBLESHOOTING.md`, `docs/MODULE_PARAMETERS.md`, `proto/WIRE_PROTOCOL.md`,
  `docs/ROADMAP.md`, and `docs/FINDINGS.md` (cited, prioritized analysis).
- README rewritten: documentation index, `mac_compat` setup, tuning pointers,
  one-command regression usage, and narrowed status caveats.
- All roadmap (8 steps) and fix (14 items) automation toolkits are archived under
  `scripts/archive/{roadmap,fixes}/` now that the work has shipped.

---

## ⚠️ Compatibility / upgrade notes

- **Native peers now require `peer_auth_acl`.** Without a matching
  `uuid=32hexpsk` entry on both hosts, native Linux peers are rejected before a
  usable RDMA session is published. Both sides must share the same PSK.
- **`native_wr_striping` was removed** — update any module configs that set it.
- **`zcopy_min_bytes` default changed** `0 → 4096`.
- **`apple_data` is now an int** (`-1/0/1`) rather than a bool.
- The native control wire format changed (HELLO grew to 68 bytes); both peers
  must run this build to interoperate.

## 🙌 Upstream baseline

Includes everything in upstream `hellas-ai/thunderbolt-ibverbs` **v0.3.0** (see
the upstream [v0.3.0 release](https://github.com/hellas-ai/thunderbolt-ibverbs/releases/tag/v0.3.0)).
