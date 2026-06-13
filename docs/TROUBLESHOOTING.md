# Troubleshooting Guide

## Quick checklist

Before diving into individual symptoms, run through these basics on **both** nodes:

```sh
# 1. Module is loaded
lsmod | grep thunderbolt_ibverbs

# 2. Device is visible to libibverbs
ibv_devices

# 3. Kernel log shows successful peer negotiation
dmesg | grep thunderbolt_ibverbs

# 4. RDMA counters are non-zero after a transfer
cat /sys/kernel/debug/thunderbolt_ibverbs/summary
```

---

## Symptom: `ibv_devices` shows no `usb4_rdma*` device

**Check `register_verbs=1`.**  The module only calls `ib_register_device()` when
`register_verbs=1` is passed at load time.  Reload with:

```sh
sudo modprobe -r thunderbolt_ibverbs
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1
```

**Check the userspace provider.**  `libibverbs` discovers providers via
`/etc/libibverbs.d/` and `/usr/lib/libibverbs.d/`.  Confirm the provider is
installed:

```sh
ls /usr/lib/libibverbs.d/ | grep usb4
```

If it is missing, install the `usb4-rdma-provider` package for your distro (see
the README for download links).

**Check that the Thunderbolt cable is connected and the peer is enumerated.**

```sh
ls /sys/bus/thunderbolt/devices/
```

You should see `domain0/`, `0-0`, `0-1`, etc.  If not, check cable seating and
whether the host's Thunderbolt controller is enabled in the BIOS/UEFI.

---

## Symptom: device appears but `ibv_devinfo` returns errors

**Kernel mismatch.**  The module requires Linux 6.14+ (or the flake's
`linux-thunderbolt` kernel).  Check:

```sh
uname -r
```

If your kernel is older, either upgrade or build the Nix `linux-thunderbolt`
kernel from this flake.

**ABI mismatch after an in-place module upgrade.**  Unload and reload:

```sh
sudo modprobe -r thunderbolt_ibverbs
sudo modprobe thunderbolt_ibverbs profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1
```

---

## Symptom: no RDMA counters after a perftest run

This means traffic is not going through the Thunderbolt rings.  Check:

```sh
cat /sys/kernel/debug/thunderbolt_ibverbs/summary
```

**Verify both peers loaded with `negotiate_native=1`.**  If one side uses the
Apple transport and the other uses the native Linux transport, they will not
interoperate.

**Check that paths are up.**  After module load you should see log lines like:

```
thunderbolt_ibverbs: peer <UUID>: native path ready, N lanes
```

If you see `path setup failed` or similar, the Thunderbolt subsystem did not
allocate the DMA paths.  Reload both modules at the same time (or reload the
remote peer first, then the local one).

**Verify lane allocation.**  With `lanes=auto` the module requests as many lanes
as the controller advertises.  On some machines you may need to set
`lanes=2` explicitly.

---

## Symptom: `peer_allowlist` rejecting an expected peer

The `peer_allowlist` parameter accepts comma-separated remote UUIDs.  Find the
peer UUID with:

```sh
cat /sys/bus/thunderbolt/devices/0-1/unique_id
```

Then reload with:

```sh
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  peer_allowlist=<uuid1>,<uuid2>
```

If `peer_allowlist` is non-empty, any peer not listed is rejected even if
otherwise valid.  Leave `peer_allowlist` unset (empty) to accept all peers.

---

## Symptom: high latency or low bandwidth compared to benchmarks

**Check interrupt throttle.**  On kernels that export `tb_ring_throttling()`,
the `nhi_interrupt_throttle_ns` parameter controls the NHI interrupt coalescing
interval.  For lowest latency set it to `0`; for highest bandwidth experiment
with values in the 10–50 µs range:

```sh
sudo modprobe thunderbolt_ibverbs ... nhi_interrupt_throttle_ns=0
```

**Check zero-copy threshold.**  The `zcopy_min_bytes` parameter sets the minimum
transfer size to use zero-copy DMA.  Lower values reduce copies for small
messages at the cost of more DMA setup overhead.

**Check QP timeout.**  `qp_timeout_ms` controls the retransmit timeout.  If the
cable is intermittent, completions may be delayed by a timeout.

**Verify you are using the native transport.**  Apple-compatible mode
(`mac_compat`) has additional protocol overhead; `linux_perf` is the
fastest path between two Linux peers.

---

## Symptom: module fails to load (`insmod` / `modprobe` error)

**Missing kernel headers.**  The module is built against your running kernel's
headers.  Install them:

```sh
# Debian / Ubuntu
sudo apt install linux-headers-$(uname -r)

# Fedora
sudo dnf install kernel-devel kernel-headers

# Arch
sudo pacman -S linux-headers
```

**DKMS build error.**  Check the DKMS build log:

```sh
sudo dkms status
sudo cat /var/lib/dkms/thunderbolt-ibverbs/<ver>/<kernel>/build/make.log
```

---

## Collecting a bug report

Include the following in any bug report:

```sh
uname -a
lsmod | grep thunderbolt
dmesg | grep -i thunderbolt
dmesg | grep -i ibverbs
ibv_devices
cat /sys/kernel/debug/thunderbolt_ibverbs/summary 2>/dev/null || echo "(not loaded)"
```
