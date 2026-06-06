# 2026-06-07 Post-Reboot Mixed App Gate

Purpose:

Validate the app gate after reboot with separate DV expectations for the
RCCL-tests and PyTorch rows. This avoids treating PyTorch hoststream copied-native
traffic as a failed DV benchmark while still requiring the RCCL-tests ladder to
exercise the module DV path.

Command:

```bash
TBV_APP_TIMEOUT=420 bash userspace/bench/tbv_app_gate.sh \
  --hosts 192.168.23.136,192.168.23.192 \
  --counter-hosts root@strix-1.local,root@strix-2.local \
  --iface eno1 \
  --log-root /mnt/Home/tmp/tbv-app-gate-logs/post-reboot-mixed-ladder-pytorch-dvcheck-20260607-014031 \
  --rccl-tests-dir /nix/store/4bhvq0qphnq9ardka495pmji5f5130a0-rccl-tests-usb4-hostheap-gfx1151-2.14.1-local/bin \
  --rccl-install /mnt/Home/tmp/rccl-hoststream-waitbudget-install \
  --rocshmem-install /mnt/Home/tmp/rocshmem-waitbudget-install \
  --rocm-path /nix/store/263sdskvmyld0qqcz8f7qf0zsx11i6l8-therock-rocm-sdk-gfx1151-7.13.0a20260515 \
  --mpi-home /nix/store/ciq3sjjgih6p38rlyfjsd2jjkzl8nfz1-openmpi-5.0.10 \
  --rdma-lib /nix/store/wc6j2l3k5qdjzwkvd27nb4v490qn0i9w-rdma-core-usb4-62.0/lib \
  --numactl-lib /nix/store/8xlwd35bpmj7n6bzjwfnr6vidpwicjdd-numactl-2.0.18/lib \
  --sizes 262144,4194304 \
  --iters 3 --warmup 2 --reps 1 \
  --collectives alltoall \
  --modes hoststream \
  --pytorch \
  --pytorch-wrapper /nix/store/3mr3fgrn6znah88jrc42r2wh692x24km-vllm-env-therock-usb4-hostheap-gfx1151 \
  --pytorch-sizes 1048576 \
  --pytorch-iters 2 \
  --torch-collectives all_to_all \
  --master-port 29625 \
  --dv-check auto \
  --pytorch-dv-check forbid \
  --expected-rccl-lib /mnt/Home/tmp/rccl-hoststream-waitbudget-install/lib/librccl.so.1.0
```

Outcome:

```text
TBV app gate complete: status=0
summary: /mnt/Home/tmp/tbv-app-gate-logs/post-reboot-mixed-ladder-pytorch-dvcheck-20260607-014031/summary.txt
```

Route assertions:

- RCCL-tests `alltoall hoststream` ladder: `dv_poll_wqes +144`, so `--dv-check auto`
  correctly required DV traffic.
- PyTorch `all_to_all_single` 1 MiB: `dv_poll_wqes +0`, so
  `--pytorch-dv-check forbid` correctly accepted the copied-native route.
- Both rows had `data_tx_posted == data_tx_completed`, no TX errors, no RX
  cancels, no WR timeouts, and no retry exhaustion.

Selected timings:

```text
RCCL-tests alltoall hoststream:
  262144 bytes: 458.64 us
  524288 bytes: 625.76 us
  1048576 bytes: 2328.74 us
  2097152 bytes: 614.17 us
  4194304 bytes: 1291.82 us

PyTorch all_to_all_single 1048576 bytes:
  635.1 us/iter, gpu=631.7 us/iter, best logical/rank bandwidth 13.21 Gb/s
```

Counter profile:

```text
RCCL-tests hoststream:
  data_tx_posted/data_tx_completed: 37164/37164
  data_wr_rnr_retransmit: +8
  data_rx_ack_match_retried: +34
  data_rx_write_gap_rnr: +135
  data_wr_timeout/data_wr_retry_exhausted/data_tx_errors/data_rx_canceled: all +0

PyTorch hoststream:
  data_tx_posted/data_tx_completed: 2809/2809
  data_wr_timeout/data_wr_retry_exhausted/data_tx_errors/data_rx_canceled: all +0
```

Additional discriminator:

Two single-size RCCL-tests hoststream probes at 4 MiB produced valid native
traffic and balanced TX, but `dv_poll_wqes +0`, so a single-size row is not a
reliable DV discriminator for this app path. The 256 KiB to 4 MiB ladder remains
the reusable gate shape when `--dv-check auto` is intended to assert DV.
