#!/usr/bin/env bash
# 00-setup-labels.sh — create every label the roadmap step scripts use.
#
# Run this once before the per-step scripts. It is idempotent: existing labels
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

info "ensuring roadmap labels on $REPO"

# name              color    description
ensure_label "meta"          "ededed" "Project meta-work and process"
ensure_label "epic"          "5319e7" "Large multi-part effort tracked with sub-issues"
ensure_label "kernel"        "1d76db" "Kernel module changes"
ensure_label "security"      "b60205" "Security threat model and hardening"
ensure_label "reliability"   "0e8a16" "Stability, error handling, fault tolerance"
ensure_label "apple"         "c5def5" "Apple / macOS compatible transport"
ensure_label "compatibility" "fbca04" "Kernel/distro compatibility"
ensure_label "performance"   "d93f0b" "Performance and tuning"
ensure_label "benchmarks"    "fef2c0" "Benchmark tooling and results"
ensure_label "testing"       "0052cc" "Tests and regression coverage"
ensure_label "ci"            "bfdadc" "Continuous integration and automation"
ensure_label "packaging"     "006b75" "Distro packaging and release"
ensure_label "release"       "5319e7" "Release process and artefacts"
ensure_label "documentation" "0075ca" "Documentation"

echo
ok "labels ready"
