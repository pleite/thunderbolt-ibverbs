# Module parameter reference

`thunderbolt_ibverbs` parameters are defined in `kernel/main.c`,
`kernel/path.c`, `kernel/ibdev.c`, and `kernel/peer.c`.

## Core profile and bring-up (`kernel/main.c`)

| Parameter | Type | Default | Access | Description |
| --- | --- | --- | --- | --- |
| `compat` | string | `auto` | `0444` | Compatibility mode: `auto`, `force`, `off`. |
| `profile` | string | `auto` | `0444` | Profile: `auto`, `mac_compat`, `linux_perf`, `mixed`. |
| `tbnet` | string | `auto` | `0444` | Thunderbolt-net coexistence policy. |
| `tbnet_identity` | string | `auto` | `0444` | Apple TBnet identity mode. |
| `tbnet_identity_tbnet` | string | `thunderbolt0` | `0444` | Netdev carrying stock ThunderboltIP for `stock_proxy`. |
| `tbnet_identity_gid` | string | `auto` | `0444` | Netdev whose IPv4 is proxied as RDMA GID for `stock_proxy`. |
| `tbnet_identity_minimal_e2e` | bool | `0` | `0444` | Enable E2E flow control for minimal packet identity rings. |
| `tbnet_identity_minimal_apple_only` | bool | `1` | `0444` | Restrict minimal identity to Apple peers. |
| `lanes` | string | `auto` | `0444` | Lane request: `auto`, `N`, or `MIN-MAX`. |
| `bind_services` | bool | `0` | `0444` | Register Thunderbolt service drivers and advertise services. |
| `native_prtcstns` | uint | `0` | `0444` | Protocol settings advertised by native Linux service. |
| `apple_prtcstns` | uint | `0` | `0444` | Protocol settings advertised by Apple AD/FA57 service. |
| `allocate_rings` | bool | `0` | `0444` | Allocate rings on probe without enabling paths. |
| `start_rings` | bool | `0` | `0444` | Start allocated rings without enabling paths. |
| `negotiate_native` | bool | `0` | `0444` | Send native HELLO after ring start without enabling paths. |
| `enable_tunnels` | bool | `0` | `0444` | Enable negotiated Thunderbolt paths after native HELLO. |
| `native_data` | bool | `1` | `0444` | Allow native peers to allocate rings and enable data paths. |
| `apple_data` | int | `-1` (`auto`) | `0444` | Apple-compatible data paths: `-1=auto`, `0=off`, `1=on`. |
| `native_fragment_striping` | bool | `0` | `0444` | Stripe native SEND fragments across active rails. |
| `register_verbs` | bool | `0` | `0444` | Register guarded libibverbs device skeleton. |
| `peer_allowlist` | string | empty | `0444` | Optional comma-separated remote UUID allow-list. |
| `gpu_direct` | string | `auto` | `0444` | GPU-direct dma-buf memory regions: `auto`, `on`, `off`. Only present when the module is built with `tbv_gpu_direct=1` (`CONFIG_TBV_GPU_DIRECT`); otherwise `ibv_reg_dmabuf_mr()` returns `EOPNOTSUPP`. |

## Transport and ring tuning (`kernel/path.c`)

| Parameter | Type | Default | Access | Description |
| --- | --- | --- | --- | --- |
| `nhi_interrupt_throttle_ns` | uint | `0` | `0644` | NHI interrupt throttling interval in ns (`0` disables). |
| `apple_tx_raw_mode` | bool | `0` | `0644` | Use RAW descriptors for Apple TX rings. |
| `apple_tx_e2e` | bool | `0` | `0644` | Enable E2E flow control on Apple TX rings. |
| `apple_rx_raw_mode` | bool | `0` | `0644` | Use RAW descriptors for Apple RX rings. |
| `native_tx_max_inflight` | uint | `32` | `0644` | Max native TX descriptors per path before completion wait (`0` disables throttling). |

## Verbs behavior and buffering (`kernel/ibdev.c`)

| Parameter | Type | Default | Access | Description |
| --- | --- | --- | --- | --- |
| `roce_netdev` | string | empty | `0444` | Netdev used for RoCE GID metadata. |
| `zcopy_min_bytes` | uint | `4096` | `0644` | Minimum bytes before native raw zero-copy streaming is requested. |
| `qp_timeout_ms` | uint | `5000` | `0644` | Fallback timeout in ms for pending WRs/partial receives (`0` disables). |
| `apple_tx_max_inflight_wr` | uint | `1` | `0644` | Max Apple UC SEND WRs in flight per QP. |
| `apple_tx_max_inflight_frames` | uint | `64` | `0644` | Max Apple 4 KiB frames posted per SEND group (`0` disables window). |
| `apple_tx_completion_delay_us` | uint | `0` | `0644` | Delay successful Apple UC SEND completion by microseconds. |
| `apple_rx_pending_bytes` | uint | `16MiB` | `0644` | Max bytes buffered per early Apple UC receive without a posted WQE. |
| `apple_rx_pending_slots` | uint | `4096` | `0644` | Max number of early Apple UC receives buffered per QP. |
| `apple_rx_pending_total_bytes` | uint | `64MiB` | `0644` | Max aggregate bytes buffered across all early Apple UC receives per QP. |
| `apple_rx_trace` | uint | `0` | `0644` | Print first N Apple RX callbacks with assembly state. |

## Peer policy (`kernel/peer.c`)

| Parameter | Type | Default | Access | Description |
| --- | --- | --- | --- | --- |
| `native_e2e` | int | `-1` (`auto`) | `0444` | Native Linux data-ring E2E mode: `-1 auto`, `0 off`, `1 on`. |

## Introspection

Use `modinfo thunderbolt_ibverbs` or inspect
`/sys/module/thunderbolt_ibverbs/parameters/` on a loaded module to confirm
runtime values.
