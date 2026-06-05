# 2026-06-04 GDA IOMMU Revive

Goal: resume the stale GDA branch with IOMMU enabled on the Strix Halo hosts.

## Host State

- Worktree: `/mnt/Home/src/thunderbolt-ibverbs-gda-iommu-revive`
- Branch: `codex/gda-iommu-revive-20260604`
- Base: `grw/feat/usb4-gda-port4hca` at `e4d36be`
- Hosts: `strix-1`, `strix-2`
- Kernel: `7.1.0-rc1`
- IOMMU: enabled with `iommu=pt`; `/sys/class/iommu/ivhd0` present
- RDMA devices after reload: `usb4_rdma5`, `usb4_rdma6`, both `ACTIVE` and `LINK_UP`

The initially loaded module was not DV-capable. `tbv_dv_caps_probe` returned
`Protocol not supported (93)`. A fresh GDA module was built against the Strix
kernel headers and loaded on both hosts.

## Code Changes

Correctness changes made in this revive worktree:

- `thunderbolt-ibverbs/kernel/ibdev.c`: do not count
  `USB4_RDMA_DV_CQE_STALE_GEN` as `dv_hard_error`. Stale generation is an
  expected, observable CQE status used by the negative queue test. Other
  non-success CQE statuses still increment `dv_hard_error`.
- `thunderbolt-ibverbs/kernel/ibdev.c`: add `write_zcopy=0|1`, default off.
  Native RDMA WRITE stays on staged copy unless experimental WRITE zcopy is
  explicitly enabled.
- `thunderbolt-ibverbs/kernel/path.c`: add `native_tx_max_inflight=<N>` and
  use it to throttle native data TX descriptors per path before waiting for
  completions.
- `thunderbolt-ibverbs/kernel/ibdev.c`: add delayed duplicate SEND_ACK retries
  with `send_ack_redundancy=<1..8>`, default 3. This covers missing ACK frames
  without keeping a QP alive after userspace teardown.
- `flake.nix`: set the linux_perf reload defaults to
  `zcopy_min_bytes=0 native_tx_max_inflight=8 send_ack_redundancy=3
  native_fragment_striping=0`.
- `userspace/bench/hip_reg_mr_probe.cpp`: add `TBV_IB_DEV` and `--dev DEV` so
  the probe can target live devices such as `usb4_rdma5` instead of the old
  hardcoded `usb4_rdma0`.
- `thunderbolt-ibverbs/README.md`: document the current staged-WRITE default
  and the new correctness parameters.

The original checkout at `/mnt/Home/src/thunderbolt-ibverbs-gda` was left with
only the pre-existing `module/apple_rdma/apple_rdma.c` user edit.

## DV Caps And Queue Smoke

After reloading the rebuilt module, both hosts reported DV ABI 2:

```text
device=usb4_rdma5 abi=2 caps=0x000001ff send=1 send_imm=1 write=1 write_imm=1 fence=1 max_sq=1024 default_sq=256 max_cq=4096 default_cq=512 wqe=64 cqe=64 doorbell_record=128 doorbell_page=4096 tail_index_bits=24 tail_generation_bits=8
```

Queue lifecycle smoke passed on both hosts:

```text
create_queue qp_num=2304 generation=1 sq_entries=256 cq_entries=512
kick_nop wr_id=0x4b49434b4e4f5031 status=0 opcode=0 sq_head=1 cq_tail=1 qp_state=0
kick_stale_gen wr_id=0x4b49434b5354414c status=6 opcode=0 sq_head=2 cq_tail=2 qp_state=0
destroy_queue ok
```

With the counter patch loaded, the same stale-generation negative test leaves
`dv_hard_error=0` on both hosts.

## Peer DV SEND Smoke

Device pair: `usb4_rdma5` on both hosts, GID index 1, `strix-2` sending to
`strix-1` at `192.168.23.136`, no userspace kick.

Small run before the counter patch:

```text
size=65536 count=16 depth=16
send_complete elapsed_sec=0.001695 gbps=4.949
recv_complete elapsed_sec=0.002000 gbps=4.194
```

Sustained run before the counter patch:

```text
size=65536 count=4096 depth=64
send_complete elapsed_sec=0.244603 gbps=8.779
recv_complete elapsed_sec=0.244924 gbps=8.768
```

Sustained run after the counter patch and module reload:

