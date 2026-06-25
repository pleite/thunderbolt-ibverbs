# Integration Checklist for Upstream Changes

Use this checklist when verifying and integrating upstream changes from `hellas-ai/thunderbolt-ibverbs` into this fork.

## Pre-Integration Analysis

### [ ] 1. Upstream PR Identification
- [ ] Source PR identified in `hellas-ai/thunderbolt-ibverbs`
- [ ] PR number recorded (e.g., #44, #46, #49, #50)
- [ ] PR URL: `https://github.com/hellas-ai/thunderbolt-ibverbs/pull/NNN`
- [ ] PR status: Merged or ready for integration
- [ ] Commit hash(es) recorded for backport script

### [ ] 2. PR Analysis & Scope Documentation
- [ ] PR title and description reviewed
- [ ] Motivation/problem statement understood
- [ ] All commits in the PR examined
- [ ] Files modified documented:
  - Kernel files affected: `kernel/ibdev.c`, `kernel/ibdev_mr.c`, `kernel/mac_compat.c`, etc.
  - Userspace files affected: `proto/`, `userspace/`, etc.
  - Patch files affected: `kernel-workflow/patches/`, etc.
- [ ] Dependencies identified (other PRs, kernel versions, etc.)
- [ ] Backwards compatibility implications noted

### [ ] 3. Fork-Specific Conflict Detection
- [ ] **R7 Split Check**: Does PR touch `ibdev` code? 
  - [ ] If yes, verify changes apply to both `kernel/ibdev.c` and `kernel/ibdev_mr.c`
  - [ ] Watch for duplicated logic that should be deduplicated
- [ ] **R4 Hot-Unplug Check**: Does PR touch service publication or resource cleanup?
  - [ ] If yes, verify R4 lifecycle semantics are preserved
  - [ ] Check device hot-unplug error handling paths
- [ ] **Apple-specific code**: Does PR touch Apple UC/service logic?
  - [ ] If yes, review for compatibility with existing Apple optimizations
- [ ] **Patch management**: Does PR affect `kernel-workflow/patches/`?
  - [ ] If yes, plan for `regen-upstream-thunderbolt-patches.sh` regeneration
- [ ] **RDMA-core**: Does PR affect userspace provider?
  - [ ] If yes, plan for `regen-rdma-core-patches.sh` regeneration

### [ ] 4. Adaptation Planning
- [ ] Fork-specific changes documented in detail:
  - What needs to be changed and why
  - Which files require modifications beyond the upstream patch
  - Risk assessment for each change
- [ ] Test plan identified:
  - Which distros to test on (Debian, Fedora, Arch, Ubuntu)
  - Which module features to exercise
  - Performance impact assessment needed?

---

## Backport Script Creation

### [ ] 5. Script Skeleton Created
- [ ] New script `scripts/upstream-sync/0N-*.sh` created (where N is the next step number)
- [ ] Script follows naming convention: `0N-<slug>.sh`
- [ ] Script includes shebang: `#!/usr/bin/env bash`
- [ ] Error handling: `set -euo pipefail` at top

### [ ] 6. Script Header Documentation
- [ ] Clear comment header describing what the script does
- [ ] Upstream PR reference documented (e.g., `# Backports hellas-ai/thunderbolt-ibverbs#44`)
- [ ] Upstream commit hash(es) included in comments
- [ ] Purpose and motivation documented

### [ ] 7. Script Variables Defined
- [ ] `STEP_NUM` set correctly (next sequential number)
- [ ] `STEP_SLUG` set to descriptive slug
- [ ] `STEP_TITLE` set to clear, user-facing title
- [ ] `STEP_LABELS` set with appropriate GitHub labels
  - Examples: `reliability,kernel` or `apple,compatibility`
- [ ] `STEP_ISSUE_BODY` filled with issue description
  - Includes: Problem statement, scope, acceptance criteria
  - References `docs/FINDINGS.md` or upstream PR as appropriate
- [ ] `STEP_PR_BODY` filled with PR description
  - Includes: What this solves, planned changes, acceptance criteria
- [ ] `STEP_HANDOFF` filled with GitHub Copilot implementation summary
  - Clear, actionable description of work to be done

### [ ] 8. Script Functions
- [ ] Script sources `lib/common.sh`: `source "lib/common.sh"`
- [ ] Script calls `run_upstream_step` at the end
- [ ] No additional logic needed (common.sh handles GitHub API/branch/PR automation)

---

## Dry Run Testing

### [ ] 9. Dry Run Executed
- [ ] Navigated to `scripts/upstream-sync/`
- [ ] Executed: `DRY_RUN=1 ./0N-*.sh`
- [ ] Output reviewed for correctness:
  - [ ] Issue body is clear and complete
  - [ ] Branch name is appropriate: `upstream-sync/0N-<slug>`
  - [ ] PR description is accurate and actionable
  - [ ] No obvious typos or formatting issues

### [ ] 10. Dry Run Output Verified
- [ ] Would-be issue title is descriptive
- [ ] Would-be issue body contains all necessary context
- [ ] Would-be branch base is correct (usually `main`)
- [ ] Would-be PR title matches issue title or is descriptive
- [ ] No error messages or warnings in dry-run output

---

## PR Creation & Handoff

### [ ] 11. Live Run Executed
- [ ] Executed: `./0N-*.sh` (without `DRY_RUN`)
- [ ] Script completed successfully
- [ ] GitHub issue created (or reused if idempotent run)
- [ ] Feature branch created: `upstream-sync/0N-<slug>`
- [ ] Draft PR opened with GitHub label(s) applied
- [ ] `@copilot` handoff comment posted on PR (if enabled in `lib/common.sh`)

### [ ] 12. GitHub Artifacts Verified
- [ ] Issue visible on GitHub with correct labels
- [ ] Branch exists and branches from `main`
- [ ] Draft PR visible, linked to issue
- [ ] PR description is readable and actionable
- [ ] `@copilot` mention visible in PR comments (if applicable)

---

## Implementation & Merge

### [ ] 13. Copilot Agent Completes Work
- [ ] Copilot agent session initiated via PR handoff comment
- [ ] Agent examined upstream PR and implemented backport
- [ ] Agent converted PR from draft to ready-for-review
- [ ] Commits present in PR branch

### [ ] 14. Code Review Completed
- [ ] Fork-specific adaptations verified (R7 split, R4 lifecycle, etc.)
- [ ] No unintended behavior changes
- [ ] Conflicts with existing code identified and resolved
- [ ] Code follows repository style and conventions
- [ ] Comments in code explain fork-specific changes

### [ ] 15. PR Merged
- [ ] All CI checks passed:
  - [ ] Module builds on Debian
  - [ ] Module builds on Fedora
  - [ ] Module builds on Arch
  - [ ] Module builds on Ubuntu
- [ ] Code review approved
- [ ] PR merged to `main` branch
- [ ] Feature branch deleted

---

## Post-Merge Validation

### [ ] 16. CI/Distro Tests Pass
- [ ] `tools/ci/distro-build.sh` passes on all distros
- [ ] `tools/ci/vm-smoke.sh` passes:
  - [ ] Debian
  - [ ] Fedora
  - [ ] Arch
  - [ ] Ubuntu
- [ ] `tools/ci/distro-package-rdma.sh` passes (if userspace affected)
- [ ] `tools/ci/datapath-functional.sh` passes (if protocol/reliability affected)

### [ ] 17. RDMA-Core Patches Regenerated (if needed)
- [ ] If userspace provider was modified:
  - [ ] `./packaging/regen-rdma-core-patches.sh` executed
  - [ ] `./packaging/test-rdma-patches.sh` passed on all distros
  - [ ] `packaging/rdma-core-patches/` files reviewed for correctness
  - [ ] Changes committed

### [ ] 18. Kernel Patches Regenerated (if needed)
- [ ] If kernel patches affected:
  - [ ] `./kernel-workflow/regen-upstream-thunderbolt-patches.sh` executed
  - [ ] New patches reviewed for correctness
  - [ ] Patch count matches expected
  - [ ] Changes committed

### [ ] 19. Regression Testing (if available)
- [ ] If live hardware available:
  - [ ] Hot-unplug test (if R4 affected)
  - [ ] Data path test (if kernel/protocol affected)
  - [ ] Apple-specific test (if Apple code affected)
- [ ] If live hardware unavailable:
  - [ ] CI tests serve as regression check
  - [ ] Manual review of changes sufficient

### [ ] 20. Integration Documentation Updated
- [ ] `scripts/archive/README_INTEGRATION_LOG.md` updated with:
  - [ ] Upstream PR number and commit hash
  - [ ] Backport script name and step number
  - [ ] Integration date
  - [ ] Any fork-specific adaptations made
  - [ ] Known issues or limitations
  - [ ] Relevant merged PR number

---

## Documentation & Archival

### [ ] 21. Integration Log Entry Created
- [ ] Added entry to `scripts/archive/README_INTEGRATION_LOG.md`:
  ```markdown
  - ✅ #NNN (0N-<slug>.sh) — Upstream commit: xxxxx — Merged PR #MMM on YYYY-MM-DD
  ```
- [ ] Notes on fork-specific changes included if substantial
- [ ] Cross-references to related issues/PRs included

### [ ] 22. Monitoring Alerts Updated
- [ ] If GitHub Actions monitoring exists:
  - [ ] Upstream PR marked as integrated
  - [ ] Alert/monitoring updated to reflect new baseline

---

## Sign-Off

### [ ] 23. Verification Complete
- [ ] All above checkboxes completed
- [ ] No open questions or concerns
- [ ] Integration ready for production

### [ ] 24. Documentation
- [ ] Date of integration: `_______________`
- [ ] Integrated by: `_______________`
- [ ] Verified by: `_______________`
- [ ] Notes/observations:
  ```
  ___________________________________________________________________
  ___________________________________________________________________
  ___________________________________________________________________
  ```

---

## Quick Reference

### Fork-Specific Adaptations
- **R7 Split**: `kernel/ibdev.c` → split into `kernel/ibdev.c` and `kernel/ibdev_mr.c`
- **R4 Lifecycle**: Hot-unplug handling during active operations must be preserved
- **Apple code**: Existing Apple optimizations must be compatible with backport
- **Patches**: Kernel patches are regenerated from upstream, not manually modified

### Key Scripts
- **Backport creation**: `scripts/upstream-sync/0N-*.sh`
- **Kernel patch regen**: `./kernel-workflow/regen-upstream-thunderbolt-patches.sh`
- **RDMA patch regen**: `./packaging/regen-rdma-core-patches.sh`
- **RDMA patch test**: `./packaging/test-rdma-patches.sh`

### Testing
- **Distro build**: `tools/ci/distro-build.sh debian|fedora|arch|ubuntu`
- **VM smoke**: `tools/ci/vm-smoke.sh debian|fedora|arch|ubuntu`
- **Datapath**: `tools/ci/datapath-functional.sh`

### Upstream Monitoring
- Manual check: `gh pr list --repo hellas-ai/thunderbolt-ibverbs --state open --sort updated`
- Check kernel tree: `git ls-remote git.kernel.org/pub/scm/linux/kernel/git/westeri/thunderbolt.git next`

---

## Related Documentation

- `docs/UPSTREAM_SYNC.md` — Comprehensive upstream sync guide
- `scripts/upstream-sync/lib/common.sh` — Shared automation library
- `scripts/archive/README_INTEGRATION_LOG.md` — Historical integration log
- `docs/FINDINGS.md` — Completed findings and fixes
- `docs/ROADMAP.md` — Completed roadmap items
