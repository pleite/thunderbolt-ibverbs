#!/usr/bin/env bash
# Verify that the package version and rdma-core patch set are consistent
# across packaging/, nix/, and dkms.conf.
#
# Canonical source of truth:
#   version        → PACKAGE_VERSION in dkms.conf
#   rdma-core patches → files listed under packaging/rdma-core-patches/*.patch
#
# Every other location that embeds either piece of information must agree.
# Run this locally before cutting a tag, or in CI to catch drift early.

set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"

pass=0
fail=0

check_eq() {
	local label="$1" expected="$2" actual="$3"
	if [[ "$actual" == "$expected" ]]; then
		printf '  ok    %s: %s\n' "$label" "$actual"
		(( pass++ )) || true
	else
		printf '  FAIL  %s: expected "%s", got "%s"\n' \
			"$label" "$expected" "$actual" >&2
		(( fail++ )) || true
	fi
}

# ── Version consistency ─────────────────────────────────────────────────────

dkms_version="$(awk -F'"' '/^PACKAGE_VERSION=/ { print $2; exit }' \
	"$repo_root/dkms.conf")"
[[ -n "$dkms_version" ]] ||
	{ printf 'error: could not read PACKAGE_VERSION from dkms.conf\n' >&2; exit 1; }

printf '==> Canonical version (dkms.conf): %s\n' "$dkms_version"

# nix/module.nix — version = "x.y.z"
nix_module_version="$(grep -m1 'version = ' \
	"$repo_root/nix/module.nix" | sed 's/.*version = "\([^"]*\)".*/\1/')"
check_eq "nix/module.nix version" "$dkms_version" "$nix_module_version"

# ── rdma-core patch consistency ─────────────────────────────────────────────

printf '\n==> rdma-core patches\n'

# Collect patch filenames that exist on disk.
mapfile -t disk_patches < <(
	find "$repo_root/packaging/rdma-core-patches" -maxdepth 1 \
		-name '*.patch' -printf '%f\n' | sort
)

# Collect patch filenames that flake.nix references.
mapfile -t nix_patches < <(
	grep -oP '(?<=rdma-core-patches/)[^"]+\.patch' "$repo_root/flake.nix" | sort
)

for p in "${disk_patches[@]}"; do
	if printf '%s\n' "${nix_patches[@]}" | grep -qxF "$p"; then
		printf '  ok    flake.nix references %s\n' "$p"
		(( pass++ )) || true
	else
		printf '  FAIL  flake.nix does not reference %s\n' "$p" >&2
		(( fail++ )) || true
	fi
done

for p in "${nix_patches[@]}"; do
	if [[ ! -f "$repo_root/packaging/rdma-core-patches/$p" ]]; then
		printf '  FAIL  flake.nix references %s but file is missing\n' "$p" >&2
		(( fail++ )) || true
	fi
done

# ── Summary ──────────────────────────────────────────────────────────────────

printf '\n==> %d passed, %d failed\n' "$pass" "$fail"

if [[ "$fail" -gt 0 ]]; then
	printf '\npackaging sync FAILED — update the locations listed above before cutting a tag.\n' >&2
	exit 1
fi

printf 'packaging sync OK\n'
