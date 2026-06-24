# Backport upstream #49: handle Rawhide ib_umem_get_va API

## Upstream source
- PR: https://github.com/hellas-ai/thunderbolt-ibverbs/pull/49
- Merge commit: `97f29a51`

## Why this is needed
- Linux >= 7.2 drops `ib_umem_get()` declaration; backport keeps new headers
  buildable while preserving old-kernel behavior.

## Fork-specific conflict/refactor notes
- Upstream changed `kernel/ibdev.c`; this fork must apply equivalent logic in
  `kernel/ibdev_mr.c` (R7 split).

## Validation
- `make -C kernel modules KDIR=/lib/modules/$(uname -r)/build`
- Rawhide package build/install checks when available
