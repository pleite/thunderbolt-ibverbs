#!/usr/bin/env bash
# Shared helpers for upstream-sync automation scripts.
# shellcheck shell=bash

set -euo pipefail

BASE_BRANCH="${BASE_BRANCH:-}"
BRANCH_PREFIX="${BRANCH_PREFIX:-upstream-sync}"
COPILOT_HANDLE="${COPILOT_HANDLE:-@copilot}"
COPILOT_ASSIGNEE="${COPILOT_ASSIGNEE:-}"
DRY_RUN="${DRY_RUN:-}"
REPO="${REPO:-}"

_c_reset=$'\033[0m'; _c_blue=$'\033[34m'; _c_green=$'\033[32m'
_c_yellow=$'\033[33m'; _c_red=$'\033[31m'

info()  { printf '%s==>%s %s\n' "$_c_blue"   "$_c_reset" "$*"; }
ok()    { printf '%s ok %s %s\n' "$_c_green"  "$_c_reset" "$*"; }
warn()  { printf '%swarn%s %s\n' "$_c_yellow" "$_c_reset" "$*" >&2; }
die()   { printf '%serr %s %s\n' "$_c_red"    "$_c_reset" "$*" >&2; exit 1; }

run() {
  if [ -n "$DRY_RUN" ]; then
    printf '   [dry-run] %s\n' "$*"
    return 0
  fi
  "$@"
}

require_cmds() {
  local missing=0 c
  for c in "$@"; do
    command -v "$c" >/dev/null 2>&1 || { warn "missing required command: $c"; missing=1; }
  done
  [ "$missing" -eq 0 ] || die "install the missing commands above and re-run"
}

remote_branch_exists() {
  local branch="$1"
  gh api "repos/$REPO/branches/$branch" >/dev/null 2>&1
}

detect_base_branch() {
  local current
  current="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
  if [ -n "$current" ] && [ "$current" != "HEAD" ] && remote_branch_exists "$current"; then
    printf '%s\n' "$current"
    return 0
  fi

  gh repo view "$REPO" --json defaultBranchRef -q .defaultBranchRef.name
}

require_repo() {
  require_cmds gh git

  if ! gh auth status >/dev/null 2>&1; then
    if [ -n "$DRY_RUN" ] && [ -n "$REPO" ] && [ -n "$BASE_BRANCH" ]; then
      warn "gh not authenticated; continuing because DRY_RUN with REPO/BASE_BRANCH set"
    else
      die "gh is not authenticated; run: gh auth login"
    fi
  fi

  if [ -z "$REPO" ]; then
    REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner)" \
      || die "could not determine repo; run inside a clone or set REPO=owner/name"
  fi

  if [ -z "$BASE_BRANCH" ]; then
    BASE_BRANCH="$(detect_base_branch)" || die "could not determine base branch; set BASE_BRANCH"
  fi

  info "repo:   $REPO"
  info "base:   $BASE_BRANCH"
  [ -n "$DRY_RUN" ] && warn "DRY_RUN is set — no changes will be made"
}

ensure_label() {
  local name="$1" color="$2" desc="$3"
  if gh label list --repo "$REPO" --limit 200 --json name -q '.[].name' 2>/dev/null \
       | grep -qxF "$name"; then
    ok "label exists: $name"
  else
    info "creating label: $name"
    run gh label create "$name" --repo "$REPO" --color "$color" --description "$desc" \
      || warn "could not create label '$name' (insufficient permissions?) — continuing"
  fi
}

find_issue_number() {
  local title="$1"
  gh issue list --repo "$REPO" --state all --search "$title in:title" \
    --json number,title -q ".[] | select(.title == \"$title\") | .number" \
    2>/dev/null | head -n1
}

