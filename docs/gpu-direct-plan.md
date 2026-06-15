# GPU-direct RDMA via dma-buf — Design and Implementation Plan

> **Status: planning** — this document is the deliverable of the investigation
> phase. No kernel, provider, or data-path behavior changes are made here.
> Every claim cites the file and line in the current `main` HEAD. The follow-up
> implementation work is tracked in the checklist at the end.

---

## 1. Executive summary

`thunderbolt-ibverbs` today is a complete, functional RDMA transport over USB4
DMA rings for *host* memory. GPU memory is explicitly excluded: the userspace
provider advertises `reg_dmabuf_mr` but the kernel device ops table has no
matching handler, so `ibv_reg_dmabuf_mr()` fails at the kernel boundary. The
send and receive data paths copy bytes through the CPU using `ib_umem_copy_from`
/ `sg_pcopy_from_buffer` and an explicit `is_zone_device_page` guard prevents
GPU pages from reaching the zero-copy ring path.

The **AMD AI MAX+ 395** (Strix Halo APU, Radeon 8060S iGPU, unified LPDDR5X
memory) changes the tradeoff: because CPU and GPU share the same DRAM — there is
no discrete VRAM, no PCIe BAR hop — a *copy-through-CPU* approach on top of a
pinned `ib_umem_dmabuf` is a viable first cut that avoids every PCIe peer-to-peer
complication.

This document investigates exactly where the current implementation fails, what
the GPU-direct path would look like, and how to build it in four focused phases
targeting the Strix Halo unified-memory case. The revised design keeps host-copy
and GPU-direct as coexisting modes: host-copy remains the default/supported path
and GPU-direct (`dma-buf` MR) is an explicit opt-in with defined fallback.

### Non-goals (v1)

- Discrete GPU PCIe peer-to-peer (P2P) BAR DMA — no NHI DMA topology change.
- Zero-copy GPU→ring streaming (NHI DMA of GPU pages directly) — kept for v2+.
- NVIDIA GPUDirect / `nv_peer_mem` — not tested on this hardware.
- Dynamic / move-notified dmabuf (ODP-equivalent for GPU memory) — Phase 4 only.
- Upstream kernel or rdma-core driver-ID registration — tracked separately.
- Replacing the current host-copy path as the only mode; host-copy remains
  supported and is the out-of-box default until a user explicitly opts in.

---

## 2. End-to-end trace of the current (failing) dmabuf MR path

### 2.1 Userspace side

`usb4_rdma_reg_dmabuf_mr` (`userspace/usb4_rdma/usb4_rdma.c:223–249`) is wired
into `usb4_rdma_context_ops` (`usb4_rdma.c:269`):

```c
/* usb4_rdma.c:264 */
static const struct verbs_context_ops usb4_rdma_context_ops = {
    ...
    .reg_dmabuf_mr   = usb4_rdma_reg_dmabuf_mr,   /* line 269 */
    ...
};
```

The implementation calls `ibv_cmd_reg_dmabuf_mr()`, with a version guard for
rdma-core < v55 (`USB4_RDMA_OLD_REG_DMABUF_MR`, `CMakeLists.txt:7–8`):

```c
/* usb4_rdma.c:236–241 */
#ifdef USB4_RDMA_OLD_REG_DMABUF_MR
    rv = ibv_cmd_reg_dmabuf_mr(pd, offset, length, iova, fd, access, vmr);
#else
    rv = ibv_cmd_reg_dmabuf_mr(pd, offset, length, iova, fd, access, vmr,
                               NULL);
#endif
```

`ibv_cmd_reg_dmabuf_mr` issues an `IB_USER_VERBS_EX_CMD_REG_DMABUF_MR` ioctl
through the standard uverbs extended-command path.

### 2.2 Kernel uverbs dispatch

The ioctl reaches `ib_uverbs_ex_reg_dmabuf_mr()` in
`drivers/infiniband/core/uverbs_cmd.c` (upstream kernel, not in-tree here).
That function calls `ib_reg_user_mr_dmabuf()`, which checks for a
`.reg_user_mr_dmabuf` op on the device:

```c
/* upstream: ib_umem.c / uverbs_cmd.c (not reproduced here) */
if (!device->ops.reg_user_mr_dmabuf)
    return ERR_PTR(-EOPNOTSUPP);
```

### 2.3 Kernel device ops table — the missing handler

`tbv_ibdev_ops` (`kernel/ibdev.c:8052–8093`) sets only:

```c
/* ibdev.c:8066 */  .get_dma_mr = tbv_get_dma_mr,
/* ibdev.c:8086 */  .reg_user_mr = tbv_reg_user_mr,
```

There is **no** `.reg_user_mr_dmabuf`. The kernel therefore returns
`-EOPNOTSUPP`, which the uverbs layer converts to `errno = EOPNOTSUPP` in
userspace. `ibv_reg_dmabuf_mr()` returns `NULL` with `errno = EOPNOTSUPP`.

A code search for `ib_umem_dmabuf_get`, `dma_buf_attach`, `reg_dmabuf`,
`p2pdma`, and `peer_mem` returns no matches anywhere under `kernel/`.

### 2.4 Summary of the current failure path

```
ibv_reg_dmabuf_mr()                        [rdma-core libverbs]
  → usb4_rdma_reg_dmabuf_mr()             [usb4_rdma.c:223]
    → ibv_cmd_reg_dmabuf_mr()             [rdma-core internal]
      → IB_USER_VERBS_EX_CMD_REG_DMABUF_MR ioctl
        → ib_uverbs_ex_reg_dmabuf_mr()    [kernel uverbs core]
          → ib_reg_user_mr_dmabuf()
            → tbv_ibdev_ops.reg_user_mr_dmabuf == NULL
              → -EOPNOTSUPP               ← failure point
```

---

## 3. MR model: `struct tbv_mr` and what a dmabuf variant needs

### 3.1 Current `struct tbv_mr`

Defined at `kernel/ibdev_internal.h:317–333`:

