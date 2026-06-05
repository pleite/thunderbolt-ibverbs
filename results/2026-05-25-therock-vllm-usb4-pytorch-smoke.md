# 2026-05-25 TheRock vLLM USB4 PyTorch Smoke

## Goal

Move the application-level smoke off the Lemonade environment and onto the
TheRock ROCm/vLLM stack, with the USB4 rocSHMEM/RCCL build selected by Nix
instead of ad-hoc `LD_PRELOAD` commands.

## Packaging

Added a wrapped environment in `nix-strix-halo-gda`:

- `vllm-env-therock-usb4-hostheap-gfx1151`
- app alias: `vllm-therock-usb4-hostheap-gfx1151`

The wrapper composes:

- TheRock vLLM/PyTorch ROCm 7.13 wheel environment
- USB4 host-heap RCCL
- USB4 host-heap rocSHMEM
- `rdma-core-usb4`, OpenMPI, and numactl runtime libraries
- RCCL/rocSHMEM defaults for the USB4 GDA route

It also provides a tiny ROCr compat directory:

- TheRock wheel core ships `libhsa-runtime64.so.1`
- RCCL's ROCr probe calls `dlopen("libhsa-runtime64.so")`
- The wrapper supplies an unversioned symlink to the wheel's HSA runtime

This avoids pointing RCCL at the SDK HSA runtime, which loaded but produced
`pfn_hsa_system_get_info failed with 4107`.

Tested wrapper output:

```text
/nix/store/vkmin7s83yjwmcj89pjs5j5yv41wyxw1-vllm-env-therock-usb4-hostheap-gfx1151
```

Local checks:

```text
torch 2.9.1+rocm7.13.0a20260513
hip 7.13.26183-83e9908b71
cuda_available True
vllm 0.16.0
vllm --version: 0.16.0+rocm713
```

## Smoke Test

Script:

```text
/mnt/Home/src/rocm-systems/projects/rocshmem/scripts/functional_tests/usb4_pytorch_smoke.sh
```

Hosts:

```text
strix-1,strix-2
```

The script launches `torch.distributed.run` on both hosts, uses PyTorch's
`nccl` backend, verifies that PyTorch loaded the USB4 RCCL, then checks USB4
driver counter deltas.

Representative passing result:

```text
Librccl path : /nix/store/nb9r3jaxdspv0ikxqlxgwq8y1rlas37z-rccl-usb4-hostheap-gfx1151-2.28.9-local/lib/librccl.so.1

USB4 counter deltas (pytorch_before -> pytorch_after):
  dv_poll_wqes                 strix-1          +12
  dv_poll_wqes                 strix-2          +12
  dv_poll_wqes                 sum              +24
  dv_hard_error                strix-1          +0
  dv_hard_error                strix-2          +0
  dv_hard_error                sum              +0
  data_wr_op_write             strix-1          +76
  data_wr_op_write             strix-2          +76
  data_wr_op_write             sum              +152
  data_wr_op_write_imm         strix-1          +256
  data_wr_op_write_imm         strix-2          +256
  data_wr_op_write_imm         sum              +512

USB4 PyTorch smoke passed
```

The ROCr runtime-path warning is gone with the compat symlink. RCCL still logs
that ordinary NET/IB GPU Direct RDMA is unavailable for device 4 when the smoke
also runs all-reduce/all-gather. That is expected for the normal RCCL IB path
and is separate from the rocSHMEM USB4 all-to-all route.

## All-To-All-Only Pass

The same harness can be restricted to all-to-all. One important correction from
the first pass: RCCL only routes all-to-all through rocSHMEM/GDA when
`count * dtype_size * nranks <= RCCL_ROCSHMEM_THRESHOLD`. With the wrapper
default of 1 MiB and 2 ranks, per-rank sizes above 512 KiB use the ordinary
RCCL path. The earlier 4 MiB number was therefore not a USB4 GDA result.

The harness was also fixed so correctness validation runs outside the timed
loop. The earlier timings included per-iteration validation synchronizes.

Routed-size USB4 GDA result, validation outside the timed loop:

