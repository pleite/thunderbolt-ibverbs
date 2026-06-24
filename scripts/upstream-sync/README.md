# Upstream-sync automation scripts

These scripts launch one branch + issue + draft PR session per selected upstream
feature backport, then hand each PR to `@copilot` for implementation.

Unlike the archived roadmap/fixes toolkits, this set is **active** and targets
post-`v0.3.4` upstream merges that are not yet in this fork.

## What each script does

For step `NN`, `NN-*.sh` idempotently:

1. Ensures tracking issue exists.
2. Creates/reuses branch `upstream-sync/NN-<slug>`.
3. Seeds the branch with `docs/upstream-sync/steps/NN-<slug>.md`.
4. Opens/reuses a draft PR against `BASE_BRANCH`.
5. Tags `@copilot` on the PR with implementation handoff text.

## Base branch behavior

- If `BASE_BRANCH` is unset, scripts first try the **current local branch** (if
  it exists on the remote).
- If that branch is unavailable, scripts fall back to the repository default
  branch.

This keeps `main` clean when you run the toolkit from a dedicated sync branch.

## Usage

```sh
cd scripts/upstream-sync

# create labels once
./00-setup-labels.sh

# launch every upstream-sync session
./run-all.sh

# launch a subset
./run-all.sh 01 03

# preview only
DRY_RUN=1 ./run-all.sh
```

## Current steps

| Order | Script | Upstream PR | Focus |
|---|---|---|---|
| 00 | `00-setup-labels.sh` | — | Create labels |
| 01 | `01-apple-send-serialization.sh` | #44 | Apple SEND correctness |
| 02 | `02-apple-service-publication-retry.sh` | #46 | Apple service reliability |
| 03 | `03-rawhide-ib-umem-get-va.sh` | #49 | Kernel API compatibility |
| 04 | `04-linux71-xdomain-patch-refresh.sh` | #50 | kernel-workflow patch-stack compatibility |
| — | `run-all.sh` | — | Run labels + selected/all steps |

## Configuration

| Variable | Default | Purpose |
|---|---|---|
| `REPO` | auto-detected `owner/repo` | target repository |
| `BASE_BRANCH` | current local branch (if remote) else repo default | PR base |
| `BRANCH_PREFIX` | `upstream-sync` | branch name prefix |
| `COPILOT_HANDLE` | `@copilot` | coding-agent mention |
| `COPILOT_ASSIGNEE` | _(unset)_ | optional assignee login |
| `DRY_RUN` | _(unset)_ | preview mode |
