# Bound the proto/ dedup/ACK window to outstanding WRs (R1)

**Driver fix R1.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The freestanding `proto/` reliability path keeps a 16-entry ACK/dedup window
(`proto/reliability.h:23`) scanned linearly (`proto/reliability.c:52-54`), while
the kernel allows up to `TBV_IBDEV_MAX_QP_WR` (1024) outstanding WRs. Under heavy
retransmission the window can wrap and accept duplicates as new. The kernel side
was already widened (`kernel/ibdev.c:70`); the protocol side still needs it.

### Scope
- Size the `proto/` dedup window to the outstanding-WR limit, or switch to a
  PSN bitmap/`xarray`-style tracker instead of a linear scan.
- Keep the freestanding code dependency-free (no kernel/libc-only helpers).

### Acceptance criteria
- [ ] Duplicate frames are rejected across the full outstanding-WR range.
- [ ] A fault-injection test covers duplicate-frame acceptance.

### Labels
`reliability`, `kernel`
