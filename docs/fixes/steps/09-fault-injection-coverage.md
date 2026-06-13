# Add fault-injection coverage for the reliability engine (T1)

**Driver fix T1 (fault injection).** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The reliability engine (`proto/reliability.c`) has no automated fault-injection
coverage. This matches the upstream PR #22 "still pending" list: lost
ACK/credit/data frame, duplicate retry, RNR exhaustion, READ-response retry, and
teardown-while-in-flight are untested.

### Scope
- Drive `proto/reliability.c` with injected faults for each scenario above.
- Run the suite in CI; it exercises the protocol, no hardware required.

### Acceptance criteria
- [ ] Tests for lost ACK/credit/data, duplicate retry, RNR exhaustion,
      READ-response retry, and teardown-while-in-flight.
- [ ] They run in CI and gate merges.

### Labels
`testing`, `ci`
