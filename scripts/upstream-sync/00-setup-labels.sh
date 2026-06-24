#!/usr/bin/env bash
# 00-setup-labels.sh — create labels used by upstream-sync scripts.

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

require_repo

info "ensuring upstream-sync labels on $REPO"

ensure_label "upstream-sync" "5319e7" "Track upstream backports and sync work"
ensure_label "apple" "fbca04" "Apple interop transport behavior"
ensure_label "compatibility" "cfd3d7" "Kernel/API compatibility fixes"
ensure_label "kernel" "1d76db" "Kernel module changes"
ensure_label "kernel-workflow" "bfdadc" "Maintainer kernel patch-stack integration"
ensure_label "testing" "0052cc" "Tests and smoke coverage"
ensure_label "refactor" "c2e0c6" "Internal refactoring with no behavior change"

echo
ok "labels ready"
