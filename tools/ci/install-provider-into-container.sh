#!/usr/bin/env bash
# Drop the host's usb4_rdma libibverbs provider into a running container so its
# stock libibverbs can enumerate usb4_rdma* devices.
#
# This is the supported form of the manual fallback described in
# docs/vllm-toolbox-integration.md (Phase 5.1): when `ibv_devices` works on the
# host but is empty inside the container, the container is missing the provider
# .driver hint and .so. This script copies both from the host (or from an
# explicit source) into the container's libibverbs directories.
#
# It does NOT rebuild the provider. If the container's libibverbs PABI differs
# from the host's, the .so may still fail to relocate inside the container; in
# that case build the provider against the container's rdma-core instead (see
# tools/ci/distro-package-rdma.sh and the RDMA_CORE_TAG guidance in
# docs/vllm-toolbox-integration.md).

set -euo pipefail

ENGINE="${TBV_CONTAINER_ENGINE:-}"
DRIVER_SRC=""
SO_SRC=""

usage() {
	cat <<'EOF'
Usage:
  tools/ci/install-provider-into-container.sh [options] <container>

Copies the usb4_rdma libibverbs provider (.driver hint + .so) from the host
into a running container so `ibv_devices` enumerates usb4_rdma* inside it.

Arguments:
  <container>          Name or ID of the running container (passed to the
                       container engine's `cp`/`exec`).

Options:
  --engine <name>      Container engine: podman, docker, or toolbox.
                       Auto-detected if omitted (env: TBV_CONTAINER_ENGINE).
  --driver <path>      Provider .driver file to copy
                       (default: /etc/libibverbs.d/usb4_rdma.driver on the host).
  --so <path>          Provider .so to copy
                       (default: host libusb4_rdma-rdmav*.so under a libibverbs dir).
  -h, --help           Show this help.

Examples:
  # podman/docker container named "vllm"
  tools/ci/install-provider-into-container.sh vllm

  # explicit engine and provider files
  tools/ci/install-provider-into-container.sh --engine docker \
      --driver /etc/libibverbs.d/usb4_rdma.driver \
      --so /usr/lib64/libibverbs/libusb4_rdma-rdmav34.so vllm
EOF
}

container=""
while [[ $# -gt 0 ]]; do
	case "$1" in
		-h|--help) usage; exit 0 ;;
		--engine)  ENGINE="${2:?--engine needs a value}"; shift 2 ;;
		--driver)  DRIVER_SRC="${2:?--driver needs a value}"; shift 2 ;;
		--so)      SO_SRC="${2:?--so needs a value}"; shift 2 ;;
		--)        shift; break ;;
		-*)        printf 'error: unknown option: %s\n' "$1" >&2; usage >&2; exit 1 ;;
		*)
			[[ -z "$container" ]] ||
				{ printf 'error: multiple containers given\n' >&2; exit 1; }
			container="$1"; shift ;;
	esac
done

[[ -n "$container" ]] || { printf 'error: container name/ID required\n\n' >&2; usage >&2; exit 1; }

# Resolve the container engine.
if [[ -z "$ENGINE" ]]; then
	for e in podman docker toolbox; do
		if command -v "$e" >/dev/null 2>&1; then ENGINE="$e"; break; fi
	done
fi
[[ -n "$ENGINE" ]] ||
	{ printf 'error: no container engine found (tried podman, docker, toolbox)\n' >&2; exit 1; }
command -v "$ENGINE" >/dev/null 2>&1 ||
	{ printf 'error: container engine not found: %s\n' "$ENGINE" >&2; exit 1; }

# Locate the host provider files if not given explicitly.
if [[ -z "$DRIVER_SRC" ]]; then
	DRIVER_SRC="/etc/libibverbs.d/usb4_rdma.driver"
fi
[[ -f "$DRIVER_SRC" ]] ||
	{ printf 'error: driver hint not found: %s (install usb4-rdma-provider on the host first)\n' "$DRIVER_SRC" >&2; exit 1; }

# Conventional libibverbs provider directories, in preference order:
# Debian/Ubuntu use the multiarch /usr/lib/x86_64-linux-gnu path, RHEL/Fedora
# use /usr/lib64, and /usr/lib is the final fallback. Shared by the host .so
# lookup and the container .so-dir detection below.
LIBIBVERBS_DIRS=(
	/usr/lib/x86_64-linux-gnu/libibverbs
	/usr/lib64/libibverbs
	/usr/lib/libibverbs
)

if [[ -z "$SO_SRC" ]]; then
	for d in "${LIBIBVERBS_DIRS[@]}"; do
		[[ -d "$d" ]] || continue
		SO_SRC="$(find "$d" -maxdepth 1 -name 'libusb4_rdma-rdmav*.so' -print -quit)"
		[[ -n "$SO_SRC" ]] && break
	done
fi
[[ -n "$SO_SRC" && -f "$SO_SRC" ]] ||
	{ printf 'error: provider .so not found on host; pass --so <path>\n' >&2; exit 1; }

printf '==> Engine:     %s\n' "$ENGINE"
printf '==> Container:  %s\n' "$container"
printf '==> Driver src: %s\n' "$DRIVER_SRC"
printf '==> .so src:    %s\n' "$SO_SRC"

# Determine the container's libibverbs .so directory. Prefer an existing dir
# that already holds providers; fall back to the conventional locations.
in_container() { "$ENGINE" exec "$container" "$@"; }

so_dir=""
for d in "${LIBIBVERBS_DIRS[@]}"; do
	if in_container test -d "$d" 2>/dev/null; then so_dir="$d"; break; fi
done
[[ -n "$so_dir" ]] ||
	{ printf 'error: no libibverbs provider dir in container (is rdma-core/libibverbs installed?)\n' >&2; exit 1; }

printf '==> Container .so dir: %s\n' "$so_dir"

# Ensure the .driver directory exists in the container, then copy both files.
in_container mkdir -p /etc/libibverbs.d
"$ENGINE" cp "$DRIVER_SRC" "$container:/etc/libibverbs.d/usb4_rdma.driver"
"$ENGINE" cp "$SO_SRC" "$container:$so_dir/$(basename "$SO_SRC")"

printf '==> Copied provider into container. Verifying...\n'
if in_container sh -c 'command -v ibv_devices >/dev/null 2>&1'; then
	in_container ibv_devices || true
else
	printf 'note: ibv_devices not in container PATH; install libibverbs-utils to verify.\n'
fi

printf '==> Done. If usb4_rdma* is still missing, the container libibverbs PABI\n'
printf '    likely differs from the host; rebuild the provider against the\n'
printf "    container's rdma-core (see RDMA_CORE_TAG guidance in the docs).\n"
