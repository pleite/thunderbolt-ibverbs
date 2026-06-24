#!/usr/bin/env bash
# run-all.sh — run every upstream-sync step script in order.

set -euo pipefail
cd "$(dirname "$0")"

info_prefix=$'\033[34m==>\033[0m'
printf '%s setting up labels\n' "$info_prefix"
./00-setup-labels.sh

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
  echo "no matching upstream-sync scripts found" >&2
  exit 1
fi

for s in "${selected[@]}"; do
  printf '\n%s running %s\n' "$info_prefix" "$s"
  "./$s"
done

printf '\n%s all requested upstream-sync steps processed\n' "$info_prefix"
