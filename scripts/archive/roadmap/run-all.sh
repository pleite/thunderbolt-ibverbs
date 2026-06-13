#!/usr/bin/env bash
# run-all.sh — run every roadmap step script in order.
#
# Runs 00-setup-labels.sh first, then each NN-*.sh step in numeric order. Each
# script is idempotent, so re-running is safe. Pass DRY_RUN=1 to preview.
#
# Usage:
#   ./run-all.sh                 # create labels, then all steps 01..08
#   DRY_RUN=1 ./run-all.sh       # preview everything
#   ./run-all.sh 01 03 05        # only the given step numbers (labels still run)

set -euo pipefail
cd "$(dirname "$0")"

# Always make sure labels exist first.
info_prefix=$'\033[34m==>\033[0m'
printf '%s setting up labels\n' "$info_prefix"
./00-setup-labels.sh

# Collect the step scripts (NN-*.sh, excluding 00). Zero-padded names mean the
# shell glob already yields them in numeric order.
all_steps=()
for s in [0-9][0-9]-*.sh; do
  [ -e "$s" ] || continue
  case "$s" in 00-*) continue ;; esac
  all_steps+=("$s")
done

selected=()
if [ "$#" -gt 0 ]; then
  for want in "$@"; do
    for s in "${all_steps[@]}"; do
      case "$s" in
        "$want"-*) selected+=("$s") ;;
      esac
    done
  done
else
  selected=("${all_steps[@]}")
fi

if [ "${#selected[@]}" -eq 0 ]; then
  echo "no matching step scripts found" >&2
  exit 1
fi

for s in "${selected[@]}"; do
  printf '\n%s running %s\n' "$info_prefix" "$s"
  "./$s"
done

printf '\n%s all requested roadmap steps processed\n' "$info_prefix"