```bash
USB4_TORCH_COLLECTIVES=all_to_all \
USB4_TORCH_SIZES=262144,524288 \
USB4_TORCH_ITERS=50 \
NCCL_DEBUG=ERROR \
USB4_SKIP_RCCL_LOG_CHECK=1 \
projects/rocshmem/scripts/functional_tests/usb4_pytorch_smoke.sh
```

Representative result:

```text
all_to_all_single bytes=262144: 2048.2 us/iter (1.02 Gb/s logical/rank)
all_to_all_single bytes=524288: 3759.2 us/iter (1.12 Gb/s logical/rank)

USB4 counter deltas:
  dv_poll_wqes sum  +408
  dv_hard_error sum +0
```

Ordinary RCCL fallback on the same sizes is currently faster:

```bash
RCCL_ROCSHMEM_ENABLE=0 \
USB4_EXPECT_DV_MIN=0 \
USB4_TORCH_COLLECTIVES=all_to_all \
USB4_TORCH_SIZES=262144,524288 \
USB4_TORCH_ITERS=50 \
projects/rocshmem/scripts/functional_tests/usb4_pytorch_smoke.sh
```

```text
all_to_all_single bytes=262144: 493.5 us/iter (4.25 Gb/s logical/rank)
all_to_all_single bytes=524288: 574.2 us/iter (7.30 Gb/s logical/rank)

USB4 counter deltas:
  dv_poll_wqes sum +0
```

The application-level all-to-all path is functionally correct but still slower
than the ordinary fallback at routed sizes, and much slower than the lower-level
xio/rocSHMEM throughput probes. Component benchmark modes show staging copies
are not the dominant cost: copy-in/copy-out are roughly 200-250 us at 256 KiB,
while the rocSHMEM exchange/sync path is milliseconds. The current optimization
target is therefore the USB4 rocSHMEM all-to-all sequence, which was doing:

1. payload WRITE
2. wait for payload completion
3. signal WRITE
4. wait for signal completion

An attempted optimization batched the payload WRITE and signal WRITE under one
doorbell, with the signal WQE fenced in the kernel so it would be admitted only
after earlier DV WQEs on that QP completed. The kernel-side fence mechanism was
implemented as a software admission fence: a fenced WQE returns `-EAGAIN` and is
retried if prior DV completions are still outstanding.

That all-to-all route was not viable. A 50-iteration routed all-to-all run
eventually aborted with an HSA GPU exception. The kernel did not report hard
errors, but it showed a retry storm:

```text
dv_poll_wqes sum       +202
dv_fence_retry sum    +2382
dv_hard_error sum       +0
data_wr_op_write sum  +214
data_wr_op_write_imm   +48
```

A one-iteration run completed, but was far slower than the baseline:

```text
all_to_all_single bytes=262144: 8361.1 us/iter (0.25 Gb/s logical/rank)
dv_poll_wqes sum    +8
dv_fence_retry sum +630
dv_hard_error sum   +0
```

The sender-side software fence serializes the critical path too aggressively and
can starve long runs. The rocSHMEM all-to-all call site was reverted to the
baseline payload-then-signal sequence. The kernel FENCE ABI and `dv_fence_retry`
counter remain useful for explicit future ordering experiments, but they are not
used in the current all-to-all path.

## Preallocated Tensor Smoke

The PyTorch smoke harness now defaults to preallocated tensors. This removes
per-iteration tensor allocation from the timed region, which is closer to how
RCCL collectives are used by real applications.

The rocSHMEM all-to-all ACK path is disabled by default. It passes a single
iteration but traps on repeated use, with `dv_fence_retry=0` and
`dv_hard_error=0`, so this is a device-side all-to-all ACK/progress bug rather
than a kernel DV failure.

ACK-off initially used one pSync slot per peer. That was not correct: raw
rocSHMEM `teamalltoall` exposed a generation race where one PE waited for
generation 1 while the peer had already overwritten the same slot with
generation 2. The stable ACK-off path now double-buffers pSync generations,
using the old ACK half of the pSync array as the second generation bank.

Raw rocSHMEM validation after that fix:

```text
rocshmem_functional_tests -a teamalltoall -s 4096 -w 1 -z 64 -n 100 -nskip 0
result: pass
representative latency range: 156-488 us over 8 B .. 4096 B message sizes
```

