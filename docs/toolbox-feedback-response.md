# tb-vllm-toolbox feedback — response

This document records how the five upstream recommendations raised by the
`tb-vllm-toolbox` work were analyzed and addressed in this repository.

**Source:** `pleite/amd-strix-halo-vllm-toolboxes`, PR #1
(*"tb-vllm-toolbox: thunderbolt-ibverbs (DKMS + userspace) in the vLLM
toolbox"*), file `docs/ibverbs-changes-required.md`, derived from the plan in
commit `9d292e6`. The toolbox inspected this repo at revision
`95c98aa4bc88a6ef3b992aa955e372573e09dce8`.

The recommendations were discovered while bundling `thunderbolt-ibverbs`
(`usb4_rdma` RDMA over Thunderbolt/USB4) into the Strix Halo vLLM toolbox so two
nodes can cluster over a direct USB4 cable.

---

## 1. DKMS version mismatch between `Makefile` and `dkms.conf` (bug) — fixed

**Finding.** `dkms.conf` declares `PACKAGE_VERSION="0.3.1"`, but the top-level
`Makefile` `dkms-*` targets hardcoded `thunderbolt-ibverbs/0.1.0`, so
`sudo make dkms-build` (as documented in the README) failed after `dkms add`:
DKMS registered `0.3.1` from `dkms.conf` while the targets asked for `0.1.0`.

**Change.** The `Makefile` now derives the package name and version from
`dkms.conf` (`DKMS_NAME`/`DKMS_VERSION` via `awk`) and uses
`$(DKMS_NAME)/$(DKMS_VERSION)` in all `dkms-build`/`dkms-install`/`dkms-remove`
targets. `dkms.conf` is the single source of truth, so the `Makefile`,
`dkms.conf`, and packaging stay in lockstep across version bumps.

Verified: `make -n dkms-build` now prints `dkms build thunderbolt-ibverbs/0.3.1`.

## 2. Container provider story (Fedora 43 PABI) — addressed

**Finding.** The toolbox image is Fedora 43. To enumerate `usb4_rdma*` inside
the container, its `libibverbs` needs the `usb4_rdma` provider `.so` with a
matching PABI. The provider is built against rdma-core `v62.0`; a PABI mismatch
lets the `.so` load but fail to enumerate.

**Change (both fallbacks the recommendation listed):**

- Added [`tools/ci/install-provider-into-container.sh`](../tools/ci/install-provider-into-container.sh),
  which copies the host's `usb4_rdma.driver` hint and `libusb4_rdma-rdmav*.so`
  into a running podman/docker/toolbox container (the supported form of the
  Phase 5.1 manual fallback).
- Documented the `RDMA_CORE_TAG` / PABI guidance for Fedora 43 in
  [vllm-toolbox-integration.md § 5.2](vllm-toolbox-integration.md#provider-pabi-fedora-43):
  pin `RDMA_CORE_TAG` to the tag matching the container's `libibverbs` and
  rebuild via `tools/ci/distro-package-rdma.sh fedora` when the copied `.so`
  does not enumerate.

## 3. A reusable smoke-test build target — added

**Finding.** The proto smoke binaries (`proto-smoke`, `reliability-smoke`,
`identity-smoke`, `config-smoke`) were built with ad-hoc `gcc` invocations
because `tools/ci/` had no Makefile; the extra `proto/*.c` translation units
each test needs had to be reverse-engineered from `#include`s.

**Change.** Added [`tools/ci/Makefile`](../tools/ci/Makefile) (mirroring
`proto/Makefile`) so `make -C tools/ci` (or `make -C tools/ci test`) builds and
runs every freestanding smoke binary. Source lists match `flake.nix`'s
`proto-smoke` check, so downstream consumers call one target instead of
hardcoding source lists. Referenced from `docs/CONTRIBUTING.md`.

## 4. Kernel-module deployment model — documented

**Finding.** The intended deployment model (host-DKMS vs container-DKMS) was
ambiguous between this repo's integration guide and the toolbox plan.

**Change.** Added a *"Deployment model — host-DKMS vs container-DKMS"* section
to [vllm-toolbox-integration.md](vllm-toolbox-integration.md#deployment-model--host-dkms-vs-container-dkms):
host-DKMS is the blessed model (the module always loads on the host, kernel
≥ 6.14); the container only needs the userspace provider; container-DKMS is an
opt-in convenience with its prerequisites spelled out.

## 5. `modprobe.d` drop-in for Strix Halo — shipped

**Finding.** Users had to assemble the `linux_perf` + `peer_auth_acl` module
line by hand.

**Change.** Added
[`packaging/modprobe.d/thunderbolt-ibverbs.conf.example`](../packaging/modprobe.d/thunderbolt-ibverbs.conf.example)
with the Strix-Halo `linux_perf` defaults; users fill in only the peer UUID and
shared PSK. Referenced from `README.md` and the integration guide (Phase 2.3).

---

## Validation

- `make -n dkms-build dkms-install dkms-remove` → all resolve to version `0.3.1`.
- `make -C tools/ci test` → all four smoke binaries build (`-Werror`) and exit 0.
- `shellcheck tools/ci/install-provider-into-container.sh` → clean.
- Compiled smoke binaries are git-ignored; no build artifacts committed.

Hardware-dependent steps (image build, kernel-module load, device enumeration,
TP=2 cluster) remain to be exercised on two USB4-connected Strix Halo nodes —
see the toolbox PR's `docs/tb-vllm-toolbox-recommendations.md` validation
checklist.