create_or_get_issue() {
  local title="$1" body_file="$2" labels="$3" num
  num="$(find_issue_number "$title" || true)"
  if [ -n "$num" ]; then
    ok "issue exists: #$num  $title" >&2
    printf '%s\n' "$num"
    return 0
  fi

  info "creating issue: $title" >&2
  if [ -n "$DRY_RUN" ]; then
    printf '   [dry-run] gh issue create --title %q --label %q --body-file %q\n' \
      "$title" "$labels" "$body_file" >&2
    printf 'DRYRUN\n'
    return 0
  fi

  local url
  url="$(gh issue create --repo "$REPO" --title "$title" \
        --label "$labels" --body-file "$body_file")" \
    || die "failed to create issue: $title"
  ok "created issue: $url" >&2
  printf '%s\n' "${url##*/}"
}

branch_ahead_count() {
  local branch="$1" ahead
  if ! ahead="$(gh api "repos/$REPO/compare/$BASE_BRANCH...$branch" --jq '.ahead_by' 2>/dev/null)"; then
    warn "could not compare $branch against $BASE_BRANCH; assuming not ahead and re-seeding"
    ahead=0
  fi
  case "$ahead" in
    ''|*[!0-9]*)
      warn "compare result for $branch was non-numeric ('$ahead'); assuming not ahead and re-seeding"
      ahead=0
      ;;
  esac
  printf '%s\n' "$ahead"
}

base_branch_sha() {
  gh api "repos/$REPO/git/ref/heads/$BASE_BRANCH" --jq '.object.sha' 2>/dev/null
}

ensure_branch_ref() {
  local branch="$1" sha
  if remote_branch_exists "$branch"; then
    return 0
  fi
  sha="$(base_branch_sha)" || true
  [ -n "$sha" ] || die "could not resolve tip of $BASE_BRANCH to create $branch"
  info "creating branch ref: $branch (from $BASE_BRANCH @ ${sha:0:7})"
  gh api "repos/$REPO/git/refs" -X POST -f "ref=refs/heads/$branch" -f "sha=$sha" >/dev/null \
    || die "failed to create branch ref: $branch"
}

put_seed_file() {
  local branch="$1" seed_rel="$2" seed_src="$3" msg="$4" content sha
  content="$(base64 < "$seed_src" | tr -d '\n')"
  sha="$(gh api "repos/$REPO/contents/$seed_rel?ref=$branch" --jq '.sha' 2>/dev/null || true)"

  local args=(-X PUT -f "message=$msg" -f "branch=$branch" -f "content=$content")
  [ -n "$sha" ] && [ "$sha" != "null" ] && args+=(-f "sha=$sha")

  gh api "repos/$REPO/contents/$seed_rel" "${args[@]}" >/dev/null \
    || die "failed to commit seed file to branch: $branch"
}

create_branch_with_seed() {
  local branch="$1" seed_rel="$2" seed_src="$3" msg="$4"

  if remote_branch_exists "$branch"; then
    local ahead
    ahead="$(branch_ahead_count "$branch")"
    if [ "$ahead" -gt 0 ]; then
      ok "branch exists on remote ($ahead commit(s) ahead of $BASE_BRANCH): $branch"
      return 0
    fi

    warn "branch exists but is not ahead of $BASE_BRANCH: $branch — adding seed commit"
    if [ -n "$DRY_RUN" ]; then
      printf '   [dry-run] commit seed %s to existing %s via contents API\n' "$seed_rel" "$branch"
      return 0
    fi

    put_seed_file "$branch" "$seed_rel" "$seed_src" "$msg"
    ok "seeded existing branch: $branch"
    return 0
  fi

  info "creating branch: $branch (from $BASE_BRANCH)"
  if [ -n "$DRY_RUN" ]; then
    printf '   [dry-run] create ref %s + commit seed %s via contents API\n' "$branch" "$seed_rel"
    return 0
  fi

  ensure_branch_ref "$branch"
  put_seed_file "$branch" "$seed_rel" "$seed_src" "$msg"
  ok "pushed branch: $branch"
}

find_pr_number() {
  local branch="$1"
  gh pr list --repo "$REPO" --head "$branch" --state all \
    --json number -q '.[0].number' 2>/dev/null | head -n1
}

