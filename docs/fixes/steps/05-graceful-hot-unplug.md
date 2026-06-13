# Flush in-flight WRs on hot-unplug instead of silent drops (R4)

**Driver fix R4.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
`rail->removing` is checked before queueing (`kernel/ibdev.c:1795`, `2102-2125`),
but WRs queued just afterward are dropped with no flush/error completion, and the
Apple tunnel teardown is warn-only. Callers can hang waiting for completions that
never arrive.

### Scope
- On `rail->removing`, flush outstanding WRs to flush/error completions.
- Convert Apple tunnel warn-only teardown into deterministic teardown.

### Acceptance criteria
- [ ] Every posted WR gets a completion (success or flush/error) on unplug.
- [ ] A hot-unplug-with-in-flight-traffic test passes.

### Labels
`reliability`, `kernel`
