# Standalone build for the apple_discover passive observer.
#
# Independent of usb4_rdma so we can load/unload while iterating without
# touching production data-path state.

KVER ?= $(shell uname -r)
KDIR ?= $(shell \
	if [ -e /lib/modules/$(KVER)/build ]; then \
		printf '%s\n' /lib/modules/$(KVER)/build; \
	else \
		d=$$(find /nix/store -maxdepth 1 -type d \
			-name '*-linux-$(KVER)-dev' -print -quit 2>/dev/null); \
		if [ -n "$$d" ]; then \
			printf '%s\n' "$$d/lib/modules/$(KVER)/build"; \
		else \
			d=$$(find /nix/store -maxdepth 1 -type d \
				-name '*-linux-usb4-*-dev' -print -quit 2>/dev/null); \
			if [ -n "$$d" ]; then \
				printf '%s\n' "$$d/lib/modules/$(KVER)/build"; \
			else \
				printf '%s\n' /lib/modules/$(KVER)/build; \
			fi; \
		fi; \
	fi)
PWD := $(shell pwd)

obj-m := apple_discover.o
ccflags-y := -Wall -Wextra -Wno-unused-parameter

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