create_or_get_pr() {
  local branch="$1" title="$2" body_file="$3" num
  num="$(find_pr_number "$branch" || true)"
  if [ -n "$num" ] && [ "$num" != "null" ]; then
    ok "PR exists: #$num  ($branch)" >&2
    printf '%s\n' "$num"
    return 0
  fi

  info "creating draft PR: $title" >&2
  if [ -n "$DRY_RUN" ]; then
    printf '   [dry-run] gh pr create --head %q --base %q --draft\n' "$branch" "$BASE_BRANCH" >&2
    printf 'DRYRUN\n'
    return 0
  fi

  local url
  url="$(gh pr create --repo "$REPO" --head "$branch" --base "$BASE_BRANCH" \
        --title "$title" --body-file "$body_file" --draft)" \
    || die "failed to create PR for branch: $branch"
  ok "created PR: $url" >&2
  printf '%s\n' "${url##*/}"
}

assign_copilot() {
  local num="$1"
  [ -n "$num" ] && [ "$num" != "DRYRUN" ] || return 0
  [ -n "$COPILOT_ASSIGNEE" ] || return 0

  info "assigning #$num to $COPILOT_ASSIGNEE"
  run gh issue edit "$num" --repo "$REPO" --add-assignee "$COPILOT_ASSIGNEE" \
    || warn "could not assign $COPILOT_ASSIGNEE to #$num (continuing with @mention)"
}

tag_copilot_on_pr() {
  local pr="$1" issue="$2" summary="$3"
  if [ -z "$pr" ] || [ "$pr" = "DRYRUN" ]; then
    warn "no PR number; skipping Copilot tag"
    return 0
  fi

  local body
  body=$(cat <<BODY
$COPILOT_HANDLE please implement this upstream-sync step.

Tracking issue: #$issue

$summary
BODY
)

  info "tagging $COPILOT_HANDLE on PR #$pr"
  if [ -n "$DRY_RUN" ]; then
    printf '   [dry-run] gh pr comment %s --body <copilot handoff>\n' "$pr"
    return 0
  fi
  gh pr comment "$pr" --repo "$REPO" --body "$body" \
    || warn "could not comment on PR #$pr"
}

run_upstream_step() {
  : "${STEP_NUM:?}" "${STEP_SLUG:?}" "${STEP_TITLE:?}" "${STEP_LABELS:?}"
  : "${STEP_ISSUE_BODY:?}" "${STEP_PR_BODY:?}" "${STEP_HANDOFF:?}"

  require_repo

  local branch="$BRANCH_PREFIX/$STEP_NUM-$STEP_SLUG"
  local seed_rel="docs/upstream-sync/steps/$STEP_NUM-$STEP_SLUG.md"

  local tmp; tmp="$(mktemp -d)"
  local issue_body="$tmp/issue.md" pr_body="$tmp/pr.md" seed="$tmp/seed.md"
  printf '%s\n' "$STEP_ISSUE_BODY" >"$issue_body"
  printf '%s\n' "$STEP_PR_BODY" >"$pr_body"
  printf '# %s\n\n%s\n' "$STEP_TITLE" "$STEP_ISSUE_BODY" >"$seed"

  local issue pr
  issue="$(create_or_get_issue "$STEP_TITLE" "$issue_body" "$STEP_LABELS")"

  printf '\n\nCloses #%s\n' "$issue" >>"$pr_body"

  create_branch_with_seed "$branch" "$seed_rel" "$seed" \
    "docs(upstream-sync): seed step $STEP_NUM ($STEP_SLUG)"

  pr="$(create_or_get_pr "$branch" "$STEP_TITLE" "$pr_body")"

  assign_copilot "$issue"
  tag_copilot_on_pr "$pr" "$issue" "$STEP_HANDOFF"

  rm -rf "$tmp"

  echo
  ok "upstream-sync step $STEP_NUM done — issue #$issue, branch $branch, PR #$pr"
}
