# Native Linux-to-Linux transport hardening

**Roadmap step 2.** See [docs/ROADMAP.md](../blob/HEAD/docs/ROADMAP.md).

### Motivation
The native Linux transport is the main path and most of the benchmark story
depends on it; bugs here block everyone.

### Scope
- Harden ring setup/teardown, error and disconnect handling, and the verb
  completion paths.
- Add stress and fault-injection coverage.
- Reduce known buggy edge cases under cable pulls and module reload.

### Acceptance criteria
- [ ] Repeated connect/disconnect and module reload cycles pass without leaks
      or oopses.
- [ ] perftest verbs (read/write/send) run clean in CI or a documented manual
      matrix.

### Labels
`kernel`, `reliability`, `epic`
