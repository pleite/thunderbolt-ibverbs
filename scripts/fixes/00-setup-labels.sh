#!/usr/bin/env bash
# 00-setup-labels.sh — create every label the fix step scripts use.
#
# Run this once before the per-fix scripts. It is idempotent: existing labels
# are left untouched. Requires `gh` authenticated with repo write access.
#
# Usage:
#   ./00-setup-labels.sh
#   DRY_RUN=1 ./00-setup-labels.sh      # preview only
#   REPO=owner/name ./00-setup-labels.sh

set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
source "lib/common.sh"

require_repo

info "ensuring fix labels on $REPO"

# name              color    description
ensure_label "epic"          "5319e7" "Large multi-part effort tracked with sub-issues"
ensure_label "kernel"        "1d76db" "Kernel module changes"
ensure_label "security"      "b60205" "Security threat model and hardening"
ensure_label "reliability"   "0e8a16" "Stability, error handling, fault tolerance"
ensure_label "performance"   "d93f0b" "Performance and tuning"
ensure_label "testing"       "0052cc" "Tests and regression coverage"
ensure_label "ci"            "bfdadc" "Continuous integration and automation"
ensure_label "documentation" "0075ca" "Documentation"
ensure_label "refactor"      "c2e0c6" "Internal refactoring with no behaviour change"

echo
ok "labels ready"
