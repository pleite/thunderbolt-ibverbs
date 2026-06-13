# Add exponential backoff + jitter to reliability retries (R6)

**Driver fix R6.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
`tbv_rel_retry_interval` ignores the retry budget and returns a fixed interval
(`proto/reliability.c:216-219`), so under congestion all senders retry in
lockstep — a synchronized-retry collapse.

### Scope
- Compute the retry interval with exponential backoff and jitter.
- Keep it deterministic enough to test.

### Acceptance criteria
- [ ] Retry interval grows with successive retries (with jitter).
- [ ] A test asserts the backoff schedule.

### Labels
`reliability`, `performance`
