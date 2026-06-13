# SPDX-License-Identifier: GPL-2.0
# Freestanding unit tests for proto/ (no kernel headers required).

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -g

TEST_BINS := test_reliability_dedup

.PHONY: all test clean

all: test

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "RUN  ./$$t"; \
		./$$t; \
	done

test_reliability_dedup: test_reliability_dedup.c reliability.c reliability.h
	$(CC) $(CFLAGS) -o $@ test_reliability_dedup.c reliability.c

clean:
	rm -f $(TEST_BINS)
