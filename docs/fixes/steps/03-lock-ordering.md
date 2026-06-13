# Document and enforce a single lock ordering (R3)

**Driver fix R3.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
Some paths take `peer->control_lock` then a state lock (`kernel/ibdev.c:2783`),
while others take only an owner lock — an inconsistent hierarchy that risks
deadlock and is hard to review.

### Scope
- Define one lock order (e.g. `peer->control_lock` → `owner->lock` → `tqp->lock`).
- Annotate it (lockdep / comments) and fix paths that violate it.

### Acceptance criteria
- [ ] The lock order is documented in the source.
- [ ] All offending paths follow it; lockdep is clean under stress.

### Labels
`reliability`, `kernel`
