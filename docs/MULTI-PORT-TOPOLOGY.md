# Multi-port topology guide

This document covers two practical cluster topologies for
`thunderbolt-ibverbs`:

1. **Two machines, two Thunderbolt ports each** — both ports active, all lanes
   saturated for maximum bandwidth between a single pair of hosts.
2. **Four machines, two Thunderbolt ports each** — ring topology that gives
   every node two RDMA peers and full fabric connectivity.

---

## What this driver exposes

Each Thunderbolt peer that successfully negotiates a native session is
registered as an independent `ib_device` (`usb4_rdma0`, `usb4_rdma1`, …).
A machine with two ports connected to two different peers therefore has two
separate RDMA devices.  NCCL/UCX can use all of them simultaneously.

The number of DMA lanes per link is controlled by the `lanes=` module
parameter.  Thunderbolt 4 supports up to two 20 Gbit/s lanes per cable
(≈ 40 Gbit/s raw in each direction); Thunderbolt 5 supports up to four 20
Gbit/s lanes (≈ 80 Gbit/s raw in each direction).  Use `lanes=auto` to
negotiate the maximum the cable and controller support, or specify an exact
count.

---

## Topology A — two machines, two ports each

```
  Machine A                    Machine B
  ┌──────────────┐             ┌──────────────┐
  │ TB port 0 ───┼─────────────┼─── TB port 0 │  cable 1
  │ TB port 1 ───┼─────────────┼─── TB port 1 │  cable 2
  └──────────────┘             └──────────────┘
  usb4_rdma0 (peer B/port0)    usb4_rdma0 (peer A/port0)
  usb4_rdma1 (peer B/port1)    usb4_rdma1 (peer A/port1)
```

Two cables between the same pair of machines create two independent
Thunderbolt domains.  Each domain negotiates its own DMA rings and appears as
a separate `usb4_rdma*` device.  With `native_fragment_striping=1` and
multiple QPs spread across both devices, you can saturate both links
concurrently.

### Step 1 — find the peer UUIDs

On each machine, with the cables connected:

```sh
for dev in /sys/bus/thunderbolt/devices/*/unique_id; do
    echo "$dev: $(cat $dev)"
done
```

Identify the UUIDs that belong to the *remote* machine.  You need these for
`peer_auth_acl`.

### Step 2 — choose a shared PSK

Generate a 16-byte (32 hex char) PSK.  Both machines must use the **same**
value for each peer pair:

```sh
# run once on either machine, share the result with the other
openssl rand -hex 16
# example output: 4a7f2c9d1e3b05f68a4c9d2e7f1b3a50
```