```c
struct tbv_mr {
    struct ib_mr        base;
    struct tbv_state   *owner;
    struct ib_umem     *umem;      /* host umem; NULL for dma_mr */
    refcount_t          refs;
    struct work_struct  free_work;
    u64                 start;
    u64                 length;
    u64                 virt_addr;
    int                 access;
    u32                 peer_id;
    bool                closing;
    bool                dma_mr;
};
```

The `umem` field is a `struct ib_umem *` pinned by `ib_umem_get()` in
`tbv_reg_user_mr` (`kernel/ibdev_mr.c:140`). All data-path helpers that read or
write MR-backed memory access `mr->umem` directly.

### 3.2 Fields required for a dmabuf MR variant

| New field | Type | Purpose |
|---|---|---|
| `umem_dmabuf` | `struct ib_umem_dmabuf *` | Pinned dmabuf backing; mutually exclusive with `umem` |
| `dmabuf_mr` | `bool` | True when the backing is `umem_dmabuf` rather than `umem` |

For the pinned (Phase 1) variant `ib_umem_dmabuf_get_pinned()` is used, which
returns an `ib_umem_dmabuf` whose embedded `struct ib_umem` is SG-table-backed
and CPU-accessible (same shape as a regular `ib_umem` on unified-memory APUs).
No move-notify / invalidation callback is needed for Phase 1.

For the dynamic (Phase 4) variant a move-notify callback (`notifier`) and a
pinned-during-transfer mechanism (similar to ODP) would be required.

### 3.3 Key allocation and lifetime hooks

| Function | File | What changes |
|---|---|---|
| `tbv_reg_user_mr` | `kernel/ibdev_mr.c:117` | No change — host path stays |
| `tbv_reg_dmabuf_mr` (new) | `kernel/ibdev_mr.c` | New function; calls `ib_umem_dmabuf_get_pinned()`, sets `mr->umem_dmabuf`, `mr->dmabuf_mr` |
| `tbv_mr_publish` | `kernel/ibdev_mr.c:54` | No change |
| `tbv_dereg_mr` | `kernel/ibdev_mr.c:163` | Must call `ib_umem_dmabuf_release()` when `mr->dmabuf_mr` |

---

## 4. Data-path copy sites

### 4.1 Outbound (send / RDMA-read response)

#### `tbv_copy_send_range` — `kernel/ibdev.c:3312`

The main copy kernel for send WRs. Iterates over `tbv_send_segment[]` and calls:

```c
/* ibdev.c:3336 */
ret = ib_umem_copy_from((u8 *)dst + copied, seg->mr->umem,
                        seg->addr + seg_off - seg->mr->start,
                        chunk);
```

`ib_umem_copy_from` is a kernel helper that copies bytes from an `ib_umem` SG
table into a kernel buffer. For a `dmabuf`-backed umem on Strix Halo (unified
memory), the same helper works because `ib_umem_dmabuf_get_pinned()` builds an
`ib_umem` with an identical `sgt_append` structure. **Required change:** check
`mr->dmabuf_mr`; if set, use `seg->mr->umem_dmabuf->umem` (which embeds an
`ib_umem`) instead of `seg->mr->umem`.

#### Copied send path — `kernel/ibdev.c:3904`

The main copied-send loop calls `tbv_copy_send_range` at:

```c
/* ibdev.c:3904 */
ret = tbv_copy_send_range(ctx->segs, ctx->nsegs, offset,
                          frame + TBV_NATIVE_DATA_HDR_SIZE,
                          payload_len);
```

This is the frame fill for every copied-mode fragment. No structural change is
needed beyond the fix in `tbv_copy_send_range` itself.

#### RDMA read response — `kernel/ibdev.c:7643`

The RDMA-read responder reads from an MR via `tbv_umem_iova_to_addr` (line 7725)
to resolve the physical address then uses `memcpy`:

```c
/* ibdev.c:7643 */
memcpy(frame + TBV_NATIVE_DATA_HDR_SIZE,
       (u8 *)ctx->data + offset, payload_len);
```

`ctx->data` is a pointer obtained earlier via `kmap`/virtual mapping of the
`ib_umem` SG table. For a dmabuf MR, the same kmap approach works on Strix Halo
because the pages are in system RAM. **Required change:** the `ctx->data`
population path (which resolves `mr->umem` pages) must handle `mr->umem_dmabuf`
the same way.

#### Zero-copy guard — `kernel/ibdev.c:3619–3626`

```c
static bool tbv_page_zcopy_safe(struct page *page)
{
    /*
     * Thunderbolt NHI DMA can stream ordinary system RAM. GPU/HMM/device
     * pages need the copied path unless/until this driver grows a real
     * peer-direct contract with the GPU driver.
     */
    return !is_zone_device_page(page);
}
```

`tbv_send_segments_zcopy_safe` (`ibdev.c:3629`) uses this to decide whether the
ring-zcopy path is safe. GPU dmabuf pages returned by `amdgpu` for Strix Halo
unified memory are **not** zone-device pages — they live in the same DRAM as CPU
pages. However, the guard here is correct and conservative. Phase 2 of this plan
keeps the copied path for all dmabuf MRs (regardless of page type) and defers
zcopy enablement to Phase 4 once move-notify is wired.

#### `tbv_umem_page_from_addr` — `kernel/ibdev.c:5702`

Used by `tbv_send_segments_zcopy_safe` (`ibdev.c:3657`) to walk the SG table and
return a `struct page *` for zcopy:

```c
/* ibdev.c:5702 */
static int tbv_umem_page_from_addr(struct tbv_mr *mr, u64 addr, u32 max_len,
                                   struct page **page_out,
                                   u32 *page_off_out, u32 *len_out)
{
    struct sg_table *sgt = &mr->umem->sgt_append.sgt;
    ...
}
```

Accesses `mr->umem` directly. **Required change (Phase 2):** add a dmabuf
branch that uses `mr->umem_dmabuf->umem.sgt_append.sgt`. In practice this
function is only on the zcopy path, which is not used by dmabuf MRs in Phase 2.

### 4.2 Inbound (receive / RDMA write)

#### `tbv_umem_copy_to` — `kernel/ibdev.c:5616`

