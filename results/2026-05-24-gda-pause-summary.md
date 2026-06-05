# USB4 GDA Pause Summary

Date: 2026-05-24

Worktree: `/mnt/Home/src/thunderbolt-ibverbs-gda`

## Original Goal

Build a credible GDA-style path over the USB4/Thunderbolt software-RDMA
transport on Strix Halo hosts, then use it as the substrate for rocm-xio,
rocSHMEM, and eventually RCCL/application-level collectives.

The practical target was not to pretend USB4 is a hardware RNIC. The target was
to expose the right GPU-visible queue and memory primitives above the existing
kernel-owned Thunderbolt rings:

- GPU/CPU-visible SQ/CQ/doorbell memory
- strict queue ordering and explicit error CQEs
- kernel admission/backpressure instead of transient-pressure-as-failure
- dmabuf MR support for GPU-location payloads
- rocSHMEM/RCCL enough to smoke real collectives

## What Landed

### DV / GDA Queue ABI

The driver now has a provider-private DV surface with:

- `QUERY_CAPS`, `CREATE_QUEUE`, `DESTROY_QUEUE`, and `KICK`
- 64-byte WQE/CQE entries
- split producer/consumer doorbell cachelines in a dedicated page
- per-queue generation tracking
- strict in-QP completion order
- observable CQEs for stale generation and hard errors
- CQ overflow -> QP error semantics

Host-only and HIP-side probes validate the descriptor path independently from
the full transport.

### Kernel Poll Worker

Phase 2 moved from explicit userspace `KICK` toward a per-device poll worker:

- one worker per USB4 RDMA device, not per QP
- fixed cadence with tunables
- NAPI-style budget
- immediate rescan when a QP still has work after budget exhaustion
- generation-aware teardown
- shared drain helper between KICK and poll worker

The earlier depth > budget collapse was fixed. Single-QP no-kick throughput now
stays in the same range across depth 64/128 instead of falling off a cliff.

### Backpressure

The important reliability correction was admitting that Thunderbolt transport
pressure is normal, not an error. The path layer now uses the intended shape:

- reserve/admit before send-side side effects
- leave a blocked WQE at SQ head
- retry on the next scan
- allow other QPs to progress
- count transient pressure separately from hard errors

Validated baselines included 32Q x b256 and 1M-WQE soak with:

- `dv_hard_error=0`
- bounded admission attempts
- non-zero backpressure retries under load
- sustained throughput around the previous high-throughput class

### dmabuf Payload MRs

The userspace provider and kernel driver now support `ibv_reg_dmabuf_mr()`.
This lets ROCm device allocations be registered as MRs and sent through the
native zcopy path when the pages pass the driver's safety checks.

Important implementation points:

- userspace provider wires `.reg_dmabuf_mr`
- kernel implements `.reg_user_mr_dmabuf`
- dmabuf MRs use the WQE IOVA consistently with the exported dmabuf offset
- imported sg tables with `nents > 0` and `orig_nents == 0` are handled
- fd lifetime bugs around deregistration were fixed

### rocm-xio / rocSHMEM / RCCL Surface

rocm-xio grew enough USB4 backend support to exercise GPU-produced WQEs and
dmabuf payloads.

rocSHMEM grew a USB4 GDA backend and enough operation coverage for meaningful
functional testing:

- PUT / WRITE
- PUT_SIGNAL / WRITE_WITH_IMM-style signaling
- READ / GET
- software atomics where rocSHMEM required atomics
- local-loopback fast paths
- generation-style pSync fixes for stale fixed-value waits
- collective fixes for broadcast, barrier, alltoall/alltoallv, and related
  sync patterns

RCCL source builds now work against the USB4-aware rocSHMEM. A minimal
two-node `rccl-tests` smoke works on `strix-1` and `strix-2`.

## Positive Results

### CPU Baseline

CPU-posted perftest on two Strix Halo hosts showed useful but CPU-heavy
throughput. q=2 reached about 21.6 Gb/s while saturating roughly one userspace
posting core. That justified the GDA direction as more than a latency exercise:
there was real CPU/posting headroom to recover.

### Phase 3 Throughput

GPU-produced rocm-xio traffic validated the core premise:

- single-QP GPU producer beat the comparable CPU-produced DV path
- 16-QP host payload runs reached roughly the 40+ Gb/s class
- dmabuf payload runs were close to host payload performance
- backpressure counters proved the path was saturating and retrying rather than
  silently counting failures as success

