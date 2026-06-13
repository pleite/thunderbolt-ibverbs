# Add kernel static analysis and memory-safety to CI (T2)

**Driver fix T2.** See [docs/FINDINGS.md](../blob/HEAD/docs/FINDINGS.md).

### Motivation
There is no checkpatch/sparse/smatch/clang-analyzer, no KASAN/KCSAN build, and
no fuzzing of the `proto/` wire parsers; smoke builds are not `-Werror`. Memory
bugs in a ~20k-LOC kernel module go uncaught.

### Scope
- Add `checkpatch.pl` and `sparse` (optionally `smatch`) CI jobs.
- Add a KASAN/KCSAN build target.
- Enable `-Werror` on the smoke builds; consider fuzzing `proto/` parsers.

### Acceptance criteria
- [ ] Static-analysis + KASAN/KCSAN jobs run in CI.
- [ ] Smoke builds are `-Werror`.

### Labels
`testing`, `ci`
