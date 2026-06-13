# Root-gate debugfs/configfs surfaces (S4)

**Driver fix S4.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
The debug surfaces (`kernel/debugfs.c`) expose QPNs, link IDs, route and proxy-IP
state to unprivileged users, and the configfs link model (`kernel/configfs.c`) is
broadly accessible — useful information for an attacker.

### Scope
- Restrict debugfs/configfs entries to privileged users (or a build/runtime
  "production" mode).
- Reduce sensitive fields in the `summary` output.

### Acceptance criteria
- [ ] Sensitive debug/config surfaces are not readable/writable unprivileged.
- [ ] Production builds can disable them entirely.

### Labels
`security`, `kernel`
