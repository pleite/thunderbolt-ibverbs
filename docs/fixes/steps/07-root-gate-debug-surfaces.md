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
- [x] Sensitive debug/config surfaces are not readable/writable unprivileged.
- [x] Production builds can disable them entirely.

### Implementation notes
- debugfs entries are now root-readable only (`0400`) and the summary output no
  longer emits proxy-IP or identity netdev names.
- configfs link attributes are now root-only (`0600`/`0400`).
- `production_mode=1` (read-only module param) disables both surfaces at
  runtime when set at module load.
- `tbv_debug_surfaces=0` (or
  `CONFIG_THUNDERBOLT_IBVERBS_DEBUG_SURFACES=n` for Kconfig builds) compiles
  both surfaces out.

### Labels
`security`, `kernel`
