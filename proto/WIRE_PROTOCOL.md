# Wire protocol reference

This directory contains the protocol contracts shared by kernel and userspace.

## Native control handshake (`native_wire.h`)

- Magic/version: `TBV_NATIVE_WIRE_MAGIC` (`"TBV1"`) and version `1`.
- Message envelope: Thunderbolt xdomain header + TBV wire header + payload.
- Control ops:
  - `HELLO`
  - `HELLO_ACK`
  - `READY`
  - `READY_ACK`
- Capabilities bitmask:
  - `TBV_NATIVE_WIRE_CAP_UC`
  - `TBV_NATIVE_WIRE_CAP_RC`
  - `TBV_NATIVE_WIRE_CAP_MULTI_RAIL`
- Path flags:
  - `TBV_NATIVE_WIRE_PATH_FRAME`
  - `TBV_NATIVE_WIRE_PATH_E2E`

The hello payload advertises capabilities, rail identity, route/hops, selected
path, ring sizes, and path flags.

## Native data frames (`native_data.h`)

- Frame size: 4096 bytes.
- Header size: 48 bytes.
- Header magic/version: `TBV_NATIVE_DATA_MAGIC` (`"TVD1"`) and version `1`.
- Max operation size: 16 MiB.

Supported opcodes include SEND/SEND_IMM, RDMA WRITE/WRITE_IMM, RDMA READ
request/response/ack, credit control, and MAD transport.

Header fields encode QP endpoints, PSN, per-fragment length and offset, remote
address/rkey, and immediate/ack status data.

## Identity and compatibility helpers

- `identity.h` defines address/GID structures and app-selection helpers.
- `tbnet.h` defines Thunderbolt-net protocol-settings capability bits used by
  minimal packet identity negotiation.

## Compatibility contract

Any wire-format change must update the constants in these headers and preserve
backward compatibility expectations documented by the handshake version fields.
