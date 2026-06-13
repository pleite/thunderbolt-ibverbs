# Apple Hardware Support

This document covers the `mac_compat` transport profile: which Apple devices
work, what the known limitations are, and how to set up an Apple↔Linux
verb exchange.

## Supported Hardware

The `mac_compat` (FA57) transport is validated against the following
Apple-side hardware:

| Machine | SoC | Thunderbolt | Status |
|---|---|---|---|
| Mac mini (M1, 2020) | M1 | TB3/USB4 | ✅ Working |
| Mac mini (M2, 2023) | M2 | TB4/USB4 | ✅ Working |
| Mac mini (M4, 2024) | M4 | TB5/USB4 | ✅ Working |
| MacBook Pro 14″/16″ (M1 Pro/Max, 2021) | M1 Pro/Max | TB4/USB4 | ✅ Working |
| MacBook Pro 14″/16″ (M3 Pro/Max, 2023) | M3 Pro/Max | TB4/USB4 | ✅ Working |
| MacBook Pro 14″/16″ (M4 Pro/Max, 2024) | M4 Pro/Max | TB5/USB4 | ✅ Working |
| Mac Pro (2023) | M2 Ultra | TB4/USB4 | ✅ Working |
| Mac Studio (M1/M2 Ultra, 2022/2023) | M1/M2 Ultra | TB4/USB4 | ✅ Working |
| Intel MacBook Pro (2018–2020) | Intel | TB3 | ⚠️ Untested |

The Linux peer can be any x86-64 or aarch64 machine with Thunderbolt 3/4/5 or
USB4 and Linux 6.14+ (or the `linux-thunderbolt` kernel from this flake).

## Thunderbolt Cable Requirements

Use an active Thunderbolt 3 or Thunderbolt 4 cable rated for 40 Gb/s. Passive
USB4 Gen 2×2 cables (20 Gb/s) work but cap bandwidth at ~2 GB/s per rail.

For Thunderbolt 5 ports (Mac mini M4, MacBook Pro M4) use a TB5/USB4 80 Gb/s
cable to unlock the full 80 Gb/s lane.

## macOS Requirements

- macOS 12 (Monterey) or newer.
- The `AppleThunderboltRDMA` and `AppleThunderboltIP` kernel extensions are
  loaded automatically when a Thunderbolt peer advertises the FA57 service.
- No additional software installation is required on the macOS side.

## Known Limitations

| Limitation | Detail |
|---|---|
| UC only, no RC | Apple's `AppleThunderboltRDMA` exposes UC Queue Pairs only. Reliable Connected (RC) operations are not supported on the Apple side. |
| Single rail | The FA57 service negotiates one DMA path per link. Multi-rail / lane-bonding as used by the native Linux transport is not available. |
| No RDMA WRITE with IMM | Apple's verbs implementation drops immediate-data WRITE completions silently; use plain RDMA WRITE or SEND instead. |
| GID auto-selection requires ThunderboltIP | The `mac_compat` profile relies on the minimal ThunderboltIP packet path (`tbnet_identity=minimal_packet`) to carry GID advertisements. Load the module on the Linux side before connecting the cable. |
| macOS sender-side credit stalls under load | Apple's flow-control window can stall on large messages (>1 MiB). Use multiple QPs or smaller messages to avoid this. |
| No IOMMU with mac_compat | DMA-remapping (IOMMU) is not yet validated against the Apple path. If the Linux host uses an IOMMU, set `iommu=off` or use a platform without one (direct-DMA mode). |
| perftest needs the Apple patch | The packaged `perftest` binary from `nix/perftest-apple-tn3205.patch` is required on the macOS side. Upstream perftest does not build for Darwin. |

## Loading the Module on the Linux Peer

Connect the Thunderbolt cable first. On the Linux host:

```sh
sudo modprobe thunderbolt_ibverbs \
  profile=mac_compat \
  bind_services=1 \
  allocate_rings=1 \
  start_rings=1 \
  enable_tunnels=1 \
  register_verbs=1
```

With `profile=mac_compat`, the module automatically:
- enables the FA57 Apple service (`apple_data=auto` → on)
- advertises a minimal ThunderboltIP packet identity
- waits for the Apple peer's LOGIN/LOGIN_RESPONSE exchange before
  publishing the verbs rail

Check that the device appeared:

```sh
dmesg | grep thunderbolt_ibverbs
ibv_devices
rdma link show
```

You should see a `usb4_rdma0` (or `usb4_apple0`) device listed.

## Apple↔Linux Interop Smoke Test

Use `tools/ci/apple-linux-interop-smoke.sh` to exercise the full interop
sequence from the Linux side and print clear pass/fail diagnostics.
See `tools/ci/apple-linux-interop-smoke.sh --help` for usage.

For a full two-node exchange using `mac_tb_rdma_probe` (macOS) and
`u4_pingpong` (Linux):

**Linux peer (server):**

```sh
u4_pingpong -d usb4_rdma0 -s
```

**macOS peer (client):**

```sh
MAC_TB_RDMA_PROBE_RTR=1 MAC_TB_RDMA_PROBE_SEND=1 \
  mac_tb_rdma_probe rdma_en1 <linux-peer-ip> <linux-qpn> 7
```

Replace `<linux-peer-ip>` with the IPv4 address shown by the Linux peer's
`ibv_devices` GID and `<linux-qpn>` with the QPN printed by `u4_pingpong`.

## perftest Interop

The `perftest` suite works for UC bandwidth and latency with the Apple patch
(`nix/perftest-apple-tn3205.patch`):

**Linux peer:**

```sh
ib_write_bw -d usb4_rdma0 --qp-timeout 25
```

**macOS peer (with patched perftest):**

```sh
ib_write_bw -d rdma_en1 <linux-peer-ip> --qp-timeout 25
```

Expect approximately 2–3 GB/s on a 40 Gb/s cable and 4–5 GB/s on an
80 Gb/s TB5 cable (single UC rail).
