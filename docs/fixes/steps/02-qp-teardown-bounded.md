# Make QP teardown bounded and race-free (R2)

**Driver fix R2.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
QP destroy ends in an untimed `wait_for_completion(&tqp->refs_zero)`
(`kernel/ibdev.c:2718`); a late RDMA-READ response in flight can hang teardown
indefinitely, and there is a window between marking the QP closing and disarming
its timeout work.

### Scope
- Put a bound on the final wait (timeout + diagnostic on expiry).
- Fence the `closing`/timeout-disarm window so no work re-arms after close.
- Drain in-flight READ responses deterministically before completing destroy.

### Acceptance criteria
- [ ] QP destroy cannot block indefinitely.
- [ ] A teardown-while-in-flight test exercises the drained path.

### Labels
`reliability`, `kernel`
