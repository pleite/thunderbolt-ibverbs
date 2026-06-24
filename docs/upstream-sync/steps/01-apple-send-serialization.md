# Backport upstream #44: serialize Apple UC SENDs per QP

## Upstream source
- PR: https://github.com/hellas-ai/thunderbolt-ibverbs/pull/44
- Merge commit: `a14a175`

## Why this is needed
- Correctness fix for Linux→macOS short SEND corruption/timeouts.
- Removes single-frame fast path and serializes all non-empty Apple SENDs per QP.

## Fork-specific conflict/refactor notes
- This fork still has duplicated TX-window predicates in `kernel/ibdev.c` and has
  not landed upstream #42 helper extraction.
- Integrate shared `proto/apple_tx.h` policy helpers and preserve R2/R3 logic.

## Validation
- `make -C kernel modules KDIR=/lib/modules/$(uname -r)/build`
- `make -C tools/ci test`
- `make -C proto test`