If machine A has two ports connected to machine B, both cables share the same
remote UUID (machine B's UUID), so a single PSK entry covers both links.

### Step 3 — load the module on both machines

Replace `<UUID-B>` / `<UUID-A>` with the UUID of the *remote* machine, and
`<PSK>` with the 32 hex char value from step 2.

**Machine A:**

```sh
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 \
  allocate_rings=1 \
  start_rings=1 \
  negotiate_native=1 \
  enable_tunnels=1 \
  register_verbs=1 \
  lanes=auto \
  native_fragment_striping=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  peer_auth_acl=<UUID-B>=<PSK>
```

**Machine B** (swap the UUID):

```sh
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 \
  allocate_rings=1 \
  start_rings=1 \
  negotiate_native=1 \
  enable_tunnels=1 \
  register_verbs=1 \
  lanes=auto \
  native_fragment_striping=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  peer_auth_acl=<UUID-A>=<PSK>
```

### Step 4 — verify both devices are up

```sh
ibv_devices
# device          	   node GUID
# ------          	----------------
# usb4_rdma0      	...
# usb4_rdma1      	...

rdma link show
dmesg | grep "thunderbolt_ibverbs.*path ready"
```

You should see two `usb4_rdma*` entries and two `path ready` log lines.

### Step 5 — verify bandwidth on both links

```sh
# Machine A — server on device 0 and device 1
ib_write_bw -d usb4_rdma0 &
ib_write_bw -d usb4_rdma1 &

# Machine B — client
ib_write_bw -d usb4_rdma0 <ip-of-A>
ib_write_bw -d usb4_rdma1 <ip-of-A>
```

### Step 6 — make the configuration persistent

```sh
# /etc/modprobe.d/thunderbolt-ibverbs.conf  (same content on both nodes,
# with the remote UUID and PSK swapped)
options thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  lanes=auto \
  native_fragment_striping=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  peer_auth_acl=<REMOTE-UUID>=<PSK>
```

---

## Topology B — four machines, two ports each (ring)

With two ports per machine and four machines, the natural topology is a ring.
Each machine is connected to its two neighbours; every node has two direct
RDMA peers and can reach the other two nodes via a single hop through a
neighbour.

```
  Machine A ─────── cable ─────── Machine B
      │                                │
    cable                            cable
      │                                │
  Machine D ─────── cable ─────── Machine C
```

Cable assignments (each machine uses both ports):

| Cable | Port on X         | Port on Y         |
|-------|-------------------|-------------------|
| A↔B  | Machine A port 0  | Machine B port 0  |
| B↔C  | Machine B port 1  | Machine C port 0  |
| C↔D  | Machine C port 1  | Machine D port 0  |
| D↔A  | Machine D port 1  | Machine A port 1  |

Each machine sees two remote peers and gets two `usb4_rdma*` devices:

| Machine | usb4_rdma0 (port 0) | usb4_rdma1 (port 1) |
|---------|---------------------|---------------------|
| A       | peer B              | peer D              |
| B       | peer A              | peer C              |
| C       | peer B              | peer D              |
| D       | peer C              | peer A              |

### Step 1 — gather UUIDs

On each machine (cables connected):

```sh
for dev in /sys/bus/thunderbolt/devices/*/unique_id; do
    echo "$dev: $(cat $dev)"
done
```

Map each UUID to its machine.  You need:

| Variable  | Value |
|-----------|-------|
| `UUID_A`  | UUID of machine A |
| `UUID_B`  | UUID of machine B |
| `UUID_C`  | UUID of machine C |
| `UUID_D`  | UUID of machine D |

### Step 2 — generate PSKs (one per link)

Each machine-to-machine link needs a unique PSK.  Generate four:

```sh
openssl rand -hex 16   # PSK_AB  — shared by A and B
openssl rand -hex 16   # PSK_BC  — shared by B and C
openssl rand -hex 16   # PSK_CD  — shared by C and D
openssl rand -hex 16   # PSK_DA  — shared by D and A
```

### Step 3 — load the module on all four machines

Each machine lists its two direct neighbours in `peer_auth_acl`.

**Machine A** (peers: B and D):

```sh
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  lanes=auto \
  native_fragment_striping=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  peer_auth_acl=${UUID_B}=${PSK_AB},${UUID_D}=${PSK_DA}
```

**Machine B** (peers: A and C):

```sh
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  lanes=auto \
  native_fragment_striping=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  peer_auth_acl=${UUID_A}=${PSK_AB},${UUID_C}=${PSK_BC}
```

**Machine C** (peers: B and D):

```sh
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  lanes=auto \
  native_fragment_striping=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  peer_auth_acl=${UUID_B}=${PSK_BC},${UUID_D}=${PSK_CD}
```

**Machine D** (peers: C and A):

```sh
sudo modprobe thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  lanes=auto \
  native_fragment_striping=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  peer_auth_acl=${UUID_C}=${PSK_CD},${UUID_A}=${PSK_DA}
```

### Step 4 — verify the ring

On each machine you should see two RDMA devices and two `path ready` log lines:

```sh
# Check devices
ibv_devices
# usb4_rdma0  (direct neighbour 1)
# usb4_rdma1  (direct neighbour 2)

# Check kernel log
dmesg | grep "thunderbolt_ibverbs.*path ready"
# thunderbolt_ibverbs: peer <UUID-X>: native path ready, N lanes
# thunderbolt_ibverbs: peer <UUID-Y>: native path ready, N lanes

# Check live counters after a transfer
cat /sys/kernel/debug/thunderbolt_ibverbs/summary
```

### Step 5 — run a ring-wide bandwidth test

```sh
# Each machine, run server on both devices simultaneously:
ib_write_bw -d usb4_rdma0 &
ib_write_bw -d usb4_rdma1 &

# From the neighbour machines, connect to both servers:
ib_write_bw -d usb4_rdma0 <ip-of-neighbour-0>
ib_write_bw -d usb4_rdma1 <ip-of-neighbour-1>
```

### Step 6 — make the ring configuration persistent

Create `/etc/modprobe.d/thunderbolt-ibverbs.conf` on each machine with the
appropriate `peer_auth_acl` value for that node (both neighbours, each with
its link PSK).

Example for machine A:

```sh
cat > /etc/modprobe.d/thunderbolt-ibverbs.conf <<'EOF'
options thunderbolt_ibverbs \
  profile=linux_perf \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=1 \
  lanes=auto \
  native_fragment_striping=1 \
  zcopy_min_bytes=4096 \
  nhi_interrupt_throttle_ns=50000 \
  peer_auth_acl=<UUID_B>=<PSK_AB>,<UUID_D>=<PSK_DA>
EOF
```

---

## Lanes deep-dive

The `lanes=` parameter controls how many DMA ring pairs the driver requests
from the Thunderbolt subsystem for each link.

| Value       | Meaning |
|-------------|---------|
| `auto`      | Request as many lanes as the controller advertises (recommended) |
| `N`         | Request exactly N lanes; fails if the controller cannot grant them |
| `MIN-MAX`   | Accept any count in the range MIN…MAX |

Each lane is a pair of TX + RX DMA rings.  Thunderbolt 4 typically provides
two lanes per cable (2 × 20 Gbit/s = 40 Gbit/s raw), Thunderbolt 5 up to
four (4 × 20 Gbit/s = 80 Gbit/s raw).

With `native_fragment_striping=1` the driver round-robins 4 KiB DMA frames
across all active lanes on a single link, so a single large SEND can saturate
multiple lanes without needing multiple QPs.  See
[`docs/TUNING.md`](TUNING.md) for the measured gain on a four-rail link.

Verify the negotiated lane count after module load:

```sh
dmesg | grep "thunderbolt_ibverbs.*path ready"
# e.g.: "native path ready, 2 lanes"

# Or per-rail stats under debugfs:
ls /sys/kernel/debug/thunderbolt_ibverbs/<peer-uuid>/rail/
```

---

## Performance considerations

| Parameter                   | Two-machine dual-port | Four-machine ring |
|-----------------------------|----------------------|-------------------|
| `lanes`                     | `auto`               | `auto`            |
| `native_fragment_striping`  | `1` (≥ 4 rails)      | `1`               |
| `zcopy_min_bytes`           | `4096`               | `4096`            |
| `nhi_interrupt_throttle_ns` | `50000` (throughput) | `50000`           |

For latency-sensitive workloads (MPI all-reduce, tightly-coupled HPC) set
`nhi_interrupt_throttle_ns=0` on all nodes; the interrupt coalescing adds
~0.5 µs per hop.

If `native_fragment_striping` causes issues with SRQ-based workloads, turn it
off (`native_fragment_striping=0`) — see [`docs/TUNING.md`](TUNING.md).

---

## Troubleshooting multi-port setups

**Only one `usb4_rdma*` device appears instead of two.**
The second peer has not been enumerated yet.  Check:

```sh
ls /sys/bus/thunderbolt/devices/
```

Both cable domains should show entries (`0-1`, `1-1`, etc.).  If only one
cable domain appears, check seating of the second cable and whether both
Thunderbolt controllers are enabled in the BIOS/UEFI.

**`peer_auth_acl` rejects one of the two peers.**
Each remote UUID must appear in `peer_auth_acl` with the correct PSK.  A
common mistake when adding the second entry is transposing the PSK between
two entries.  List all entries with commas and no spaces.

**Ring node sees the correct two devices but NCCL fails to use both.**
Make sure you pass both device names to NCCL.  Set:

```sh
export NCCL_IB_HCA=usb4_rdma0,usb4_rdma1
```

or, for the ring topology on each node, list all four devices across all nodes
in the host-file or NCCL config so NCCL can discover the full topology.

**Debugfs shows counters only on one device.**
Run `ibv_devinfo` on both devices to confirm they are registered, then run
separate `ib_write_bw` pairs targeting each device.

For a complete symptom reference see [`docs/TROUBLESHOOTING.md`](TROUBLESHOOTING.md).

---

## See also

- [`docs/MODULE_PARAMETERS.md`](MODULE_PARAMETERS.md) — full parameter reference
- [`docs/TUNING.md`](TUNING.md) — `nhi_interrupt_throttle_ns`, `native_fragment_striping`, `zcopy_min_bytes` sweep results
- [`docs/SECURITY.md`](SECURITY.md) — threat model and `peer_auth_acl` details
- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) — per-rail QP mapping and data path
- [`docs/TROUBLESHOOTING.md`](TROUBLESHOOTING.md) — symptom-by-symptom checklist
- [`README.md`](../README.md) — installation, quick-start, and useful parameters
