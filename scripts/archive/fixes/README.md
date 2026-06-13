# Fix automation scripts (archived)

> **Archived — every analysis finding shipped.** Fixes 01–14 were filed and
> merged via PRs #19, #21, #23, #25, #27, #29, #31, #33, #35, #37, #39, #41,
> #43, and #45 (finding S1 shipped earlier in PR #1), so this toolkit is kept
> here for reference only. The findings it drove are all marked `fixed` in
> [`docs/FINDINGS.md`](../../../docs/FINDINGS.md).
>
> Paths in the rest of this file are written relative to the original
> `scripts/fixes/` location.

These scripts turn each **open finding** from the driver performance/security
analysis in [`docs/FINDINGS.md`](../../docs/FINDINGS.md) into a **separate GitHub
issue + branch + draft PR**, and hand the implementation off to the GitHub
Copilot coding agent by `@copilot`-mentioning it on the PR.

They are the successor to the earlier roadmap toolkit
([`scripts/archive/roadmap/`](../roadmap/)), whose eight steps all
shipped. The mechanics are identical — only the source of work changed (analysis
findings instead of roadmap steps).

You run them; they call the GitHub API on your behalf. Nothing here pushes from
CI — they are meant to be run locally (or anywhere you have `gh` authenticated).

## What each fix script does

For finding *NN*, the script `NN-<slug>.sh` will, **idempotently**:

1. **Issue** — create a tracking issue titled like the fix, with its motivation /
   scope / acceptance criteria and the suggested labels (reusing an existing
   issue of the same title if present).
2. **Branch** — create `fix/NN-<slug>` from the default branch, seeded with the
   fix spec at `docs/fixes/steps/NN-<slug>.md` so the branch has a real diff and
   the agent has the spec to start from.
3. **Draft PR** — open a draft PR from that branch whose body points to exactly
   what the fix solves and `Closes #<issue>`.
4. **Hand-off** — comment `@copilot please implement this fix` on the PR so the
   coding agent picks it up.

Re-running a script reuses whatever already exists, so it is safe to run twice.

## Prerequisites

- [`gh`](https://cli.github.com/) authenticated with write access:
  `gh auth login`
- `git`
- Run from inside a clone of this repo (the scripts auto-detect `owner/repo` and
  the default branch), or set `REPO=owner/name`.

## Usage

```sh
cd scripts/fixes

# 1. create the labels the fixes use (once)
./00-setup-labels.sh

# 2a. do everything, in order
./run-all.sh

# 2b. ...or run individual fixes
./01-reliability-dedup-window.sh
./04-peer-authentication.sh

# 2c. ...or a subset
./run-all.sh 01 02 03
```

### Preview first (recommended)

Every script honours `DRY_RUN`, which prints the actions without touching GitHub
or git:

```sh
DRY_RUN=1 ./run-all.sh
```

## Files and order

The numbering follows the prioritized fix list in `docs/FINDINGS.md` (P0→P3).
Finding **S1** (predictable rkey/lkey) is intentionally not scripted — it already
shipped on the `analyze-driver-performance-security` branch (PR #1).

| Order | Script                              | Findings | Labels |
|-------|-------------------------------------|----------|--------|
| 00    | `00-setup-labels.sh`                | create all labels used below | — |
| 01    | `01-reliability-dedup-window.sh`    | R1 | `reliability`, `kernel` |
| 02    | `02-qp-teardown-bounded.sh`         | R2 | `reliability`, `kernel` |
| 03    | `03-lock-ordering.sh`               | R3 | `reliability`, `kernel` |
| 04    | `04-peer-authentication.sh`         | S2, S3 | `security`, `kernel`, `epic` |
| 05    | `05-graceful-hot-unplug.sh`         | R4 | `reliability`, `kernel` |
| 06    | `06-resource-limits.sh`             | R5 | `reliability`, `kernel` |
| 07    | `07-root-gate-debug-surfaces.sh`    | S4 | `security`, `kernel` |
| 08    | `08-datapath-functional-test.sh`    | T1, T3 | `testing`, `ci` |
| 09    | `09-fault-injection-coverage.sh`    | T1 | `testing`, `ci` |
| 10    | `10-kernel-static-analysis.sh`      | T2 | `testing`, `ci` |
| 11    | `11-perf-regression-gating.sh`      | T1 | `performance`, `ci` |
| 12    | `12-reliability-backoff.sh`         | R6 | `reliability`, `performance` |
| 13    | `13-project-docs.sh`                | T4 | `documentation` |
| 14    | `14-split-ibdev.sh`                 | R7 | `kernel`, `refactor` |
| —     | `run-all.sh`                        | run labels + every fix in order | — |
| —     | `lib/common.sh`                     | shared `gh`/`git` helpers (sourced) | — |

## Configuration

Override any of these via environment variables:

| Variable           | Default                       | Purpose |
|--------------------|-------------------------------|---------|
| `REPO`             | auto-detected `owner/repo`    | target repository |
| `BASE_BRANCH`      | repo default branch           | branch/PR base |
| `BRANCH_PREFIX`    | `fix`                         | branch name prefix |
| `COPILOT_HANDLE`   | `@copilot`                    | mention used to trigger the agent |
| `COPILOT_ASSIGNEE` | _(unset)_                     | optional login to also assign (best-effort) |
| `DRY_RUN`          | _(unset)_                     | when set, preview without changes |

## Notes

- The scripts never force-push or delete anything; they only create issues,
  branches, PRs, and comments.
- If your account/repo can assign issues directly to the Copilot agent, set
  `COPILOT_ASSIGNEE` to its login; otherwise the `@copilot` PR comment is the
  portable trigger.
