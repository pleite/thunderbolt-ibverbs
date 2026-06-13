# Roadmap automation scripts (archived)

> **Archived — all eight roadmap steps shipped.** Steps 1–8 were filed and merged
> via PRs #3, #5, #7, #9, #11, #13, #15, and #17, so this toolkit is kept here for
> reference only. The follow-on work driven by the driver analysis used the
> [`scripts/archive/fixes/`](../fixes/) toolkit (now archived too); see
> [`docs/FINDINGS.md`](../../../docs/FINDINGS.md).
>
> Paths in the rest of this file are written relative to the original
> `scripts/roadmap/` location.

These scripts turn each step of [`docs/ROADMAP.md`](../../docs/ROADMAP.md) into a
**separate GitHub issue + branch + draft PR**, and hand the implementation off to
the GitHub Copilot coding agent by `@copilot`-mentioning it on the PR.

You run them; they call the GitHub API on your behalf. Nothing here pushes from
CI — they are meant to be run locally (or anywhere you have `gh` authenticated).

## What each step script does

For roadmap step *N*, the script `0N-<slug>.sh` will, **idempotently**:

1. **Issue** — create a tracking issue titled like the roadmap step, with the
   step's motivation / scope / acceptance criteria and the suggested labels
   (reusing an existing issue of the same title if present).
2. **Branch** — create `roadmap/0N-<slug>` from the default branch, seeded with
   the step spec at `docs/roadmap/steps/0N-<slug>.md` so the branch has a real
   diff and the agent has the spec to start from.
3. **Draft PR** — open a draft PR from that branch with a body that points to
   exactly what the step solves and `Closes #<issue>`.
4. **Hand-off** — comment `@copilot please implement this step` on the PR so the
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
cd scripts/roadmap

# 1. create the labels the steps use (once)
./00-setup-labels.sh

# 2a. do everything, in order
./run-all.sh

# 2b. ...or run individual steps
./01-security-threat-model.sh
./02-native-transport-hardening.sh
# ...
./08-contributor-docs.sh

# 2c. ...or a subset
./run-all.sh 01 03 05
```

### Preview first (recommended)

Every script honours `DRY_RUN`, which prints the actions without touching
GitHub or git:

```sh
DRY_RUN=1 ./run-all.sh
```

## Files and order

| Order | Script                              | Roadmap step                              | Labels |
|-------|-------------------------------------|-------------------------------------------|--------|
| 00    | `00-setup-labels.sh`                | create all labels used below              | —      |
| 01    | `01-security-threat-model.sh`       | Security threat model and hardening       | `security`, `kernel`, `epic` |
| 02    | `02-native-transport-hardening.sh`  | Native Linux-to-Linux transport hardening | `kernel`, `reliability`, `epic` |
| 03    | `03-apple-transport-stabilization.sh`| Apple-compatible transport stabilization | `kernel`, `apple`, `epic` |
| 04    | `04-kernel-compatibility.sh`        | Broaden kernel version compatibility      | `kernel`, `compatibility` |
| 05    | `05-performance-tuning.sh`          | Performance tuning and tunable defaults   | `performance`, `benchmarks` |
| 06    | `06-automated-testing.sh`           | Automated end-to-end and regression testing | `testing`, `ci` |
| 07    | `07-packaging-release.sh`           | Packaging and release automation          | `packaging`, `ci`, `release` |
| 08    | `08-contributor-docs.sh`            | User and contributor documentation        | `documentation` |
| —     | `run-all.sh`                        | run labels + every step in order          | —      |
| —     | `lib/common.sh`                     | shared `gh`/`git` helpers (sourced)       | —      |

> Roadmap step 0 (establishing the roadmap) is intentionally not scripted — it is
> the work that produced `docs/ROADMAP.md` and these scripts.

## Configuration

Override any of these via environment variables:

| Variable           | Default                       | Purpose |
|--------------------|-------------------------------|---------|
| `REPO`             | auto-detected `owner/repo`    | target repository |
| `BASE_BRANCH`      | repo default branch           | branch/PR base |
| `BRANCH_PREFIX`    | `roadmap`                     | branch name prefix |
| `COPILOT_HANDLE`   | `@copilot`                    | mention used to trigger the agent |
| `COPILOT_ASSIGNEE` | _(unset)_                     | optional login to also assign (best-effort) |
| `DRY_RUN`          | _(unset)_                     | when set, preview without changes |

## Notes

- The scripts never force-push or delete anything; they only create issues,
  branches, PRs, and comments.
- If your account/repo can assign issues directly to the Copilot agent, set
  `COPILOT_ASSIGNEE` to its login; otherwise the `@copilot` PR comment is the
  portable trigger.
