# 2026-06-07 Post-Atomic App Gates

## Context

After the native atomic PSN-ordering fix was deployed on both Strix hosts, the
next check was to move back up the stack:

```text
strix-1: /nix/store/g4hb7hpy5n97wcvycmj7r9yfpj8nnv5m-nixos-system-strix-1-26.11pre-git
strix-2: /nix/store/ciaa5ng0acml95gqzddbx9vjlrd5a6vx-nixos-system-strix-2-26.11pre-git
kernel: 7.0.10
RCCL: /mnt/Home/tmp/rccl-hoststream-waitbudget-install/lib/librccl.so.1
rocSHMEM: /mnt/Home/tmp/rocshmem-waitbudget-install
```

The waitbudget RCCL library contains the host-stream diagnostics symbols:

```text
RCCL_ROCSHMEM_HOST_STREAM_ALLTOALL
RCCL_ROCSHMEM_GDA_BENCH_MODE
RCCL_ROCSHMEM_HOST_STREAM_TIMING
```

## RCCL Tests

Small RCCL app gate:

```text
log root: /mnt/Home/tmp/tbv-app-gate-logs/post-atomic-rccl-smoke-20260607-010509
collectives: alltoall, alltoallv
modes: fallback, hoststream, device
sizes: 65536,131072,262144
iters/warmup/reps: 1/1/1
status: pass
```

Representative timing summary:

```text
alltoall fallback   65536..262144: 119.61, 99.25, 123.51 us
alltoall hoststream 65536..262144: 402.81, 345.09, 573.24 us
alltoall device     65536..262144: 1199.56, 1892.72, 5729.62 us

alltoallv fallback   65536..262144: 128.76, 147.49, 190.48 us
alltoallv hoststream 65536..262144: 326.88, 306.80, 495.91 us
alltoallv device     65536..262144: 747.11, 1123.58, 1953.49 us
```

Hard-counter summary across all six RCCL rows:

```text
wrong results                         0
data_wr_timeout                       0
data_wr_retry_exhausted               0
data_wr_retransmit                    0
data_tx_errors                        0
dv_hard_error                         0
data_tx_posted/completed              balanced in every row
```

GDA modes still produced retryable RNR/reorder pressure. For example,
`alltoall` device mode reported `rnr_retx=8`, `ack_retry=8`,
`write_gap_rnr=48`, and `tx_rnr/rx_rnr=48/48`, but recovered cleanly.

## PyTorch Gate

The first PyTorch attempt intentionally exposed a packaging trap:

```text
log root: /mnt/Home/tmp/tbv-app-gate-logs/post-atomic-pytorch-smoke-20260607-010624
status: fail
```

Both modes completed the all-to-all and had clean driver counters, but the
Python workers crashed in teardown. The log showed two RCCL libraries mapped:

```text
/mnt/Home/tmp/rccl-hoststream-waitbudget-install/lib/librccl.so.1.0
/nix/store/74kx31vim3ynwpivjlxq04wbdl62nrbg-rccl-usb4-hostheap-gfx1151-2.28.9-local/lib/librccl.so.1.0
```

Root cause: the USB4 vLLM/PyTorch wrapper's `bin/python` and `bin/python3` are
shell wrappers that force the older RCCL into `LD_LIBRARY_PATH` and
`LD_PRELOAD`. The app gate was also injecting the rebuilt RCCL through
`TBV_TORCH_RCCL_LIB`, causing both libraries to be present in one process.

Fix: when `--pytorch-python` is not supplied, `tbv_app_gate.sh` now resolves
the wrapper's inner `exec ".../bin/python3"` target and runs that interpreter
directly. The app gate already supplies the desired RCCL/rocSHMEM environment,
so bypassing the outer wrapper is the correct default for controlled PyTorch
gates.

Passing PyTorch rerun with the fixed default:

```text
log root: /mnt/Home/tmp/tbv-app-gate-logs/post-atomic-pytorch-auto-inner-20260607-011001
collective: all_to_all
modes: fallback, hoststream
size: 65536
iters/reps: 1/1
resolved python: /nix/store/ckc6lc8s0mx7qfmskdlad3kndlf94ir3-vllm-env-therock-gfx1151/bin/python3
status: pass
```

The passing run mapped only the rebuilt waitbudget RCCL:

```text
loaded_collective_lib counts:
  4 /mnt/Home/tmp/rccl-hoststream-waitbudget-install/lib/librccl.so.1.0
```

PyTorch timing and counters:

```text
fallback all_to_all_single 65536:   235.6 us, gpu 231.4 us
hoststream all_to_all_single 65536: 1988.6 us, gpu 1983.7 us

data_wr_retransmit                   0
data_wr_timeout                      0
data_wr_retry_exhausted              0
data_tx_errors                       0
dv_hard_error                        0
data_tx_posted/completed fallback    526/526
data_tx_posted/completed hoststream  395/395
```

## Interpretation

The post-atomic module is now back through:

1. focused rocSHMEM atomic/fence ordering tests,
2. raw rocSHMEM alltoall smoke,
3. RCCL alltoall/alltoallv app gates, and
4. PyTorch distributed all-to-all with the rebuilt RCCL mapped exactly once.

The remaining application-level issue is performance, not the atomic
correctness bug: fallback remains faster than hoststream/device GDA in these
small rows, and GDA modes still show retryable RNR pressure under RCCL tests.