With the same double-buffered path, the routed PyTorch GDA smoke passes:

```text
all_to_all_single bytes=262144: 7884.3 us/iter (0.27 Gb/s logical/rank)
all_to_all_single bytes=524288: 15492.2 us/iter (0.27 Gb/s logical/rank)

USB4 counter deltas:
  dv_poll_wqes sum      +408
  dv_fence_retry sum      +0
  dv_hard_error sum       +0
```

The ordinary RCCL fallback is still much faster on the same preallocated shape:

```text
all_to_all_single bytes=262144: 156.8 us/iter (13.37 Gb/s logical/rank)
all_to_all_single bytes=524288: 242.4 us/iter (17.30 Gb/s logical/rank)

USB4 counter deltas:
  dv_poll_wqes sum +0
```

The current GDA all-to-all route is therefore an application-level functional
smoke, not a performance win. It is useful because it proves the full stack can
route PyTorch -> RCCL -> rocSHMEM -> USB4 GDA, but it is not yet a credible
vLLM tensor-parallel transport.

Updated component timings at 256 KiB with preallocated tensors:

```text
copy-in only:        30.0 us/iter
copy-out only:       92.5 us/iter
copy-in + copy-out: 100.1 us/iter
full GDA path:     7884 us/iter after the pSync double-buffer correctness fix
```

The copy cost is not the dominant term. The remaining gap is in the rocSHMEM
all-to-all exchange/progress path. The RCCL-side component benchmark modes are
diagnostic only, but they now run cleanly enough to show the shape: at 256 KiB,
copy-in/copy-out are tens of microseconds while exchange-only remains in the
millisecond range. A rocSHMEM-local diagnostic mode also exists for narrower
experiments: `ROCSHMEM_GDA_USB4_ALLTOALL_MODE=1` runs the payload-only slice and
`=2` runs the signal+wait-only slice. These are diagnostic modes, not valid
application-level collectives.

## Follow-Up: Local pSync Backoff

The split-heap design made local pSync words host-coherent and directly
loadable from the GPU. The original USB4 pSync wait loops still used the same
heavy backoff as remote software atomics, which added an unnecessary
software-RNIC-shaped latency floor to local polls.

Added `usb4_sync_poll_backoff(qp, addr)`: if the sync word is in a region that
the USB4 queue pair reports as direct-loadable, it uses a short local-memory
poll delay; otherwise it falls back to the heavy atomic backoff. This is only
used on local `usb4_sync_load_local()` wait loops, not on remote atomic-fetch
poll loops.

Build used for the measurement:

```text
/nix/store/n1ryw6jdv5vpf0z59jhb54fznirn5iri-vllm-env-therock-usb4-hostheap-gfx1151
/nix/store/ksps5bz7z2kvx9ahh06rh8bxynb5gn8h-rocshmem-usb4-hostheap-gfx1151-3.4.0-local
/nix/store/v9p8ji7az4hxlqjijs7cjsfkh38jdqi1-rccl-usb4-hostheap-gfx1151-2.28.9-local
```

Raw rocSHMEM `teamalltoall -s 524288 -w 1 -z 64 -n 50` improved:

```text
Msg Size 262144: 320.38 us
Msg Size 524288: 565.73 us

USB4 counter deltas:
  dv_poll_wqes sum          +3400
  dv_backpressure_retry sum    +0
  dv_fence_retry sum           +0
  dv_hard_error sum            +0
```

The same PyTorch/RCCL all-to-all smoke also improved, though it remains much
slower than raw rocSHMEM:

```text
all_to_all_single bytes=262144: 2405.0 us/iter (0.87 Gb/s logical/rank)
all_to_all_single bytes=524288: 3709.6 us/iter (1.13 Gb/s logical/rank)

USB4 counter deltas:
  dv_poll_wqes sum      +408
  dv_fence_retry sum      +0
  dv_hard_error sum       +0
```

Raw rocSHMEM is not sensitive to larger workgroup sizes in the bad direction;
with `-z 512 -n 20`, it was faster:

```text
Msg Size 262144: 277.47 us
Msg Size 524288: 488.23 us
```