### rocSHMEM Functional Surface

The rocSHMEM substrate is now much broader than "one put works":

- barrier latency dropped from the earlier tens-of-ms class to hundreds of us
  after the generation-based fixes
- broadcast/team broadcast, sync/barrier, alltoall/alltoallv, put_signal, and
  related tests have working paths in the USB4 backend
- high-iteration collective scripts exist in the rocm-systems tree to keep this
  from becoming purely ad-hoc

### Device-Source DMA Experiments

The driver is already doing real Thunderbolt NHI DMA for large zcopy payloads:
the path maps MR pages with `dma_map_page(..., DMA_TO_DEVICE)` and increments
`data_wr_zcopy`.

The hard question was GPU-store visibility before an external DMA engine reads
the memory. The allocator matrix found:

- ordinary `hipMalloc` dmabuf works only after HIP runtime synchronization
- HIP VMM pinned/uncached allocations did not solve no-sync visibility
- HSA GPU-location extended-scope memory proves no-sync external DMA can work,
  but was slow/fragile under sustained zcopy
- `hipDeviceMallocUncached` is the best current candidate: short no-sync runs
  passed at 32 B, 4 KiB, and 64 KiB, and 4 KiB x100 passed
- 64 KiB x100 uncached still hit a transport timeout, so it is not default-safe

This led to a clean opt-in path instead of changing defaults:

- reliable default: host-coherent scratch
- experimental path: uncached rocSHMEM heap and `RCCL_ROCSHMEM_SOURCE_HEAP=1`

### Application-Level Smoke

Built:

- `rocshmem-usb4-uncached-gfx1151`
- `rccl-tests-usb4-uncached-gfx1151`

Two-node RCCL smoke on `strix-1,strix-2`:

- `all_reduce_perf` passed with `RCCL_ROCSHMEM_SOURCE_HEAP=1`
- `alltoall_perf -b 65536 -e 65536 -n 1` passed
- RCCL logs showed `source heap=...`, confirming the source scratch allocator
  switch was active
- alltoall logs showed `Enabling GDA alltoall for RCCL`

Caveat: driver counters still showed `dv_poll_wqes=0` after those RCCL runs.
So this is an application smoke for the allocator/GDRDMA integration, not proof
that RCCL is using the DV poll-worker queue path.

## Blockers / Workarounds

### GPU-Store Visibility for External DMA

Status: partially characterized, not generally solved.

Workaround:

- host-coherent scratch for reliable application tests
- `hipDeviceMallocUncached` only as an experimental device-source path
- `hipDeviceSynchronize()` remains required for arbitrary `hipMalloc` payloads
  if they are used directly as DMA sources

Next question:

- why does 64 KiB x100 uncached zcopy hit a timeout when shorter runs pass?

### RCCL Does Not Yet Prove DV Queue Usage

Status: application smoke passes, but observed path still uses NET/IB/GDRDMA
for the tested collectives.

Workaround:

- keep using rocSHMEM and rocm-xio tests as the ground truth for DV queue
  mechanics
- treat RCCL tests as integration smoke until counters show DV WQEs

Next question:

- either route RCCL's rocSHMEM path through the USB4 DV queue explicitly, or
  add instrumentation showing exactly which rocSHMEM/RCCL calls hit the USB4 DV
  backend.

### Software Atomics Are Correct but Expensive

Status: implemented well enough for rocSHMEM correctness, not hardware-RNIC
fast.

Workaround:

- provider-aware collective fixes reduce unnecessary stale waits and fixed-value
  polling
- local-loopback fast path avoids remote software atomic overhead where possible

Next question:

- decide whether optimizing atomics matters before RCCL workload results are
  meaningful.

### Discrete GPU Generalization

Status: architecture allows it, not validated.

Strix Halo UMA does not mean "automatically coherent." It only means CPU and GPU
ultimately address the same DRAM. Addressable, coherent, and ordered are
separate properties. The current experiments are Strix-specific.

Next question:

- run the same dmabuf/visibility probes on a discrete GPU host such as `trex`
  before claiming anything about that class.

## Next Direction

1. Keep host-coherent application smoke as the reliability baseline.
2. Reproduce and isolate the 64 KiB x100 uncached zcopy timeout.
   - Is it transport timeout policy?
   - backpressure/admission?
   - NHI DMA visibility under sustained pressure?
   - page/sg layout specific to uncached allocations?