Delivers received payload into an MR:

```c
/* ibdev.c:5616 */
static int tbv_umem_copy_to(struct tbv_mr *mr, u64 addr, const void *src,
                            size_t len)
{
    struct sg_table *sgt = &mr->umem->sgt_append.sgt;
    ...
    copied = sg_pcopy_from_buffer(sgt->sgl, sgt->orig_nents, src, len, offset);
    ...
}
```

Called by:

- `tbv_rx_copy_to_wqe` (`ibdev.c:5890`) — deliver receive fragment into recv WQE
- `tbv_rx_handle_rdma_write_fragment` (`ibdev.c:7342` → `ibdev.c:7532`) — RDMA WRITE target
- `tbv_rx_handle_send_fragment` (`ibdev.c:6874` → `ibdev.c:6999`, `7115`) — SEND target

**Required change (Phase 2):** check `mr->dmabuf_mr`; if set, use
`mr->umem_dmabuf->umem.sgt_append.sgt` instead of `mr->umem->sgt_append.sgt`.
The `sg_pcopy_from_buffer` call itself is unchanged — the SG table shape is
identical between pinned dmabuf umem and regular umem.

#### `tbv_umem_copy_to_iova` — `kernel/ibdev.c:5642`

Thin wrapper over `tbv_umem_copy_to`; inherits the fix automatically.

#### `tbv_rx_copy_to_wqe` — `kernel/ibdev.c:5870`

```c
/* ibdev.c:5890 */
ret = tbv_umem_copy_to(mr, wqe->addr + offset, payload, copy_len);
```

No structural change needed beyond `tbv_umem_copy_to`.

### 4.3 CPU kmap on Strix Halo unified memory

On a discrete GPU there is VRAM that the CPU cannot reach without a PCIe BAR
aperture; `kmap`/`sg_pcopy_from_buffer` on such pages would fault or return
garbage. On the AMD AI MAX+ 395, the Radeon 8060S iGPU has **no discrete VRAM**:
all GPU allocations are carved from the same LPDDR5X DRAM as CPU allocations.
The `amdgpu` driver backs these allocations with system pages (not
`ZONE_DEVICE`). Consequently:

- `ib_umem_dmabuf_get_pinned()` can pin them as ordinary system pages.
- `sg_pcopy_from_buffer` / `ib_umem_copy_from` work without any special BAR
  mapping.
- `is_zone_device_page()` returns false, so the existing `tbv_page_zcopy_safe`
  guard does not block them (though Phase 2 deliberately avoids the zcopy path
  for all dmabuf MRs anyway).

This is the key architectural reason Phase 1+2 (pinned dmabuf + CPU-copy) is
viable without any DMA topology or ring-driver change.

---

## 5. Strix Halo / AMD AI MAX+ 395 unified-memory rationale

### 5.1 Architecture

The AMD AI MAX+ 395 (Strix Halo) integrates a Zen 5 CPU complex and a Radeon
8060S iGPU on the same die. Both share a single LPDDR5X memory controller with
no discrete VRAM. AMD's ROCm/`amdgpu` driver maps GPU allocations into
system-RAM pages visible to the CPU, enabling the following:

- ROCm ≥ 6.0 added `hipMemGetHandleForAddressRange` with
  `hipMemHandleTypePosixFileDescriptor`, which exports a dmabuf fd for any ROCm
  (HIP/HSA) allocation. This is the same fd that `ROCSHMEM_GDA_ENABLE_DMABUF=1`
  depends on (`userspace/bench/tbv_vllm_smoke.sh:453`).
- The `amdgpu` driver backs these unified-memory allocations with struct pages in
  `ZONE_NORMAL`; they are not `ZONE_DEVICE` pages. `ib_umem_dmabuf_get_pinned()`
  can therefore pin them with ordinary `get_page` / `pin_user_pages` semantics.
- `kmap_local_page` / `sg_pcopy_from_buffer` work on these pages at full memory
  bandwidth — there is no PCIe BAR bottleneck.

### 5.2 Why copy-through-CPU is acceptable for v1

USB4 40 Gbps ≈ 5 GB/s raw bandwidth. Thunderbolt NHI ring DMA bandwidth is
bounded by the Thunderbolt fabric, not by CPU-copy throughput (Zen 5 at 128-bit
AVX is >100 GB/s for memory-bandwidth-bound copies). The copy-through-CPU path
does not add a new bottleneck; it is already the documented architecture for host
MRs (`docs/ARCHITECTURE.md`). For GPU tenants doing all-reduce over USB4 the
GPU→CPU→ring→CPU→GPU path adds latency but not bandwidth compression.

### 5.3 ROCm / amdgpu requirements

| Requirement | Version | Notes |
|---|---|---|
| `hipMemGetHandleForAddressRange` with dmabuf fd | ROCm ≥ 6.0 | Linux kernel `dma-buf` subsystem must be enabled |
| `amdgpu` unified-memory allocation in ZONE_NORMAL | kernel ≥ 6.2 | Earlier kernels may use ZONE_DEVICE for some paths |
| `ib_umem_dmabuf_get_pinned` | kernel ≥ 6.0 | In-tree RDMA core helper |
| `ROCSHMEM_GDA_ENABLE_DMABUF=1` | rocSHMEM (GDA branch) | Pre-built or rebuilt from source |

---

## 6. GDA transport contract: userspace/CI versus kernel reality

`userspace/bench/tbv_vllm_smoke.sh` sets up the GDA environment at lines 453–455:

```sh
ROCSHMEM_GDA_PROVIDER=ib
ROCSHMEM_GDA_ENABLE_DMABUF=1
ROCSHMEM_GDA_USB4_A2A_CHUNK_BYTES=...
```

The `--transport gda` mode is exposed in `.github/workflows/regression-self-hosted.yml:15–22`
and the README. This is a userspace contract that must remain robust when
GPU-direct is unavailable: `--transport gda` should capability-probe
(`ibv_reg_dmabuf_mr`) and fall back to host-staging behavior rather than
hard-failing when dmabuf MR support is not active.

