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
SRCDIR := $(TOPDIR)

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2
CFLAGS += -I$(SRCDIR)

# Smoke test sources: name -> list of proto/*.c translation units needed
SMOKES := proto-smoke reliability-smoke identity-smoke config-smoke

# Source files for each smoke test (reliability, identity, config need their .c)
SMOKE_RELIABILITY_SRCS := proto/reliability.c
SMOKE_IDENTITY_SRCS := proto/identity.c
SMOKE_CONFIG_SRCS := proto/config.c proto/identity.c

.PHONY: all clean

all: $(SMOKES)

proto-smoke: tools/ci/proto-smoke.c
	$(CC) $(CFLAGS) -o $@ $<

reliability-smoke: tools/ci/reliability-smoke.c $(SMOKE_RELIABILITY_SRCS)
	$(CC) $(CFLAGS) -o $@ $< $(SMOKE_RELIABILITY_SRCS)

identity-smoke: tools/ci/identity-smoke.c $(SMOKE_IDENTITY_SRCS)
	$(CC) $(CFLAGS) -o $@ $< $(SMOKE_IDENTITY_SRCS)

config-smoke: tools/ci/config-smoke.c $(SMOKE_CONFIG_SRCS)
	$(CC) $(CFLAGS) -o $@ $< $(SMOKE_CONFIG_SRCS)

clean:
	rm -f $(SMOKES)
