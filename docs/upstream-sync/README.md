# Upstream sync execution plan (post-v0.3.4)

This plan tracks newly merged upstream PRs after the baseline captured in
`docs/UPSTREAM_SYNC.md` and defines execution sessions launched by
`scripts/upstream-sync/`.

## Newly merged upstream PRs to integrate

| Priority | Upstream PR | Merge date | Keep in fork? | Reason |
|---|---|---|---|---|
| P0 | #44 serialize all UC SENDs per QP | 2026-06-21 | Yes | Apple correctness fix for short SEND corruption/timeouts |
| P1 | #46 retry deferred service publication | 2026-06-21 | Yes | Prevent stranded Apple rail publication after transient errors |
| P1 | #49 handle Rawhide `ib_umem_get_va` API | 2026-06-24 | Yes | Keep Rawhide / Linux >= 7.2 compatibility |
| P2 | #50 refresh xdomain patch for Linux 7.1 | 2026-06-24 | Yes (if kernel-workflow stack is used) | Keeps kernel-workflow patch stack applying cleanly |

## Conflict and refactor impact in this fork

- **#44:** Medium-high conflict in `kernel/ibdev.c` Apple TX admission area.
  This fork still has the pre-#42 duplicated predicates, so integrate #44 with a
  local refactor step while preserving R2/R3 behavior.
- **#46:** Low-medium conflict in `kernel/service.c`/`kernel/tbv.h`; mostly
  additive lifecycle changes (delayed work + retries + READ/WRITE_ONCE).
- **#49:** Low conflict but **requires patch relocation** from upstream
  `kernel/ibdev.c` to fork `kernel/ibdev_mr.c` due R7 split.
- **#50:** Low conflict in patch-stack files (`kernel-workflow/patches/*`);
  no interaction with downstream MR/GPU-direct work.

## Session launcher

Use `scripts/upstream-sync/run-all.sh` to launch one PR session per feature. The
script set follows the same idempotent issue/branch/draft-PR pattern used by
previous roadmap/fixes toolkits, but defaults to the current branch as base so
`main` can stay clean.

```sh
cd scripts/upstream-sync
./00-setup-labels.sh
./run-all.sh
```