3. Add stronger instrumentation around RCCL -> rocSHMEM -> USB4 backend routing.
   The next useful RCCL result is one where driver counters prove the DV queue
   path was actually exercised.
4. Run a fixed regression suite after each driver/provider change:
   - rocm-xio 16Q x b256 host payload
   - rocm-xio 16Q/32Q x b256 dmabuf payload
   - 1M-WQE soak
   - rocSHMEM collective script
   - RCCL smoke
5. Only after the uncached/device-source timeout is understood should we consider
   making device-source scratch the normal path.

## Current Recommendation

Do not delete the host-coherent path. It is still the reliable baseline.

Do keep the uncached device-source path. It is the best available experiment for
"real device-side DMA" without requiring HIP synchronization, and it has already
produced enough positive signal to justify continued investigation.

The next slice should be a narrow reliability/debugging slice, not another layer
of integration: make the uncached 64 KiB sustained test either pass cleanly or
fail with enough telemetry that we know which subsystem owns the bug.

## 2026-05-25 Update: Backpressure Soak Recovery

After rebasing on the merged port-4HCA work, the rocm-xio high-pressure soak
initially failed in a way that looked like a shared path scheduler wedge:
`data_tx_accepted` increased, `data_tx_posted` did not, QPs eventually timed
out, and later WQEs completed with error CQEs. The live path counters were not
fine-grained enough to prove which internal path state was stuck.

Fixes landed in the GDA worktree:

- debugfs `peers` now reports per-path queue/scheduler state:
  `ctrl_q`, `data_q`, `reserved`, `inflight`, `free`, `zcopy_inflight`,
  `scheduling`, and `raw_active`.
- raw-stream TX now tracks the active stream owner.
- timeout/destroy cleanup no longer half-cancels queued or in-flight packets
  belonging to an already-started raw stream. Once the raw-stream header is on
  the wire, the payload tail is allowed to drain through the stream end marker
  so later streams can be framed safely.
- path flush/reset clears raw-stream active state explicitly.

Validation on `strix-2 -> strix-1`, module built locally on `trex` against the
Strix `7.1.0-rc1` kernel-dev:

- 1-QP host payload sanity: passed, queues empty.
- 8Q x batch256 host payload pressure: passed, non-zero backpressure retries,
  zero hard errors/timeouts.
- 32Q x batch256 x 2048 host payload: passed. This was the short shape that had
  previously failed.
- 32Q x batch256 x 32768 host payload: passed with +1,048,576 DV WQEs,
  +0 hard errors, +0 WR timeouts, and empty queues.
- forced abort of a 32Q high-pressure client: left expected process-abort DV
  errors, but the shared path recovered (`raw_active=0`, empty queues), and a
  subsequent 1-QP xio run passed.
- 1-QP dmabuf/device payload sanity: passed.
- 32Q x batch256 x 2048 dmabuf payload: passed with +65,536 DV WQEs,
  +1,887,632 backpressure retries, +0 hard errors/timeouts, and empty queues.
- 32Q x batch256 x 32768 dmabuf payload: passed with +1,048,576 DV WQEs,
  +30,038,009 backpressure retries, +0 hard errors/timeouts, and empty queues.
- RCCL/rocSHMEM smoke rerun after the driver fix: passed.
  - broadcast/all_gather/all_reduce still completed without DV WQEs, matching
    the known RCCL routing limitation.
  - alltoall completed with +140 DV WQEs summed across hosts, +0 hard errors,
    +0 WR timeouts.
  - alltoallv completed with +210 DV WQEs summed across hosts, +0 hard errors,
    +0 WR timeouts.

Updated interpretation:

- the earlier 32Q failures were not evidence that E2E/backpressure itself was
  inadequate; the two-phase admission path was doing its job.
- the missing piece was teardown/timeout interaction with the raw-stream
  framing state on the shared Thunderbolt path.
- host and dmabuf payload paths now both pass the sustained xio substrate soak
  under heavy backpressure.

Remaining caveat:

- the forced-abort path still produces DV error CQEs for the killed process,
  which is acceptable for that workload shape. The important property is that
  abort no longer poisons the shared rail for the next user.
- the RCCL smoke environment depends on store closures being present on both
  Strix hosts. During this rerun, `strix-2` was missing the OpenMPI/PRRTE and
  ROCm SDK store closures; copying those closures restored the test. This was a
  machine setup issue, not a GDA transport failure.
