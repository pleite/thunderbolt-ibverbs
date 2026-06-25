# SPDX-License-Identifier: GPL-2.0
#
# Out-of-tree build for the release-track thunderbolt-ibverbs module.

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
PWD := $(shell pwd)
tbv_debug_surfaces ?= 1
# GPU-direct (dma-buf MR) build-time gate.  Off by default so out-of-box
# behavior stays on the host-copy path (see docs/gpu-direct-plan.md Phase 1).
tbv_gpu_direct ?= 0

obj-m := thunderbolt_ibverbs.o
thunderbolt_ibverbs-y := main.o profile.o backend.o configfs.o core.o debugfs.o ibdev.o ibdev_native.o ibdev_apple.o ibdev_cq.o ibdev_mr.o ibdev_qp.o link.o native.o apple.o trace.o native_control.o native_control_legacy.o native_control_xdomain.o path.o peer.o rail.o service.o tbnet_identity.o tbnet_minimal.o tbv_compat.o ../proto/config.o ../proto/identity.o ../proto/reliability.o

ccflags-y := -Wall -Wextra -Wno-unused-parameter -I$(src) -I$(src)/.. -DTBV_DEBUG_SURFACES_COMPILED=$(tbv_debug_surfaces)

# Feature detection: ib_dmah parameter added to reg_user_mr in Linux 6.13.
# On Debian/Ubuntu the rdma headers live in a *-common sibling of KDIR
# (e.g. linux-headers-7.0.12+deb14.1-common) rather than in the arch-specific
# tree that KDIR points to.  Probe both locations so detection works on
# monolithic and split-header distros alike.
_tbv_kdir_real   := $(realpath $(KDIR))
_tbv_kdir_common := $(firstword $(wildcard $(dir $(_tbv_kdir_real))*-common))
_tbv_ib_verbs_h  := $(firstword $(wildcard \
    $(KDIR)/include/rdma/ib_verbs.h \
    $(_tbv_kdir_common)/include/rdma/ib_verbs.h))
ifneq ($(shell test -n "$(_tbv_ib_verbs_h)" && \
    grep -q 'struct ib_dmah' "$(_tbv_ib_verbs_h)" 2>/dev/null && echo yes),)
    ccflags-y += -DTBV_KERNEL_HAS_IB_DMAH
endif

# GPU-direct: compile in the gated reg_user_mr_dmabuf op when requested.
ifeq ($(tbv_gpu_direct),1)
    ccflags-y += -DCONFIG_TBV_GPU_DIRECT
endif

# Older kernels declare the dma-buf umem helpers in a dedicated header; newer
# trees fold them into <rdma/ib_umem.h>.  Include the dedicated header only when
# it is present.  Probe both the arch-specific and *-common header locations,
# mirroring the ib_verbs.h probe above.
_tbv_ib_umem_dmabuf_h := $(firstword $(wildcard \
    $(KDIR)/include/rdma/ib_umem_dmabuf.h \
    $(_tbv_kdir_common)/include/rdma/ib_umem_dmabuf.h))
ifneq ($(_tbv_ib_umem_dmabuf_h),)
    ccflags-y += -DTBV_KERNEL_HAS_IB_UMEM_DMABUF_H
endif

# Feature detection: ib_umem_get_va() added in Linux 7.2.
_tbv_ib_umem_h := $(firstword $(wildcard \
    $(KDIR)/include/rdma/ib_umem.h \
    $(_tbv_kdir_common)/include/rdma/ib_umem.h))
ifneq ($(shell test -n "$(_tbv_ib_umem_h)" && \
    grep -q 'ib_umem_get_va' "$(_tbv_ib_umem_h)" 2>/dev/null && echo yes),)
    ccflags-y += -DTBV_HAS_IB_UMEM_GET_VA_API
endif

.PHONY: all clean modules modules_install help

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) ../proto/*.o ../proto/.*.o.cmd

help:
	@echo "Targets:"
	@echo "  modules            Build thunderbolt_ibverbs.ko"
	@echo "  modules_install    Install under /lib/modules/\$$(uname -r)/extra/"
	@echo "  clean              Remove build artifacts"
	@echo ""
	@echo "Parameters:"
	@echo "  compat=auto|force|off"
	@echo "  profile=auto|mac_compat|linux_perf|mixed"
	@echo "  tbnet=auto|allow|prefer_rdma|block"
	@echo "  tbnet_identity=auto|stock|stock_proxy|minimal_packet|off"
	@echo "  tbnet_identity_tbnet=<netdev>"
	@echo "  tbnet_identity_gid=auto|<netdev>"
	@echo "  tbnet_identity_minimal_e2e=0|1"
	@echo "  tbnet_identity_minimal_apple_only=0|1"
	@echo "  bind_services=0|1"
	@echo "  tbv_debug_surfaces=0|1"
	@echo "  tbv_gpu_direct=0|1"
	@echo "  allocate_rings=0|1"
	@echo "  start_rings=0|1"
	@echo "  negotiate_native=0|1"
	@echo "  enable_tunnels=0|1"
	@echo "  register_verbs=0|1"
	@echo "  native_prtcstns=<u32>"
	@echo "  apple_prtcstns=<u32>"
	@echo "  peer_allowlist=<uuid[,uuid...]>"
	@echo "  peer_auth_acl=<uuid=32hexpsk[,uuid=32hexpsk...]>"
	@echo "  gpu_direct=auto|on|off"
	@echo "  gpu_direct_dynamic=0|1"
	@echo "  apple_tx_max_inflight_wr=<N>"
	@echo "  apple_tx_max_inflight_frames=<N>"
	@echo "  apple_rx_pending_bytes=<bytes>"
	@echo "  apple_rx_pending_slots=<N>"
	@echo "  apple_rx_pending_total_bytes=<bytes>"
	@echo "  apple_rx_trace=<N>"