The key gap today: `ROCSHMEM_GDA_ENABLE_DMABUF=1` causes rocSHMEM to call
`ibv_reg_dmabuf_mr()` for GPU buffers. That call currently returns `EOPNOTSUPP`
(§2). Under this revised design, that error is the intended probe signal and
must drive host-copy fallback unless GPU-direct has been explicitly activated.

`docs/vllm-toolbox-integration.md:275` documents the current workaround:

```sh
# NCCL_IB_GID_INDEX=1 and NCCL_NET_GDR_LEVEL=0 are already set by the toolbox
```

`NCCL_NET_GDR_LEVEL=0` keeps host-staging mode in NCCL. This remains the safe
default. A GPU-direct configuration (`NCCL_NET_GDR_LEVEL ≥ 1`) is documented as
an opt-in once kernel dmabuf MR support is present and enabled.

---

## 7. Capability advertising gaps

### 7.1 Driver ID

`tbv_ibdev_ops.driver_id = RDMA_DRIVER_UNKNOWN` (`kernel/ibdev.c:8054`). NCCL
and UCX do not require a specific driver ID for dmabuf probe, but some
frameworks and diagnostic tools branch on it. A real `RDMA_DRIVER_USB4` enum
upstream is tracked as a separate item.

### 7.2 `query_device` capability flags

`tbv_query_device` (`kernel/ibdev.c:1852–1882`) sets:

```c
attr->device_cap_flags = IB_DEVICE_CHANGE_PHY_PORT;
attr->kernel_cap_flags = IBK_LOCAL_DMA_LKEY;
```

No dmabuf-specific capability flag is defined in the RDMA core ABI today.
NCCL/RCCL probe dmabuf support by calling `ibv_reg_dmabuf_mr()` directly and
checking whether it returns `EOPNOTSUPP`; there is no separate feature bit to
advertise. With GPU-direct compiled out or load-time disabled, advertising and
runtime behavior remain identical to today and `EOPNOTSUPP` is the intended
probe signal. No `query_device` change is needed for dmabuf probing per se.

### 7.3 `IBV_ACCESS_RELAXED_ORDERING`

`IBV_ACCESS_RELAXED_ORDERING` is an access flag callers may pass to
`ibv_reg_dmabuf_mr`. The driver should accept and ignore it for v1 (the copy
path is already sequentially consistent at the CPU). `tbv_reg_user_mr` already
accepts any access flags and stores them on `mr->access`; the new
`tbv_reg_dmabuf_mr` should do the same.

### 7.4 `uverbs_no_driver_id_binding`

`tbv_ibdev_ops.uverbs_no_driver_id_binding = 1` (`kernel/ibdev.c:8056`) allows
the provider to match on node GUID rather than driver ID. This is correct and
should not change.

---

## 8. Optionality and fallback design

This design revision makes GPU-direct optional at three independent control
points. The names below are **proposed (non-binding)** and can be finalized
during implementation.

### 8.1 Build-time control (compile dmabuf MR support in/out)

Proposed toggle:

- Kconfig: `CONFIG_TBV_GPU_DIRECT`
- Out-of-tree make variable: `tbv_gpu_direct=0|1` (feeding `ccflags-y`)
- Proposed default: `CONFIG_TBV_GPU_DIRECT=n` / `tbv_gpu_direct=0` so out-of-box
  behavior stays on the current host-copy path.

Model/reference patterns:

- Existing Kconfig feature gating in `kernel/Kconfig` (`THUNDERBOLT_IBVERBS`,
  `THUNDERBOLT_IBVERBS_DEBUG_SURFACES`) shows how optional surfaces are compiled
  in/out.
- Existing Makefile feature detection for
  `TBV_KERNEL_HAS_IB_DMAH` (`kernel/Makefile:15–28`) shows the established
  compile-time probe/define pattern used by this tree.

Required behavior when compiled out:

- `.reg_user_mr_dmabuf` is not registered in `tbv_ibdev_ops`.
- `ibv_reg_dmabuf_mr()` returns `-EOPNOTSUPP` via uverbs (same contract as
  today).
- All host-copy behavior is byte-for-byte identical to current releases.

### 8.2 Load-time control (module parameter gate)

Proposed module parameter:

- `gpu_direct=auto|on|off` (default `auto`)

This follows the existing module-parameter style and load-time policy gates used
throughout the driver (`profile=`, `register_verbs=`, `negotiate_native=`), as
documented in `docs/MODULE_PARAMETERS.md`, `docs/ARCHITECTURE.md` (module
parameter table), and README `modprobe` examples.

`auto` semantics:

- If build-time support is compiled in **and** required kernel/`amdgpu` dmabuf
  support is present, activate the dma-buf MR op.
- Otherwise, keep host-copy as the active behavior and emit one single-line
  info log with the reason (e.g., "gpu_direct=auto fallback: amdgpu dmabuf interface unavailable").

`on` semantics:

- Request dma-buf MR support.
- If prerequisites are missing, return `EOPNOTSUPP` for dmabuf registration
  calls and log one single-line reason.

`off` semantics:

- Force current behavior regardless of kernel/GPU capability.
- `ibv_reg_dmabuf_mr()` returns `EOPNOTSUPP`; host-copy remains active.

### 8.3 Per-MR/runtime behavior and consumer probe contract

Even with build/load controls, per-call runtime behavior remains explicit:

- `ibv_reg_dmabuf_mr()` is the capability probe used by GPU-aware consumers.
- `EOPNOTSUPP` remains the canonical "feature unavailable here" signal.
- RCCL/NCCL and rocSHMEM GDA paths can branch cleanly:
  - probe succeeds → use direct-GPU dmabuf MR path;
  - probe returns `EOPNOTSUPP` → keep/choose host-copy staging path.

This preserves compatibility with existing consumer probing logic and avoids
hard-fail behavior when GPU-direct is unavailable.

### 8.4 Fallback matrix (build × load-time param × runtime support)

Safe default is explicit: out-of-the-box behavior remains today's host-copy
path unless the user opts in.

