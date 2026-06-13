# Security model (roadmap step 1)

Thunderbolt/USB4 exposes DMA-capable links. This driver treats a connected peer
as potentially malicious unless explicitly trusted.

## Trust boundary

- **Trusted local kernel + local userspace process owning the PD/MRs**
- **Untrusted remote host on the cable**

The remote side can send transport frames, replay stale frames, and try random
RKEY/LKEY values. It must not gain arbitrary host-memory read/write.

## Threats considered

- Remote write/read outside the registered MR window.
- Cross-peer key reuse (peer A trying to use peer B's keys).
- Key guessing by predictable key allocation.
- Unexpected peer attachment when operating in a restricted topology.

## Hardening in this step

### 1) Memory registration / key handling

- MR publication now uses non-sequential keys (mixed with kernel RNG) instead of
  purely monotonic keys.
- Each MR is tagged with its owning `peer_id`.
- MR lookup for send/recv/read/write paths is peer-scoped, so keys are only
  valid for the peer that created the MR.
- `reg_user_mr` rejects address-range overflows up front.

### 2) Remote memory-access bounds

- RDMA write fragment headers are validated before touching memory:
  - offset+length overflow checks
  - max message-size checks
  - LAST-fragment consistency checks
  - remote-write permission checks
- Read/write data copy paths continue to validate IOVA bounds against the MR.

### 3) Optional peer allow-listing

- New module parameter: `peer_allowlist=<uuid[,uuid...]>`
- When configured, only peers whose Thunderbolt `remote_uuid` is in the
  allow-list are admitted; others are rejected with `-EACCES`.
- If allow-listing is enabled but the peer UUID is unavailable, admission fails
  closed.

## Tests added for high-risk paths

- KUnit tests for:
  - RDMA write header validation edge cases.
  - MR peer-scope matching behavior.

These tests live in `kernel/ibdev.c` under `CONFIG_KUNIT`.
