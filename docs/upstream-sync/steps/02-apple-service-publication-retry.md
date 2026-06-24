# Backport upstream #46: retry deferred Apple service publication

## Upstream source
- PR: https://github.com/hellas-ai/thunderbolt-ibverbs/pull/46
- Merge commit: `825eb388`

## Why this is needed
- Avoids stuck `apple_rails_pending=1` state when transient tunnel setup fails.
- Uses worker-owned publication and delayed retries.

## Fork-specific conflict/refactor notes
- Ensure delayed-work conversion in `kernel/tbv.h` + `kernel/service.c` remains
  compatible with downstream hot-unplug behavior and service teardown ordering.

## Validation
- `make -C kernel modules KDIR=/lib/modules/$(uname -r)/build`
- `make -C tools/ci test`