| Build (`CONFIG_TBV_GPU_DIRECT`) | `gpu_direct` param | dmabuf runtime support present (`amdgpu`/kernel) | Resulting behavior | `ibv_reg_dmabuf_mr()` | Logging |
|---|---|---|---|---|---|
| off | auto | absent/present | Host-copy only (identical to today) | `EOPNOTSUPP` | Optional one-line init note: compiled out |
| off | on | absent/present | Host-copy only (cannot activate direct path) | `EOPNOTSUPP` | One-line reason: requested on but compiled out |
| off | off | absent/present | Host-copy only (identical to today) | `EOPNOTSUPP` | Optional one-line init note: disabled |
| on | off | absent/present | Host-copy only (forced off) | `EOPNOTSUPP` | One-line reason: disabled by module param |
| on | auto | absent | Host-copy fallback | `EOPNOTSUPP` | One-line reason: auto fallback (support missing) |
| on | auto | present | Direct-GPU dmabuf MR active; host-copy still available for non-dmabuf MRs | success | One-line info: gpu_direct auto-enabled |
| on | on | absent | Host-copy fallback (direct requested but unavailable) | `EOPNOTSUPP` | One-line reason: requested on, support missing |
| on | on | present | Direct-GPU dmabuf MR active; host-copy still available for non-dmabuf MRs | success | One-line info: gpu_direct enabled |

**Citations for control-point patterns:** `kernel/Kconfig:3–20`,
`kernel/Makefile:15–28`, `docs/MODULE_PARAMETERS.md:8–31`,
`docs/ARCHITECTURE.md:81–96`, `README.md:304–323`, `kernel/main.c:29–31`,
`kernel/main.c:91–93`, `kernel/main.c:121–123`.

---

## 9. Phased implementation plan

### Phase 1 — Kernel `reg_user_mr_dmabuf` + build/load-time gates

**Goal:** introduce the optionality controls with safe defaults and wire a
gated `reg_user_mr_dmabuf` op. Merging this phase must be a no-op for existing
users (host-copy remains default behavior).

**Files / functions touched:**

| File | Change |
|---|---|
| `kernel/ibdev_internal.h:317` | Add `struct ib_umem_dmabuf *umem_dmabuf; bool dmabuf_mr;` to `struct tbv_mr` |
| `kernel/ibdev_mr.c` | Add `tbv_reg_dmabuf_mr()` function; add `ib_umem_dmabuf.h` include |
| `kernel/ibdev_mr.c:163` | In `tbv_dereg_mr`: release `umem_dmabuf` when `mr->dmabuf_mr` |
| `kernel/ibdev.c:8086` | Add `.reg_user_mr_dmabuf = tbv_reg_dmabuf_mr` to `tbv_ibdev_ops` |
| `kernel/ibdev_split.h` | Export `tbv_reg_dmabuf_mr` prototype |
| `kernel/Kconfig` | Add proposed `CONFIG_TBV_GPU_DIRECT` build toggle (non-binding name) |
| `kernel/Makefile` | Add proposed `tbv_gpu_direct` make-variable plumbing for `ccflags-y` (non-binding name), modeled after `TBV_KERNEL_HAS_IB_DMAH` pattern |
| `kernel/main.c` + docs | Add proposed `gpu_direct=auto|on|off` module parameter gate (non-binding name), defaulting to host-copy behavior |

**Pseudocode sketch (inert — no behavior change in this PR):**

```c
/* kernel/ibdev_mr.c — Phase 1 stub (NOT in this PR) */
// TODO(gpu-direct-phase1): implement tbv_reg_dmabuf_mr
// struct ib_mr *tbv_reg_dmabuf_mr(struct ib_pd *pd, u64 offset, u64 length,
//         u64 iova, int fd, int access, struct ib_udata *udata)
// {
//     mr->umem_dmabuf = ib_umem_dmabuf_get_pinned(pd->device, offset,
//                                                  length, fd, access);
//     mr->dmabuf_mr = true;
//     mr->base.type = IB_MR_TYPE_USER;
//     ... tbv_mr_publish(mr, pd) ...
// }
```

**Acceptance criteria:**
- With `CONFIG_TBV_GPU_DIRECT` off **or** `gpu_direct=off`, behavior is
  byte-for-byte identical to today and `ibv_reg_dmabuf_mr()` returns
  `EOPNOTSUPP`.
- With build support on and `gpu_direct=auto`, dmabuf registration only
  activates when runtime support is present; otherwise it cleanly returns
  `EOPNOTSUPP` and logs one fallback reason line.
- `ibv_dereg_mr()` on a dmabuf MR succeeds without leak when enabled.

### Phase 2 — Data-path copy helpers made dmabuf-aware

**Goal:** keep both copy paths and select per-MR at runtime:
host-copy for regular MRs, dmabuf-aware host-copy for dmabuf MRs when
GPU-direct is enabled.

**Files / functions touched:**

| File | Line | Change |
|---|---|---|
| `kernel/ibdev.c` | 3336 | In `tbv_copy_send_range`: use `umem_dmabuf->umem` when `mr->dmabuf_mr` |
| `kernel/ibdev.c` | 5616 | In `tbv_umem_copy_to`: same — use dmabuf embedded umem |
| `kernel/ibdev.c` | 5702 | In `tbv_umem_page_from_addr`: add dmabuf branch for zcopy probe |
| `kernel/ibdev.c` | 5687 | `tbv_umem_copy_from_iova` (wraps `ib_umem_copy_from`): same fix |
| `kernel/ibdev.c` | 7643 | RDMA-read response memcpy: ensure `ctx->data` population handles dmabuf umem |

The core pattern for each helper is:

```c
/* pseudo-patch — Phase 2, NOT in this PR */
// struct ib_umem *umem = mr->dmabuf_mr
//     ? &mr->umem_dmabuf->umem
//     : mr->umem;
// ... use umem ...
```

`tbv_rx_copy_to_wqe` (`ibdev.c:5870`), `tbv_rx_handle_rdma_write_fragment`
(`ibdev.c:7342`), and `tbv_rx_handle_send_fragment` (`ibdev.c:6874`) all reach
`tbv_umem_copy_to` and inherit the fix automatically.

