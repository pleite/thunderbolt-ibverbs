# Enforce resource limits and per-peer rate limiting (R5)

**Driver fix R5.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The advertised `max_qp`/`max_cq`/`max_mr` limits are not enforced at create time,
there is no per-peer rate limiting or credit reservation, and Apple pending-RX
bytes are bounded per QP (`kernel/ibdev.c:5606-5616`) but not globally per
device. A peer can exhaust host resources.

### Scope
- Enforce advertised object limits at create time.
- Cap Apple pending-RX bytes per device (not just per QP).
- Add per-peer rate limiting / credit reservation.

### Acceptance criteria
- [ ] Exceeding an advertised limit fails the create call cleanly.
- [ ] Per-device memory pressure from one peer is bounded.

### Labels
`reliability`, `kernel`
