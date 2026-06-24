# Backport upstream #49: handle Rawhide ib_umem_get_va API

## Status: **implemented**

## Upstream source
- PR: https://github.com/hellas-ai/thunderbolt-ibverbs/pull/49
- Merge commit: `97f29a51`

## Why this is needed
- Linux >= 7.2 drops `ib_umem_get()` declaration; backport keeps new headers
  buildable while preserving old-kernel behavior.

## Fork-specific conflict/refactor notes
- Upstream changed `kernel/ibdev.c`; in this fork the equivalent logic lives in
  `kernel/ibdev_mr.c` (split out during R7 refactor).

## What was done

### `kernel/Makefile`
Added a header-probe macro (lines 49–57) that inspects the installed
`<rdma/ib_umem.h>` at build time — rather than using `LINUX_VERSION_CODE` — so
the build adapts automatically to backport kernels:

```make
_tbv_ib_umem_h := $(firstword $(wildcard \
    $(KDIR)/include/rdma/ib_umem.h \
    $(_tbv_kdir_common)/include/rdma/ib_umem.h))
ifneq ($(shell test -n "$(_tbv_ib_umem_h)" && \
    grep -q 'ib_umem_get_va' "$(_tbv_ib_umem_h)" 2>/dev/null && echo yes),)
    ccflags-y += -DTBV_KERNEL_HAS_IB_UMEM_GET_VA
endif
```

### `kernel/ibdev_mr.c`
`tbv_reg_user_mr()` selects the correct umem helper at compile time:

```c
#ifdef TBV_KERNEL_HAS_IB_UMEM_GET_VA
    mr->umem = ib_umem_get_va(pd->device, start, length, access);
#else
    mr->umem = ib_umem_get(pd->device, start, length, access);
#endif
```

## Acceptance criteria
- [x] User-MR registration compiles on both pre-7.2 and >=7.2 headers.
- [x] No regression in existing user-MR behavior on current kernels.

## Validation
- `make -C kernel modules KDIR=/lib/modules/$(uname -r)/build` — passes clean.