**Note on `tbv_page_zcopy_safe`:** no change needed. Dmabuf MRs are kept on the
copied path for Phase 2. The existing guard (`ibdev.c:3619–3626`) is not removed;
its comment about a "peer-direct contract" is precisely what Phase 4 addresses.

**Acceptance criteria:**
- With `gpu_direct=off` (or compiled out), dmabuf registration still returns
  `EOPNOTSUPP` and all host-copy tests match current behavior.
- `ib_send_bw --use_rocm` (or equivalent perftest with a ROCm dmabuf MR)
  completes without error on Strix Halo when gpu_direct is enabled.
- `ib_write_bw` with a dmabuf MR completes.
- Bandwidth is within 5% of the host-MR baseline (copy overhead is dominated by
  Thunderbolt fabric, not CPU).
- No `data_rx_copy_error` or `data_wr_copy_error` debugfs counter increments.

### Phase 3 — Userspace driver-id, capability advertising, RCCL/NCCL enablement

**Goal:** document and validate both supported configurations: host-staging and
GPU-direct opt-in.

**Files / functions touched:**

| File | Change |
|---|---|
| `userspace/usb4_rdma/usb4_rdma.c:293` | Change `RDMA_DRIVER_UNKNOWN` → `RDMA_DRIVER_USB4` (once upstream enum is allocated); in the interim add a `/* TODO: request upstream RDMA_DRIVER_USB4 */` comment |
| `docs/vllm-toolbox-integration.md:275` | Document both modes: host-staging (`NCCL_NET_GDR_LEVEL=0`) and GPU-direct opt-in (`NCCL_NET_GDR_LEVEL ≥ 1`) keyed off `gpu_direct` state |
| `userspace/bench/tbv_vllm_smoke.sh:453` | No code change; validate that `ROCSHMEM_GDA_ENABLE_DMABUF=1` now works end-to-end |

No kernel change is needed for Phase 3: NCCL/RCCL probe by calling
`ibv_reg_dmabuf_mr` and checking the return value; once Phase 1+2 are done the
probe succeeds automatically.

**Acceptance criteria:**
- Documentation clearly states:
  - `gpu_direct=off` + `NCCL_NET_GDR_LEVEL=0` = current host-staging path;
  - `gpu_direct=on|auto(enabled)` + `NCCL_NET_GDR_LEVEL ≥ 1` = direct-GPU path.
- `RCCL_ROCSHMEM_ENABLE=1 ROCSHMEM_GDA_ENABLE_DMABUF=1` allreduce completes on
  two Strix Halo machines when GPU-direct is enabled.
- `tbv_vllm_smoke.sh --transport gda` exits 0.
- `NCCL_NET_GDR_LEVEL` can be set to 1 without errors when feature-gated on.

### Phase 4 — Move-notify / dynamic dmabuf and zcopy re-baselining

**Goal:** support HMM / dynamic dmabuf (ODP-equivalent for GPU memory) and, if
Strix Halo pages remain zone-normal, conditionally enable ring zcopy for dmabuf
MRs only when `gpu_direct` is explicitly enabled.

**Files / functions touched:**

| File | Change |
|---|---|
| `kernel/ibdev_mr.c` | Add `ib_umem_dmabuf_get()` variant with move-notify callback; wire `tbv_mr.notifier` |
| `kernel/ibdev_internal.h` | Add notifier field to `struct tbv_mr` |
| `kernel/ibdev.c:3619` | Evaluate whether to lift the `tbv_page_zcopy_safe` zone-device guard for pinned dmabuf MRs, but only under explicit GPU-direct enablement |
| `bench/perftest-smoke-baseline.csv` | Re-baseline if dmabuf zcopy changes throughput |
| `docs/vllm-toolbox-integration.md` | Keep dual-mode docs; add Phase 4 guidance for move-notify/zcopy under explicit GPU-direct opt-in while preserving host-staging guidance |

**Acceptance criteria:**
- `ibv_reg_dmabuf_mr` with a moveable allocation does not crash on invalidation.
- Ring zcopy (if enabled under explicit parameter opt-in) does not corrupt data
  on migration.
- Smoke baselines updated and regression CI passes.

---

## 10. Risk and security analysis

### 10.1 dmabuf invalidation race (Phase 1–2)

With `ib_umem_dmabuf_get_pinned()`, pages are pinned for the lifetime of the MR.
If the GPU driver attempts to evict or migrate the allocation, the pin blocks it,
which is the correct behavior for Phase 1. Risk: long-lived MRs could OOM the
GPU allocator if pinning prevents reclaim. Mitigation: document a recommended
MR lifetime model (register-use-deregister per transfer) and expose this in the
vLLM integration guide. Operational mitigation is explicit: operators can set
`gpu_direct=off` to force the host-copy path if pin pressure appears in
production.

For Phase 4 (dynamic dmabuf), the move-notify / invalidation path must hold the
driver lock and quiesce in-flight send/receive operations on the MR before
allowing the page to move. Failure to do so could cause the ring to DMA from a
stale physical address. This is the same race that `ib_umem_odp` handles for
on-demand-paging MRs and should follow the same pattern.

### 10.2 Existing `tbv_page_zcopy_safe` guard

The guard at `kernel/ibdev.c:3619–3626` is an explicit defense against feeding
GPU pages to the NHI DMA engine without a peer-direct contract. This plan keeps
the guard for Phases 1–3. Removing it in Phase 4 requires verifying:

- The pages returned by `amdgpu` for Strix Halo unified memory are permanently
  `ZONE_NORMAL` (not `ZONE_DEVICE`) under all kernel versions in the support
  matrix.
- Move-notify is in place so the ring does not DMA a page that the GPU driver
  has concurrently remapped.
- The load-time off switch (`gpu_direct=off`) remains an immediate escape hatch
  to restore current host-copy behavior if field issues appear.

### 10.3 Peer-scoped rkey model

