# Changelog

All notable changes to this downstream fork are documented here. This fork
diverged from upstream `hellas-ai/thunderbolt-ibverbs` at **v0.3.0**.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/).

> Versioning note: distribution packages (DKMS + userspace provider) keep a
> plain numeric version (currently `0.3.1`) so Debian/RPM/Arch/Nix version
> strings stay valid; the Git tag and GitHub release for each downstream
> snapshot carry a `pl` suffix (e.g. `v0.3.1pl`).

## [v0.3.1pl] — 2026-06-16

Downstream snapshot on top of `v0.3.0pl`. Adds optional, gated GPU-direct
RDMA support and a documented upstream-sync path. Distribution packages are
versioned `0.3.1`; the Git tag and GitHub release are named `v0.3.1pl`.

### Added

- Gated GPU-direct RDMA via dma-buf memory regions, delivered in four phases:
  - Phase 1: `tbv_reg_dmabuf_mr()` dma-buf MR registration, gated by build-time
    `CONFIG_TBV_GPU_DIRECT` (out-of-tree `tbv_gpu_direct=1`) and load-time
    `gpu_direct=auto|on|off`.
  - Phase 2: dma-buf-aware data-path copy helpers.
  - Phase 3: RCCL/NCCL GPU-direct capability advertising and documentation.
  - Phase 4: dynamic (move-notify) dma-buf via `gpu_direct_dynamic=0|1`, with a
    zero-copy guard that excludes dma-buf MRs from ring zero-copy.
- `docs/gpu-direct-plan.md` design/implementation plan and
  `docs/UPSTREAM_SYNC.md` cataloguing upstream `v0.3.1`–`v0.3.4` and the
  integration path.
- `docs/vllm-toolbox-integration.md` vLLM toolbox integration guide.

### Changed

- Bumped packaging/Nix version strings from `0.3.0` to `0.3.1`.
- Refreshed `flake.lock` (nixpkgs bump).

## [v0.3.0pl] — 2026-06-14

Downstream snapshot on top of upstream v0.3.0. Delivers the full 8-step project
roadmap and a 14-item security/robustness analysis fix list (findings
`S*`/`R*`/`T*`), plus supporting CI, benchmarks, and documentation. See
[`RELEASE_NOTES.md`](RELEASE_NOTES.md) for the full write-up.

### Added

- Native peer authentication with per-peer PSK ACLs: `peer_auth_acl=<uuid=32hexpsk,...>`
  module parameter and a nonce + PSK HELLO/READY/ACK handshake (S2/S3).
- Optional peer allow-listing via `peer_allowlist=<uuid,...>` (fails closed).
- Runtime/build switches to disable diagnostic surfaces: `production_mode=1`,
  `tbv_debug_surfaces=0`, and `CONFIG_THUNDERBOLT_IBVERBS_DEBUG_SURFACES` (S4).
- Per-peer send-queue reservation (`peer_sendq_reserved_max`) and enforced
  device resource limits for `max_qp`/`max_cq`/`max_mr` (R5).
- `tbv_compat.{c,h}` compatibility shim layer; build-time `ib_dmah` detection
  (`TBV_KERNEL_HAS_IB_DMAH`) for split-header distro kernels.
- Project documentation: `SECURITY.md`, `CONTRIBUTING.md`, `docs/SECURITY.md`,
  `docs/CONTRIBUTING.md`, `docs/ARCHITECTURE.md`, `docs/TROUBLESHOOTING.md`,
  `docs/MODULE_PARAMETERS.md`, `docs/TUNING.md`, `docs/ROADMAP.md`,
  `docs/FINDINGS.md`, `docs/apple-hardware.md`, and `proto/WIRE_PROTOCOL.md`.
- Testing/CI: virtual two-node data-path functional test
  (`tools/ci/datapath-functional.sh`), kernel-hygiene job (checkpatch/sparse/
  smatch/KASAN/KCSAN + proto fuzzing), performance-regression gating with a
  committed baseline (`bench/perftest-smoke-baseline.csv`), reliability
  fault-injection tests, one-command regression suite (`nix run .#tbv-regression`),
  hot-unplug-with-traffic smoke, Apple↔Linux interop smoke, and a packaging-sync
  check.
- Parametric tuning sweep tool (`userspace/bench/tbv_tuning_sweep.py`) with
  committed results.

### Changed

- Memory keys (`rkey`/`lkey`) are now CSPRNG-derived and distinct per MR
  instead of a global incrementing counter (S1).
- Endpoint acceptance is bound to an authenticated native session rather than a
  plaintext QPN (S3).
- debugfs attributes are `0400` and configfs attributes `0400`/`0600`; sensitive
  fields removed from `summary` output (S4).
- Reliability `proto/` ACK/dedup history resized to `TBV_REL_ORDER_MAX` and
  indexed by `op_id` so it cannot wrap (R1).
- QP teardown is bounded and race-free; in-flight work is flushed with error
  completions (R2, R4).
- Single documented lock order enforced with `lockdep_assert_held()` (R3).
- Reliability retry now uses exponential backoff with bounded jitter (R6).
- `kernel/ibdev.c` split into reviewable units (`ibdev_cq.c`, `ibdev_mr.c`,
  `ibdev_qp.c`, `ibdev_native.c`, `ibdev_apple.c`) with no behavior change (R7).
- **Default change:** `zcopy_min_bytes` default `0 → 4096`.
- `apple_data` is now a tri-state int (`-1=auto`, `0=off`, `1=on`) and
  auto-enables under `profile=mac_compat`/`mixed`.
- Apple path now accepts RoCE V1 or V2 GIDs.
- Native control wire HELLO message grew `40 → 68` bytes to carry auth fields.
- Distro/smoke builds compile with `-Werror`; the release job now depends on
  `[build, nix, datapath-functional, kernel-hygiene]`.

### Removed

- `native_wr_striping` module parameter (use `native_fragment_striping`).

### Security

- Addresses analysis findings S1–S4, R1–R7, and T1–T4 from `docs/FINDINGS.md`.
- The native control wire format changed; both peers must run this build to
  interoperate.

## [v0.3.0] — upstream baseline

Everything from upstream `hellas-ai/thunderbolt-ibverbs`
[v0.3.0](https://github.com/hellas-ai/thunderbolt-ibverbs/releases/tag/v0.3.0).

[v0.3.1pl]: https://github.com/pleite/thunderbolt-ibverbs/releases/tag/v0.3.1pl
[v0.3.0pl]: https://github.com/pleite/thunderbolt-ibverbs/releases/tag/v0.3.0pl
[v0.3.0]: https://github.com/hellas-ai/thunderbolt-ibverbs/releases/tag/v0.3.0
