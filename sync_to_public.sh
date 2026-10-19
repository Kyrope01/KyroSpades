#!/usr/bin/env bash
# Sync a branch of this repo (KS-Dev) to the public KyroSpades repo as a PR.
#
# The two repos do not share git history -- KS-Dev was created from a snapshot
# import, so it has one root commit while KyroSpades has its full history, and
# neither is a GitHub fork of the other. That is why the normal "compare across
# forks" UI cannot be used and why copying files by hand felt necessary.
#
# This works around it by building the sync branch ON TOP of KyroSpades' main,
# so the branch does share an ancestor with the base and the PR shows only the
# real delta instead of the whole tree. The PR is opened entirely inside
# KyroSpades (head and base both there), which needs no fork relationship.
#
# Usage:
#   ./sync_to_public.sh                 # sync main
#   ./sync_to_public.sh some-branch     # sync another branch
#   ./sync_to_public.sh main --dry-run  # build and verify, push nothing
#
# Your working checkout is never touched: the branch is built in a throwaway
# git worktree under a temp directory.

set -euo pipefail

PUBLIC_REPO="Kyrope01/KyroSpades"
PUBLIC_REMOTE="public"
# Overridable so CI can inject credentials into the URL.
PUBLIC_URL="${PUBLIC_URL:-https://github.com/${PUBLIC_REPO}.git}"
BASE="main"

SRC="${1:-main}"
DRY_RUN=0
[ "${2:-}" = "--dry-run" ] && DRY_RUN=1

cd "$(dirname "$0")"

command -v gh >/dev/null || { echo "error: gh CLI not found" >&2; exit 1; }

if ! git rev-parse --verify --quiet "$SRC" >/dev/null; then
	echo "error: no such ref '$SRC'" >&2
	exit 1
fi

SRC_SHA="$(git rev-parse --short "$SRC")"

if git remote get-url "$PUBLIC_REMOTE" >/dev/null 2>&1; then
	git remote set-url "$PUBLIC_REMOTE" "$PUBLIC_URL"
else
	git remote add "$PUBLIC_REMOTE" "$PUBLIC_URL"
fi

echo "==> fetching $PUBLIC_REPO"
git fetch --quiet "$PUBLIC_REMOTE"

# Refuse to build a sync branch that would not be a descendant of the base --
# that is exactly the situation that produces an unusable PR.
BASE_SHA="$(git rev-parse "${PUBLIC_REMOTE}/${BASE}")"

STAMP="$(date -u +%Y%m%d-%H%M%S)"
SAFE_SRC="${SRC//\//-}"
# Overridable so repeated CI runs update one ongoing PR instead of piling up
# branches. Interactive runs stay timestamped.
BRANCH="${SYNC_BRANCH:-sync/${SAFE_SRC}-${STAMP}}"

TMP="$(mktemp -d)"
WT="${TMP}/worktree"
cleanup() {
	git worktree remove --force "$WT" >/dev/null 2>&1 || true
	rm -rf "$TMP"
}
trap cleanup EXIT

echo "==> building '$BRANCH' on top of ${PUBLIC_REMOTE}/${BASE} (${BASE_SHA:0:7})"
git worktree add --quiet --detach "$WT" "$BASE_SHA"

# Make the worktree tree exactly equal $SRC's tree: delete everything except the
# worktree's own .git, then extract the source tree over it. Deleting first is
# what makes deletions propagate, so the result is an exact tree match rather
# than a merge-ish overlay.
find "$WT" -mindepth 1 -maxdepth 1 ! -name '.git' -exec rm -rf {} +
git archive "$SRC" | tar -x -C "$WT"

cd "$WT"
git add -A

WANT_TREE="$(git rev-parse "${SRC}^{tree}")"
GOT_TREE="$(git write-tree)"
if [ "$WANT_TREE" != "$GOT_TREE" ]; then
	echo "error: staged tree $GOT_TREE != ${SRC} tree $WANT_TREE" >&2
	exit 1
fi
echo "==> tree matches ${SRC} exactly (${GOT_TREE:0:7})"

if git diff --quiet --cached HEAD; then
	echo "==> nothing to sync: ${PUBLIC_REPO}/${BASE} already matches ${SRC}"
	exit 0
fi

echo "==> delta vs ${PUBLIC_REPO}/${BASE}:"
git diff --stat HEAD | tail -20

CHANGED="$(git diff --name-only HEAD | wc -l | tr -d ' ')"
git commit --quiet -m "Sync from KS-Dev ${SRC}

Imported from the private KS-Dev repo at $(git rev-parse --short "$SRC").
${CHANGED} file(s) changed relative to ${BASE}."

if [ "$DRY_RUN" = "1" ]; then
	echo
	echo "==> dry run: built and verified, nothing pushed"
	echo "    branch would be: $BRANCH"
	echo "    commit:          $(git rev-parse --short HEAD)"
	exit 0
fi

echo "==> pushing $BRANCH to $PUBLIC_REPO"
git push --quiet --force "$PUBLIC_REMOTE" "HEAD:refs/heads/${BRANCH}"

echo "==> opening PR against $BASE"
BODY_FILE="${TMP}/body.md"
{
	echo "Synced from the private \`KS-Dev\` repo, branch \`${SRC}\` at \`${SRC_SHA}\`."
	echo
	echo "**${CHANGED} file(s) changed:**"
	echo
	echo '```'
	git diff --name-status HEAD~1
	echo '```'
} > "$BODY_FILE"

if EXISTING="$(gh pr list --repo "$PUBLIC_REPO" --head "$BRANCH" --json number --jq '.[0].number' 2>/dev/null)" \
	&& [ -n "$EXISTING" ]; then
	echo "==> updating existing PR #${EXISTING}"
	gh pr edit "$EXISTING" --repo "$PUBLIC_REPO" --body-file "$BODY_FILE"
	echo "https://github.com/${PUBLIC_REPO}/pull/${EXISTING}"
else
	gh pr create \
		--repo "$PUBLIC_REPO" \
		--base "$BASE" \
		--head "$BRANCH" \
		--title "Sync from KS-Dev ${SRC}" \
		--body-file "$BODY_FILE"
fi

echo "==> done"