`tbv_mr_get` (`kernel/ibdev.c:530` area) validates `peer_id` on MR lookup to
prevent one peer from using another peer's rkey. A dmabuf MR must carry the same
`peer_id` as any other MR and is subject to the same scoping. The existing
`tbv_mr_publish` path (`kernel/ibdev_mr.c:54`) sets `mr->peer_id` from
`tbv_ibdev_peer_id(pd->device)` and `tbv_reg_dmabuf_mr` must use the same path.

### 10.4 Access flag forwarding

`ib_umem_dmabuf_get_pinned(device, offset, length, fd, access)` forwards the
access flags to the dmabuf exporter. The kernel's dmabuf fencing infrastructure
uses these to determine direction. Passing incorrect flags (e.g. read-only when
write access is needed) will cause silent data corruption or a kernel warning.
The implementation must forward `access` faithfully from the uverbs request.

---

## 11. Precise per-phase file list with current line references

| Phase | File | Lines (HEAD) | What changes |
|---|---|---|---|
| 1 | `kernel/ibdev_internal.h` | 317–333 | Add `umem_dmabuf`, `dmabuf_mr` to `struct tbv_mr` |
| 1 | `kernel/ibdev_mr.c` | after 161 | New `tbv_reg_dmabuf_mr()` function |
| 1 | `kernel/ibdev_mr.c` | 163–179 | `tbv_dereg_mr`: release dmabuf umem |
| 1 | `kernel/ibdev.c` | 8086 | Add `.reg_user_mr_dmabuf = tbv_reg_dmabuf_mr` |
| 1 | `kernel/ibdev_split.h` | (prototype section) | Export `tbv_reg_dmabuf_mr` |
| 1 | `kernel/Kconfig` | 3–20 (+new symbol) | Add `CONFIG_TBV_GPU_DIRECT` gate (proposed name) |
| 1 | `kernel/Makefile` | 15–28 (+new make variable) | Wire compile-time gate (proposed `tbv_gpu_direct`) following existing feature-detect style |
| 1 | `kernel/main.c` | module-parameter block | Add `gpu_direct=auto|on|off` gate (proposed name) |
| 1 | `docs/MODULE_PARAMETERS.md` / `docs/ARCHITECTURE.md` / `README.md` | parameter + modprobe sections | Document load-time switch and safe default |
| 2 | `kernel/ibdev.c` | 3336 | `tbv_copy_send_range`: dmabuf umem branch |
| 2 | `kernel/ibdev.c` | 5616 | `tbv_umem_copy_to`: dmabuf umem branch |
| 2 | `kernel/ibdev.c` | 5702 | `tbv_umem_page_from_addr`: dmabuf umem branch |
| 2 | `kernel/ibdev.c` | 5687 | `tbv_umem_copy_from_iova` / `ib_umem_copy_from` call |
| 2 | `kernel/ibdev.c` | ~7725 | RDMA read response `ctx->data` population |
| 3 | `userspace/usb4_rdma/usb4_rdma.c` | 293 | Driver ID TODO comment |
| 3 | `docs/vllm-toolbox-integration.md` | 275 | `NCCL_NET_GDR_LEVEL` note update |
| 4 | `kernel/ibdev_mr.c` | (new) | Dynamic dmabuf + move-notify |
| 4 | `kernel/ibdev_internal.h` | 317 | Notifier field |
| 4 | `kernel/ibdev.c` | 3619 | Evaluate zcopy guard lift |
| 4 | `bench/perftest-smoke-baseline.csv` | all rows | Re-baseline |

---

## 12. Acceptance criteria and test/validation plan

### 12.1 Phase 1 smoke

```sh
# Allocate a ROCm buffer, export dmabuf fd, register as RDMA MR
# Expected:
# - CONFIG_TBV_GPU_DIRECT=off OR gpu_direct=off -> EOPNOTSUPP
# - gpu_direct=auto/on with support present -> non-NULL MR + clean dereg
./userspace/bench/rc_write_gpu_poll --dmabuf --verify
```

`userspace/bench/rc_write_gpu_poll.cpp` already exists as a skeleton; it will
need a `--dmabuf` flag and a HIP `hipMemGetHandleForAddressRange` call added.

### 12.2 Phase 2 smoke (data-path)

```sh
# ib_write_bw / ib_send_bw equivalent using ROCm dmabuf MR
# Two Strix Halo machines connected over Thunderbolt 4 @ 40 Gbps
ib_write_bw --use_rocm --dmabuf -d usb4_rdma0 -x 1 -s 65536 -n 1000
ib_send_bw  --use_rocm --dmabuf -d usb4_rdma0 -x 1 -s 65536 -n 1000
```

Regression gate: bandwidth within 7.5% of host-MR baseline (matching the
existing `bench/perftest-smoke-baseline.csv` threshold documented in
`.github/workflows/regression-self-hosted.yml:28–90`).

Debugfs validation:

```sh
# After run: data_rx_copy_error and data_wr_copy_error must remain at 0
grep -r copy_error /sys/kernel/debug/thunderbolt_ibverbs/
```

### 12.3 Phase 3 RCCL/NCCL smoke

```sh
# Host-staging (default-safe mode)
NCCL_NET_GDR_LEVEL=0 ./userspace/bench/tbv_vllm_smoke.sh --transport gda ...

# GPU-direct opt-in mode
./userspace/bench/tbv_vllm_smoke.sh \
    --transport gda \
    --head-host node0 --worker-host node1 \
    --rccl-install /opt/rccl-gda \
    --rocshmem-install /opt/rocshmem-gda
```

Expected: host-staging mode remains valid, and GPU-direct mode exits 0 with
`assert_rdma_used` passing when `gpu_direct` is enabled.

### 12.4 Phase 4 move-notify regression

```sh
# Trigger GPU memory migration while an RDMA MR is live; confirm no crash/corruption
# Use ROCm page-migration stress test + concurrent ib_write_bw
./tools/ci/datapath-functional.sh   # existing CI smoke must still pass
```

Also verify `gpu_direct=off` immediately restores host-copy behavior after
Phase 4 lands.

---

## 13. Checklist — issue-ready task specs

The following items map each phase to a trackable GitHub issue. Each is written
so it can be filed with the body below and linked back to this document.

---