```text
size=65536 count=4096 depth=64
send_complete elapsed_sec=0.253088 gbps=8.485
recv_complete elapsed_sec=0.253500 gbps=8.471
```

Final post-run counters were clean:

- `strix-2`: `dv_poll_wqes=4096`, `dv_poll_errors=0`, `dv_hard_error=0`,
  `data_wr_send=4096`, `data_wr_timeout=0`, `data_wr_copy_error=0`,
  `data_tx_errors=0`, `data_cq_overflow=0`
- `strix-1`: `dv_poll_errors=0`, `dv_hard_error=0`, `data_wr_timeout=0`,
  `data_rx_bad_frame=0`, `data_rx_bad_header=0`, `data_rx_copy_error=0`,
  `data_cq_overflow=0`
- Both rail queues drained: `ctrl_q=0`, `data_q=0`, `inflight=0`,
  `free=1024`, `raw_active=0`

Sustained sender-side credit stalls were observed under load
(`data_tx_credit_stalls=62435` in the final run), but credits recovered and no
timeout or data error followed.

## HIP/GDA Probes

`strix-2` has a working HIP device and passed the single-host GDA visibility
probes.

Host-coherent:

```text
direction=dv-producer iterations=10000 status=OK mismatches=0
kind=host-coherent direction=kernel-cqe-to-gpu-plain-load iterations=10000 status=OK avg_us=2.001 gpu_seen=10000 gpu_error=0
```

Host-uncached:

```text
direction=dv-producer iterations=10000 status=OK mismatches=0
kind=host-uncached direction=kernel-cqe-to-gpu-plain-load iterations=10000 status=OK avg_us=1.894 gpu_seen=10000 gpu_error=0
```

Managed:

```text
direction=dv-producer iterations=10000 status=OK mismatches=0
kind=managed direction=kernel-cqe-to-gpu-plain-load iterations=10000 status=OK avg_us=1.953 gpu_seen=10000 gpu_error=0
```

Host:

```text
direction=dv-producer iterations=10000 status=OK mismatches=0
kind=host direction=kernel-cqe-to-gpu-plain-load iterations=10000 status=OK avg_us=2.034 gpu_seen=10000 gpu_error=0
```

MR registration on `strix-2` against `usb4_rdma5` also passed:

```text
kind=host-coherent method=reg_mr gpu_touch=0 size=4096 reg_mr=OK
kind=host-uncached method=reg_mr gpu_touch=0 size=4096 reg_mr=OK
kind=device method=dmabuf gpu_touch=1 size=4096 fd=9 offset=0 reg_mr=OK
```

Initially, `strix-1` could not run HIP probes. It had `/dev/kfd` but no
`/dev/dri`, and amdgpu failed during boot:

```text
amdgpu 0000:0a:00.0: Unable to locate a BIOS ROM
amdgpu 0000:0a:00.0: Fatal error during GPU init
amdgpu 0000:0a:00.0: probe with driver amdgpu failed with error -22
```

Because of that, the peer `hip_rdma_write_visibility_probe` was not run. The
probe initializes HIP unconditionally on both roles, so it needs both hosts to
have working HIP devices.

## Follow-up: strix-1 VBIOS Fix

The `strix-1` amdgpu failure was not missing linux-firmware. The failing boot
had these experimental PCI enumeration parameters:

```text
pci=realloc,assign-busses pcie_ports=native
```

`strix-2` did not have those parameters and fetched VBIOS from VFCT normally.
Disabling the parameters in `/mnt/Home/src/nixos-config/machines/x86/strix-halo/default.nix`,
deploying the next boot generation, and rebooting `strix-1` fixed amdgpu:

```text
amdgpu 0000:c6:00.0: Fetched VBIOS from VFCT
[drm] Initialized amdgpu 3.64.0 for 0000:c6:00.0 on minor 0
```

After reboot, `strix-1` exposed both `/dev/kfd` and `/dev/dri/renderD128`.
HIP probes that previously failed then passed:

```text
kind=host-coherent direction=dv-producer iterations=10000 status=OK mismatches=0
kind=host-uncached direction=dv-producer iterations=10000 status=OK mismatches=0
kind=host-coherent direction=kernel-cqe-to-gpu-plain-load iterations=10000 status=OK avg_us=1.958 gpu_seen=10000 gpu_error=0
kind=host-uncached direction=kernel-cqe-to-gpu-plain-load iterations=10000 status=OK avg_us=1.931 gpu_seen=10000 gpu_error=0
kind=host-coherent method=reg_mr gpu_touch=0 size=4096 reg_mr=OK
kind=host-uncached method=reg_mr gpu_touch=0 size=4096 reg_mr=OK
kind=device method=dmabuf gpu_touch=1 size=4096 reg_mr=OK
```

## Peer RDMA Write Visibility After strix-1 Fix

With `strix-1` as GPU-visible receiver and `strix-2` as sender, small RDMA
WRITE visibility tests passed:

```text
kind=host-coherent mode=normal size=64 count=1000 status=OK avg_us=168.834
kind=host-uncached mode=normal size=64 count=1000 status=OK avg_us=168.946
```

A larger host-uncached run failed:

```text
kind=host-uncached mode=normal size=65536 count=256
send wc error wr_id=3 status=12 opcode=1 byte_len=0
receiver: timeout waiting gpu_seen=1 got=0 signal=0
```

The first failure used WRITE zcopy (`data_wr_zcopy=1`) and left raw-stream TX
state stuck (`zcopy_inflight=3`, `raw_active=1`). After rebooting both Strix
hosts, reloading the same module with `zcopy_min_bytes=0` made the same
64 KiB host-uncached probe pass once:

```text
send_result ... size=65536 count=256 status=OK elapsed_sec=0.095057 avg_us=371.318
recv_result ... size=65536 count=256 status=OK elapsed_sec=0.095093 avg_us=371.456
```

Counters confirmed the isolation on the sender:

```text
data_wr_send=512
data_wr_copied=512
data_wr_zcopy=0
data_wr_timeout=0
data_tx_posted=4608
data_tx_completed=4608
```

I patched the branch to make staged WRITE the default correctness path:

- add `write_zcopy=0|1`, default off, so native RDMA WRITE zcopy is gated even
  if `zcopy_min_bytes` is set;
- change the linux_perf reload options from `zcopy_min_bytes=4096` to
  `zcopy_min_bytes=0`;
- keep `USB4_RDMA_DV_CQE_STALE_GEN` from counting as a hard DV error;
- add `--dev DEV` / `TBV_IB_DEV` to `hip_reg_mr_probe`.

The next normal 64 KiB run still exposed a copied-path completion problem:

```text
send wc error wr_id=219 status=12 opcode=1 byte_len=0
receiver: timeout waiting gpu_seen=110 got=109 signal=109
```

The receiver had accepted all copied WRITE frames (`data_rx_op_write=1962`),
but the sender had one WR timeout, one staged TX frame still inflight, and one
missing ACK group:

```text
sender data_wr_send=218 data_wr_copied=218 data_wr_zcopy=0
sender data_wr_timeout=1 data_tx_posted=1962 data_tx_completed=1961
sender path_tx inflight=1 zcopy_inflight=0 raw_active=0
```

I then tried two diagnostic changes that did not fix the timeout:

- redundant ACK control frames, first on the inbound rail and then over all
  ready native rails for the peer;
- capping copied RDMA WRITE payload fragments at
  `TBV_NATIVE_DATA_MAX_PAYLOAD - 1` to avoid full-size `frame.size=0` TX
  descriptors.

The redundant ACK run made duplicate ACKs visible on the sender
(`data_rx_ack` rose well above WR count and split across both rails), but did
not fix the timeout. One run with multi-rail ACKs failed at WR 51:

```text
send wc error wr_id=51 status=12 opcode=1 byte_len=0
sender data_wr_send=50 data_wr_timeout=1 data_wr_live=1
sender data_tx_posted=450 data_tx_completed=446
sender data_rx_ack=292
```

After recovering `strix-2`, the fragment-cap run failed similarly at WR 39:

```text
send wc error wr_id=39 status=12 opcode=1 byte_len=0
receiver: timeout waiting gpu_seen=20 got=19 signal=19
sender data_wr_send=38 data_wr_timeout=1 data_wr_live=1
sender data_tx_posted=342 data_tx_completed=338
sender data_rx_ack=220
```

The remaining signature was not GPU visibility; it was native TX/ACK
completion reliability under large copied WRITE framing.

The fragment-cap patch was removed. The branch then kept two changes that match
the failure signatures:

