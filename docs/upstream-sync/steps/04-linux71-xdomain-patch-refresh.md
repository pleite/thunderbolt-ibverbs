# Backport upstream #50: refresh xdomain identity patch for Linux 7.1

## Upstream source
- PR: https://github.com/hellas-ai/thunderbolt-ibverbs/pull/50
- Merge commit: `da6b1ef9`

## Why this is needed
- Keeps kernel-workflow patch stack aligned with Linux 7.1 and fixes patch
  application drift.

## Fork-specific conflict/refactor notes
- Add upstream `0006` prep patch and refreshed `0009` patch form.
- Update `kernel-workflow/patches/local-integration-debug.nix` patch list.

## Validation
- Run existing kernel-workflow patch-stack checks used by repo CI/Nix.