### GDP-1 · Phase 1: Kernel `reg_user_mr_dmabuf` op

- [ ] **Motivation:** `ibv_reg_dmabuf_mr()` currently returns `EOPNOTSUPP` because
  `tbv_ibdev_ops` has no `.reg_user_mr_dmabuf` handler (`kernel/ibdev.c:8086`).
  RCCL, rocSHMEM, and any GPU-aware RDMA consumer cannot register GPU buffers as
  RDMA MRs until this is fixed. This is the minimal kernel change to unblock all
  subsequent GPU-direct work on the Strix Halo unified-memory APU.
- **Scope:** Add `struct ib_umem_dmabuf *umem_dmabuf` and `bool dmabuf_mr` to
  `struct tbv_mr` (`ibdev_internal.h:317`); implement `tbv_reg_dmabuf_mr()` in
  `kernel/ibdev_mr.c` using `ib_umem_dmabuf_get_pinned()`; wire the dereg path
  in `tbv_dereg_mr`; add `.reg_user_mr_dmabuf = tbv_reg_dmabuf_mr` to
  `tbv_ibdev_ops` (`ibdev.c:8086`). In the same phase, add proposed
  (non-binding) build-time (`CONFIG_TBV_GPU_DIRECT`) and load-time
  (`gpu_direct=auto|on|off`) gates, defaulting to current behavior.
- **Acceptance:** With `CONFIG_TBV_GPU_DIRECT` off **or** `gpu_direct=off`,
  behavior is byte-for-byte identical to today and `ibv_reg_dmabuf_mr()`
  returns `EOPNOTSUPP`. With feature enabled and prerequisites present,
  `ibv_reg_dmabuf_mr()` returns a valid MR; `ibv_dereg_mr()` succeeds without
  leak. Module builds clean (checkpatch, sparse).
- **Labels:** `kernel`, `gpu-direct`, `enhancement`

---

### GDP-2 · Phase 2: Data-path copy helpers dmabuf-aware

- [ ] **Motivation:** After Phase 1, any `ibv_post_send` / `ibv_post_recv` using
  a dmabuf MR crashes or silently produces wrong data because `tbv_copy_send_range`
  (`ibdev.c:3336`), `tbv_umem_copy_to` (`ibdev.c:5616`), and related helpers
  dereference `mr->umem` directly — which is NULL for a dmabuf MR.
- **Scope:** In each of `tbv_copy_send_range`, `tbv_umem_copy_to`,
  `tbv_umem_page_from_addr`, and the RDMA-read response `ctx->data` population
  path, add a branch that uses `mr->umem_dmabuf->umem` (the embedded `ib_umem`
  inside the pinned dmabuf structure) when `mr->dmabuf_mr` is set. No change to
  the NHI ring path or the zcopy guard (`ibdev.c:3619`). Keep all dmabuf MRs on
  the copied path and preserve host-copy behavior for non-dmabuf MRs.
- **Acceptance:** `ib_write_bw` / `ib_send_bw` with a ROCm dmabuf MR completes
  on two Strix Halo machines when GPU-direct is enabled. With `gpu_direct=off`,
  dmabuf registration remains `EOPNOTSUPP` and host-copy behavior remains
  unchanged. Bandwidth within 7.5% of host-MR baseline.
  `data_rx_copy_error` / `data_wr_copy_error` remain zero.
- **Labels:** `kernel`, `gpu-direct`, `data-path`

---

### GDP-3 · Phase 3: RCCL/NCCL enablement and capability advertising

- [ ] **Motivation:** Even after Phases 1+2, NCCL's `NCCL_NET_GDR_LEVEL` is
  documented as 0 (`docs/vllm-toolbox-integration.md:275`), suppressing
  GPU-direct. The GDA transport smoke (`tbv_vllm_smoke.sh --transport gda`) will
  still fail at the RCCL rocSHMEM level without a config update.
- **Scope:** Update `docs/vllm-toolbox-integration.md` to reflect that
  host-staging (`NCCL_NET_GDR_LEVEL=0`) and GPU-direct opt-in
  (`NCCL_NET_GDR_LEVEL ≥ 1`) configurations, keyed off `gpu_direct` state; add
  a `/* TODO: request upstream RDMA_DRIVER_USB4 */` comment to `usb4_rdma.c:293`;
  run `tbv_vllm_smoke.sh --transport gda` to validate end-to-end. No kernel change.
- **Acceptance:** `tbv_vllm_smoke.sh --transport gda` exits 0. RCCL allreduce
  confirms RDMA path (`assert_rdma_used`) when GPU-direct is enabled; with
  feature off/unavailable, documented fallback path remains host-staging with
  `EOPNOTSUPP` probe handling and no hard-fail.
- **Labels:** `userspace`, `gpu-direct`, `documentation`

---

### GDP-4 · Phase 4: Move-notify / dynamic dmabuf and zcopy re-baselining

- [ ] **Motivation:** Phase 1 uses `ib_umem_dmabuf_get_pinned()`, which holds a
  hard pin. Long-lived MRs block GPU memory reclaim. Dynamic dmabuf
  (move-notify) allows the GPU to migrate allocations while the MR is live,
  and once move-notify is in place the zcopy guard (`ibdev.c:3619`) can be
  evaluated for lifting on Strix Halo unified pages.
- **Scope:** Add `ib_umem_dmabuf_get()` with an invalidation callback to
  `tbv_reg_dmabuf_mr`; wire the callback to quiesce in-flight WRs; add
  a notifier field to `struct tbv_mr`; evaluate the `tbv_page_zcopy_safe` guard;
  re-baseline `bench/perftest-smoke-baseline.csv` if zcopy is enabled under
  explicit `gpu_direct` opt-in; keep `gpu_direct=off` as escape hatch.
- **Acceptance:** GPU memory migration stress test + concurrent RDMA traffic
  produces no crash or data corruption. All existing `datapath-functional.sh` and
  regression-suite CI checks pass. Baselines updated if zcopy throughput changes.
  `gpu_direct=off` still restores current host-copy behavior.
- **Labels:** `kernel`, `gpu-direct`, `performance`, `reliability`
