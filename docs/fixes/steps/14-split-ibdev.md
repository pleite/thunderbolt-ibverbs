# Split ibdev.c into reviewable modules (R7)

**Driver fix R7.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
`kernel/ibdev.c` is a ~9.8k-line file mixing native transport, Apple transport,
CQ, MR, and the QP state machine. Its size makes the security-critical paths hard
to review, which compounds every other finding here.

### Scope
- Split `ibdev.c` into focused translation units (native transport / Apple
  transport / CQ / MR / QP state machine) with no behaviour change.
- Land it after the P0/P1 behavioural fixes so tests pin behaviour first.

### Acceptance criteria
- [ ] `ibdev.c` is split into smaller modules.
- [ ] No functional change; existing tests still pass.

### Labels
`kernel`, `refactor`