- `native_tx_max_inflight=8`: prevents the local native TX completion leak seen
  as `data_tx_posted > data_tx_completed` and `path_tx inflight > 0`;
- `send_ack_redundancy=3`: sends two delayed duplicate ACKs after the immediate
  ACK, using a response reference on the inbound path.

With those two changes plus staged WRITE defaults, the 64 KiB peer visibility
probe now survives clean reloads and the `strix-2` reboot.

Host-uncached repeat after `strix-2` reboot:

```text
send_result source_kind=malloc source_fill=cpu source_reg=reg_mr size=65536 count=256 status=OK elapsed_sec=0.099165 avg_us=387.363
recv_result kind=host-uncached mode=normal size=65536 count=256 status=OK elapsed_sec=0.099192 avg_us=387.469 gpu_spins=13157
```

Post-run counters:

```text
strix-1 receiver:
data_tx_posted=1680 data_tx_completed=1680
data_rx_completed=4608 data_rx_credit_sent=4608 data_rx_op_write=4608
data_wr_timeout=0 data_rx_no_qp=0
path_tx inflight=0 zcopy_inflight=0 raw_active=0

strix-2 sender:
data_wr_send=512 data_wr_copied=512 data_wr_zcopy=0
data_wr_timeout=0 data_tx_posted=4608 data_tx_completed=4608
data_tx_credit_received=4608 data_rx_ack=1494 data_rx_no_qp=42
path_tx inflight=0 zcopy_inflight=0 raw_active=0
```

Host-coherent repeat after another clean reload:

```text
send_result source_kind=malloc source_fill=cpu source_reg=reg_mr size=65536 count=256 status=OK elapsed_sec=0.098911 avg_us=386.371
recv_result kind=host-coherent mode=normal size=65536 count=256 status=OK elapsed_sec=0.098964 avg_us=386.578 gpu_spins=13012
```

Post-run counters:

```text
strix-1 receiver:
data_tx_posted=1680 data_tx_completed=1680
data_rx_completed=4608 data_rx_credit_sent=4608 data_rx_op_write=4608
data_wr_timeout=0 data_rx_no_qp=0
path_tx inflight=0 zcopy_inflight=0 raw_active=0

strix-2 sender:
data_wr_send=512 data_wr_copied=512 data_wr_zcopy=0
data_wr_timeout=0 data_tx_posted=4608 data_tx_completed=4608
data_tx_credit_received=4608 data_rx_ack=1500 data_rx_no_qp=36
path_tx inflight=0 zcopy_inflight=0 raw_active=0
```

The `data_rx_no_qp` count on `strix-2` is from late duplicate ACKs that arrive
after userspace has destroyed the sender QP. It is not causing WR failure or
leaving live verbs objects, but it is the main remaining correctness wart in
this workaround.

## Takeaways

- IOMMU enabled with `iommu=pt` did not break the GDA DV ABI, queue lifecycle,
  no-kick SEND path, or HIP-visible host-memory probes on `strix-2`.
- Host-uncached now passes the single-host DV producer and kernel-CQE visibility
  probes for 10k iterations on `strix-2`.
- The `strix-1` amdgpu blocker was caused by experimental PCI enumeration
  parameters, not missing linux-firmware.
- Small peer RDMA WRITE visibility into `strix-1` GPU-visible host memory now
  works.
- Large 64 KiB RDMA WRITE visibility now passes for both host-uncached and
  host-coherent receiver memory with staged WRITE, native TX throttling, and
  delayed ACK retries.
- Current code keeps the validated correctness changes: WRITE zcopy is disabled
  by default, stale DV generations do not count as hard errors, native TX is
  throttled, SEND_ACK is retried, and the HIP MR probe can select the active
  RDMA device.
- Delayed duplicate ACKs can arrive after sender QP teardown and show up as
  `data_rx_no_qp`; this is currently benign in the tested 64 KiB runs but should
  be refined before treating the workaround as final protocol behavior.

## Next

1. Run longer peer visibility loops and larger sizes with the same defaults to
   check whether `native_tx_max_inflight=8` is conservative enough.
2. Refine delayed ACK teardown behavior so redundant ACKs stop before they
   become `data_rx_no_qp` after the remote QP is destroyed.
3. Decide whether to forward-port this GDA stack onto current `origin/main` or
   keep reviving the historical branch in place.