`rocshmem_alltoallmem_on_stream`, which queues one alltoall kernel per
iteration through rocSHMEM's host API, is also in the raw-rocSHMEM class:

```text
Alltoallmem_On_Stream, -z 512 -n 20:
Msg Size 262144: 273.55 us
Msg Size 524288: 475.73 us
```

This narrows the remaining application gap: it is not payload copy cost, not
raw USB4 GDA transport cost, and not simply "one kernel launch per collective."
The next suspect is how RCCL invokes rocSHMEM from its device kernel and module
context.

## Negative Experiment: RCCL WG Context

rocSHMEM's own `rocshmem_alltoallmem_kernel` creates a workgroup team context
inside the device kernel before calling `rocshmem_ctx_alltoall_wg`. A matching
RCCL experiment changed `alltoall_gda.h` to call
`rocshmem_wg_team_create_ctx()` inside the RCCL device collective and use that
context for the exchange.

That experiment is not viable as-is. The PyTorch smoke timed out after 180 s
before meaningful DV traffic:

```text
dv_poll_wqes sum        +0
dv_hard_error sum       +0
data_wr_op_write sum    +4
data_wr_op_write_imm   +16
```

The likely explanation is that RCCL can use a host-created rocSHMEM context
passed through `ncclDevWorkColl`, but the RCCL module/kernel is not initialized
for rocSHMEM device-side context creation/destruction. The experiment was
reverted. A future clean fix would need to initialize the RCCL HIP module with
rocSHMEM's module initialization path, or add a host-created per-collective or
per-channel rocSHMEM context pool that RCCL can pass into device work without
creating contexts inside the RCCL kernel.

## Follow-Up: Context Array and GPU Timing

A less invasive context experiment added a rocSHMEM host helper to read a
preconstructed device context from `rocshmem_ctx_array`, and an RCCL knob:

```text
RCCL_ROCSHMEM_DEVICE_CTX_ARRAY_INDEX=<index>
```

With `RCCL_ROCSHMEM_DEVICE_CTX_ARRAY_INDEX=0`, RCCL confirmed it used the array
context:

```text
NCCL INFO Using rocSHMEM device ctx array index 0 ctx=... team=...
```

This path is functional, but it is not a performance fix. With 30 timed
iterations:

```text
default context:
  262144 B: 2273.5 us
  524288 B: 4209.2 us

ctx_array[0]:
  262144 B: 2376.4 us
  524288 B: 3747.9 us
```

The difference is noise-level and size-dependent. Default-context contention is
therefore not the main app-level gap.

The PyTorch smoke now also reports HIP event timing around the timed loop. GDA
wall time and GPU time match almost exactly:

```text
GDA all_to_all:
  262144 B: wall 2235.3 us, gpu 2234.6 us
  524288 B: wall 3646.3 us, gpu 3645.9 us

ordinary RCCL fallback:
  262144 B: wall 427.9 us, gpu 427.6 us
  524288 B: wall 572.7 us, gpu 572.4 us
```

So the remaining GDA loss is not Python enqueue overhead or host scheduling; it
is device-side work inside the RCCL/rocSHMEM GDA path.

`RCCL_ROCSHMEM_SOURCE_HEAP=1` was also tested and is worse on this shape:

```text
RCCL_ROCSHMEM_SOURCE_HEAP=1:
  262144 B: 6120.1 us
  524288 B: 25866.1 us
```

Keeping RCCL's source scratch host-coherent is the least-bad current setting.
The next useful experiment is lower-level device instrumentation inside the
RCCL GDA kernel/rocSHMEM alltoall call to split:

1. RCCL staging fences/synchronization around the call
2. rocSHMEM payload WRITE + quiet
3. rocSHMEM signal WRITE + quiet
4. local pSync wait
5. copy-out from host-coherent scratch

The coarse timings already show copy-in/copy-out are not the dominant term, but
the GPU-event data confirms that the remaining gap is on-device and should be
instrumented there.

The rocSHMEM-local alltoall diagnostic split, run with `-z 512 -n 20 -noverif`,
shows the standalone rocSHMEM protocol is now payload-dominated rather than
signal-wait dominated:

```text
ROCSHMEM_GDA_USB4_ALLTOALL_MODE=0 full:
  262144 B: 276.61 us
  524288 B: 486.44 us

ROCSHMEM_GDA_USB4_ALLTOALL_MODE=1 payload-only:
  262144 B: 265.15 us
  524288 B: 460.67 us

ROCSHMEM_GDA_USB4_ALLTOALL_MODE=2 signal+wait-only:
  262144 B: 36.29 us
  524288 B: 27.32 us
```

So after the local pSync backoff fix, standalone rocSHMEM alltoall is behaving
as expected. The application gap is specific to RCCL's device-kernel integration
with rocSHMEM, not to the raw USB4 alltoall protocol.

## Follow-Up: Host-Stream Alltoall

An opt-in RCCL host-stream route was added:

```text
RCCL_ROCSHMEM_HOST_STREAM_ALLTOALL=1
```

Instead of launching the RCCL device-side `AlltoAllGda` kernel, this path stages
PyTorch's tensor into RCCL's rocSHMEM scratch buffer, calls
`rocshmem_alltoallmem_on_stream()`, then stages the result back out. It also
supports the existing component modes from `RCCL_ROCSHMEM_GDA_BENCH_MODE`:

```text
0 full path
1 copy-in only
2 rocSHMEM exchange only
3 copy-out only
4 copy-in + copy-out
5 rocSHMEM exchange + copy-out
```

The useful result is diagnostic rather than final performance. Copy-only modes
are tens of microseconds, so tensor staging is not the dominant cost. Modes that
include the rocSHMEM exchange move DV WQEs and remain correct, but can be much
slower under host load. The expanded counters showed:

```text
dv_admission_attempts == dv_poll_wqes
dv_backpressure_retry == 0
dv_fence_retry == 0
dv_hard_error == 0
```

That rules out the kernel admission/backpressure path for the observed
application latency. When the machines were loaded, raw
`rocshmem_alltoallmem_on_stream` still completed 512 KiB in about `494-664 us`,
while RCCL/PyTorch host-stream exchange and `rccl-tests alltoall_perf` were in
the multi-millisecond range. Those live numbers are not clean baselines because
`strix-2` was compiling during the run, but the robustness result is useful:
there were no hard errors, data-path errors, or admission retries.

The next clean benchmark should compare these four paths with both Strix hosts
idle:

1. raw rocSHMEM `Alltoallmem_On_Stream`
2. `rccl-tests alltoall_perf` ordinary RCCL fallback
3. `rccl-tests alltoall_perf` host-stream GDA
4. PyTorch `dist.all_to_all_single` host-stream GDA

If raw rocSHMEM remains fast while RCCL tests are slow, the bottleneck is in
RCCL's integration with rocSHMEM. If RCCL tests are fast but PyTorch is slow,
the bottleneck is in PyTorch ProcessGroup/RCCL orchestration.

## Interpretation

This is a meaningful application-level smoke:

- Python application code uses PyTorch distributed collectives.
- PyTorch loads our Nix-built RCCL rather than the bundled wheel RCCL.
- RCCL routes threshold-sized all-to-all traffic into rocSHMEM.
- rocSHMEM drives the USB4 GDA transport.
- Kernel-side DV counters prove the transport was exercised.
- No new `dv_hard_error` occurred.

This is not yet a vLLM tensor-parallel benchmark. vLLM TP primarily stresses
all-reduce/all-gather, while the USB4 rocSHMEM RCCL path currently gives us a
validated all-to-all/all-to-allv route. Running vLLM TP=2 today would mostly
measure the ordinary RCCL path, not the USB4 GDA path.

## Next

1. Keep the TheRock wrapper as the default application smoke environment.
2. Use `usb4_pytorch_smoke.sh` as the quick application-level regression.
3. Keep `ROCSHMEM_GDA_USB4_ALLTOALL_ACK=0` as the stable smoke default; ACK-off
   is now protected by double-buffered pSync generations.
4. Do not add more RCCL GDA collectives until the current all-to-all bottleneck
   is localized. The next credible work is inside rocSHMEM all-to-all
   exchange/progress: sequence/pSync waits, per-iteration signal traffic, and
   whether the current payload-then-signal protocol can be collapsed into a
   receiver-ordered or kernel-compound operation without sender-side retry
   storms.
