# 2026-06-07 Post-Copy Native TX Analysis

This is a local-only analysis of saved app-gate logs. No new Strix Thunderbolt
traffic was generated.

## Tooling

`userspace/bench/tbv_app_gate_summarize.sh` now reports DV WRITE bucket
throughput in addition to time:

```text
dv_write_tx_mr_bucket aggregates:
suite collective mode bucket count bytes avg_bytes avg_ms copy_avg_ms postcopy_avg_ms total_gbps copy_gbps postcopy_gbps
```

`avg_bytes` confirms the WRITE size seen by the module, while `postcopy_gbps`
turns the post-copy/native-TX span into a direct throughput signal.

## Saved-log comparison

Post-SG-cursor full correctness run:

```text
root:
  /mnt/Home/tmp/tbv-app-gate-logs/pytorch-sgcursor-full-rotate-20260606-2203

bucket count bytes   avg_bytes avg_ms copy_ms postcopy_ms total_gbps copy_gbps postcopy_gbps
0      8     8388608 1048576   1.122  0.054   1.067       7.48       154.04    7.86
1      4     4194304 1048576   1.104  0.046   1.058       7.60       184.28    7.93
2      4     4194304 1048576   1.073  0.043   1.030       7.82       193.35    8.15
3      4     4194304 1048576   1.057  0.043   1.014       7.93       195.86    8.27
```

4 MiB payload-only run with `ROCSHMEM_GDA_USB4_A2A_CHUNK_BYTES=1048576`:

```text
root:
  /mnt/Home/tmp/tbv-app-gate-logs/pytorch-chunk1m-payload-4m-20260606-2232

bucket count bytes    avg_bytes avg_ms copy_ms postcopy_ms total_gbps copy_gbps postcopy_gbps
0      16    16777216 1048576   2.810  0.056   2.754       2.98       149.61    3.05
1      16    16777216 1048576   4.782  0.078   4.704       1.75       107.14    1.78
2      16    16777216 1048576   3.424  0.079   3.345       2.45       106.50    2.51
3      16    16777216 1048576   2.969  0.075   2.893       2.83       111.34    2.90
```

The 4 MiB payload row still uses 1 MiB module-level WRs. The copy span remains
small, but post-copy/native TX drops from about 8 Gb/s to roughly 1.8-3.1 Gb/s.
That makes the next bottleneck a queueing/completion/backpressure problem under
larger exchanges, not an MR-offset or CPU-copy problem.

## Tail correlation

Saved 30-rep full app run:

```text
root:
  /mnt/Home/tmp/tbv-app-gate-logs/pytorch-hoststream-writegaprnr30-qptimeout14-20260606-171117
```

Per-rep split from `tbv_app_gate_summarize.sh`:

```text
fast_no_recovery:
  reps=13
  1 MiB avg=11213.5 us
  2 MiB avg=21650.8 us
  2 MiB min/max=20413.8/24050.1 us

recovery:
  reps=17
  1 MiB avg=21946.9 us
  2 MiB avg=77571.9 us
  2 MiB min/max=21033.6/221343.9 us
  average recovery events per affected rep=956.2
```

Aggregate recovery counters across the same 30 reps:

```text
wr_retx=0
rnr_retx=258
ack_retry=5163
ack64=1759
late_ack=6264
dup_ack=95
write_gap_rnr=10835
tx_rnr=10835
rx_rnr=10835
dv_hard=0
wr_to=0
wr_exh=0
tx_err=0
tx_post=393356
tx_comp=393356
```

Interpretation: the application tail is dominated by recovered receive-side
ordering/backpressure events. The kernel correctness story is good in these
rows, but the performance target is now reducing how often a larger host-stream
exchange enters the RNR/write-gap recovery path and why that path inflates the
post-copy/native-TX span.

## Next live-test recipe

When the Strix pair is available again, run a small chunk-size discriminator
instead of another broad app sweep:

```bash
for chunk in 262144 524288 1048576 2097152; do
  RCCL_ROCSHMEM_THRESHOLD=67108864 \
  RCCL_ROCSHMEM_GDA_BENCH_MODE=2 \
  ROCSHMEM_GDA_USB4_A2A_CHUNK_BYTES=$chunk \
  TBV_APP_TIMEOUT=420 \
    bash userspace/bench/tbv_app_gate.sh \
      --hosts 192.168.23.136,192.168.23.192 \
      --counter-hosts root@192.168.23.136,root@192.168.23.192 \
      --iface eno1 \
      --log-root "/mnt/Home/tmp/tbv-app-gate-logs/postcopy-chunk${chunk}-$(date +%Y%m%d-%H%M%S)" \
      --skip-rccl --pytorch \
      --pytorch-wrapper /nix/store/3mr3fgrn6znah88jrc42r2wh692x24km-vllm-env-therock-usb4-hostheap-gfx1151 \
      --rccl-install /mnt/Home/tmp/rccl-hoststream-waitbudget-install \
      --rocshmem-install /mnt/Home/tmp/rocshmem-waitbudget-install \
      --rocm-path /nix/store/263sdskvmyld0qqcz8f7qf0zsx11i6l8-therock-rocm-sdk-gfx1151-7.13.0a20260515 \
      --mpi-home /nix/store/ciq3sjjgih6p38rlyfjsd2jjkzl8nfz1-openmpi-5.0.10 \
      --rdma-lib /nix/store/wc6j2l3k5qdjzwkvd27nb4v490qn0i9w-rdma-core-usb4-62.0/lib \
      --numactl-lib /nix/store/8xlwd35bpmj7n6bzjwfnr6vidpwicjdd-numactl-2.0.18/lib \
      --modes hoststream \
      --reps 3 \
      --pytorch-sizes 4194304 \
      --pytorch-iters 4 \
      --torch-collectives all_to_all \
      --torch-validate 0 \
      --pytorch-dv-check require \
      --torch-rccl-lib auto \
      --expected-rccl-lib /mnt/Home/tmp/rccl-hoststream-waitbudget-install/lib/librccl.so.1
done
```

Summarize each root with:

```bash
bash userspace/bench/tbv_app_gate_summarize.sh "$root"
```

Decision table:

```text
smaller chunk improves postcopy_gbps and reduces RNR/write-gap counters
  -> native queue depth / receiver reorder pressure is the primary performance lever

postcopy_gbps remains low at every chunk size
  -> focus on Thunderbolt ring TX completion/drain latency or path scheduler pacing

only some chunks avoid recovery but app time remains high
  -> split hoststream exchange into post/quiet/wait phases at the rocSHMEM layer
```
