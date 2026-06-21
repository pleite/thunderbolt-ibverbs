# SPDX-License-Identifier: GPL-2.0
# Freestanding smoke binaries for proto/ (no kernel headers required).
#
# Mirrors the source lists used by flake.nix (.#checks.*.proto-smoke) and the
# release-artefacts CI job so downstream consumers (e.g. the tb-vllm-toolbox
# image) can build every smoke test with a single `make -C tools/ci` instead of
# reverse-engineering the per-binary translation units from #includes.
#
# Run from the repository root or from tools/ci/ — paths are resolved relative
# to the repo root regardless.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror
REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
CPPFLAGS += -I$(REPO_ROOT)

# Smoke binaries and their extra proto/*.c translation units. The smoke .c file
# itself is tools/ci/<name>.c; SRCS_<name> lists the additional sources it needs.
SMOKE_BINS := proto-smoke reliability-smoke identity-smoke config-smoke

SRCS_proto-smoke       :=
SRCS_reliability-smoke := $(REPO_ROOT)/proto/reliability.c
SRCS_identity-smoke    := $(REPO_ROOT)/proto/identity.c
SRCS_config-smoke      := $(REPO_ROOT)/proto/config.c $(REPO_ROOT)/proto/identity.c

.PHONY: all build test clean

all: test

build: $(SMOKE_BINS)

# Build then run each smoke binary, matching CI behaviour.
test: build
	@for t in $(SMOKE_BINS); do \
		echo "RUN  ./$$t"; \
		./$$t || exit 1; \
	done

$(SMOKE_BINS): %: $(REPO_ROOT)/tools/ci/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(SRCS_$@) -o $@

clean:
	rm -f $(SMOKE_BINS)
