# SPDX-License-Identifier: GPL-2.0
#
# tools/ci/Makefile — build the freestanding proto smoke-test binaries.
#
# These smoke tests compile without kernel headers or rdma-core and give
# a quick sanity check of the wire protocol / reliability / identity /
# config code paths.
#
# Usage:
#   make -C tools/ci          # build all smoke binaries
#   make -C tools/ci proto-smoke  # build specific test
#
# CC and CFLAGS can be overridden on the command line:
#   make CC=clang CFLAGS="-O2 -g" -C tools/ci
#

THIS_MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TOPDIR := $(abspath $(THIS_MAKEFILE_DIR)/../..)
CI_SRCDIR := $(TOPDIR)/tools/ci

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2
CFLAGS += -I$(TOPDIR)

# Smoke test sources: name -> list of proto/*.c translation units needed
SMOKES := proto-smoke reliability-smoke identity-smoke config-smoke

# Source files for each smoke test (reliability, identity, config need their .c)
.PHONY: all test clean

all: test

test: $(SMOKES)

define BUILD_SMOKE
$1: $(CI_SRCDIR)/$1.c $2
	$(CC) $(CFLAGS) -o $$@ $$< $2
endef

$(eval $(call BUILD_SMOKE,proto-smoke,))
$(eval $(call BUILD_SMOKE,reliability-smoke,$(TOPDIR)/proto/reliability.c))
$(eval $(call BUILD_SMOKE,identity-smoke,$(TOPDIR)/proto/identity.c))
$(eval $(call BUILD_SMOKE,config-smoke,$(TOPDIR)/proto/config.c $(TOPDIR)/proto/identity.c))

clean:
	rm -f $(SMOKES)
