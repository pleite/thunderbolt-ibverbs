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

TOPDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
CI_SRCDIR := $(TOPDIR)/tools/ci

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2
CFLAGS += -I$(TOPDIR)

# Smoke test sources: name -> list of proto/*.c translation units needed
SMOKES := proto-smoke reliability-smoke identity-smoke config-smoke

# Source files for each smoke test (reliability, identity, config need their .c)
SMOKE_EXTRA_proto-smoke :=
SMOKE_EXTRA_reliability-smoke := $(TOPDIR)/proto/reliability.c
SMOKE_EXTRA_identity-smoke := $(TOPDIR)/proto/identity.c
SMOKE_EXTRA_config-smoke := $(TOPDIR)/proto/config.c $(TOPDIR)/proto/identity.c

.PHONY: all test clean

all: test

test: $(SMOKES)

$(SMOKES): %: $(CI_SRCDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $< $(SMOKE_EXTRA_$@)

clean:
	rm -f $(SMOKES)
