#!/bin/sh
set -eu

# Ensure we are inside a Git working tree (not just a bare repo or .git dir)
if [ "$(git rev-parse --is-inside-work-tree 2>/dev/null)" != "true" ]; then
    echo "ERROR: This script must be run inside a Git working tree." >&2
    exit 1
fi

# Ensure we are on branch 'trunk'
current_branch=$(git rev-parse --abbrev-ref HEAD)
if [ "$current_branch" != "trunk" ]; then
    echo "ERROR: This script must be run while on the 'trunk' branch." >&2
    echo "Current branch is: $current_branch" >&2
    exit 1
fi

echo "Upstream ref: upstream/trunk"
echo "Local branch: trunk"
echo

# Ensure clean working tree (no uncommitted changes)
if ! git diff-index --quiet HEAD --; then
    echo "ERROR: Working tree is not clean. Commit or stash your changes first." >&2
    exit 1
fi

# Make sure origin/trunk is up to date
git fetch origin

# Abort if trunk has commits not on origin/trunk
if [ "$(git rev-list --count origin/trunk..trunk)" -ne 0 ]; then
    echo "ERROR: trunk has unpushed commits relative to origin/trunk; aborting reset." >&2
    exit 1
fi

# Reset the branch to origin/trunk and reset tags to match origin
git reset --hard origin/trunk
git tag -l | xargs -r git tag -d > /dev/null 2>&1    # Delete all local tags
git fetch origin --tags > /dev/null 2>&1             # Re-fetch tags from origin

# Create temp files
old_commits=$(mktemp)
new_commits=$(mktemp)
tags_map=$(mktemp)

cleanup() {
    rm -f "$old_commits" "$new_commits" "$tags_map"
}
trap cleanup EXIT INT TERM

# Fetch latest upstream
echo "Fetching upstream..."
git fetch upstream
echo

# Collect commits ahead of upstream/trunk (old commits), oldest first
echo "Determining commits ahead of upstream/trunk..."
git rev-list --reverse upstream/trunk..trunk >"$old_commits"

old_count=$(wc -l <"$old_commits" | tr -d ' ')
if [ "$old_count" -eq 0 ]; then
    echo "No commits ahead of upstream/trunk; nothing to do."
    exit 0
fi

echo "Commits ahead of upstream/trunk:"
while IFS= read -r sha; do
    subj=$(git log -1 --pretty='%s' "$sha")
    echo "  $sha  $subj"
done <"$old_commits"
echo

# Build a mapping: old_commit -> tags (space-separated)
echo "Recording tags on ahead-of-upstream commits..."
: >"$tags_map"
tagged_count=0
while IFS= read -r sha; do
    tags=$(git tag --points-at "$sha" || true)
    if [ -n "$tags" ]; then
        tags_space=$(printf '%s\n' "$tags" | tr '\n' ' ' | sed 's/ *$//')
        echo "$sha $tags_space" >>"$tags_map"
        echo "  $sha: $tags_space"
        tagged_count=$((tagged_count + 1))
    fi
done <"$old_commits"
echo

if [ "$tagged_count" -eq 0 ]; then
    echo "NOTE: No tags found on any commits ahead of upstream/trunk."
    echo "      There will be no tags to move after the rebase."
fi

echo "Rebasing trunk onto upstream/trunk..."
if ! git rebase upstream/trunk; then
    echo "ERROR: Rebase failed (likely due to conflicts)." >&2
    echo "       Resolve the conflicts and finish the rebase manually," >&2
    echo "       then run this script again when trunk is clean." >&2
    exit 1
fi
echo "Rebase complete."
echo

# Collect new commits ahead of upstream/trunk after rebase
echo "Determining commits ahead of upstream/trunk after rebase..."
git rev-list --reverse upstream/trunk..trunk >"$new_commits"

new_count=$(wc -l <"$new_commits" | tr -d ' ')

echo "New commits ahead of upstream/trunk:"
while IFS= read -r sha; do
    subj=$(git log -1 --pretty='%s' "$sha")
    echo "  $sha  $subj"
done <"$new_commits"
echo

# Sanity check: same number of commits before and after
if [ "$new_count" -ne "$old_count" ]; then
    echo "ERROR: Number of commits ahead of upstream changed after rebase." >&2
    echo "Old count: $old_count, new count: $new_count" >&2
    echo "Not reassigning tags automatically." >&2
    exit 1
fi

# Reassign tags: pair old[i] with new[i] by position
echo "Reassigning tags from old commits to new commits..."
paste "$old_commits" "$new_commits" | while IFS="$(printf '\t')" read -r old new; do
    line=$(grep "^$old " "$tags_map" 2>/dev/null || true)
    if [ -n "$line" ]; then
        tags=$(echo "$line" | sed "s/^$old //")
        for t in $tags; do
            echo "  Moving tag $t: $old -> $new"
            git tag -f "$t" "$new" >/dev/null 2>&1
        done
    fi
done
echo

if [ "$tagged_count" -eq 0 ]; then
    echo "No tags were moved (none were found on the ahead-of-upstream commits)."
else
    echo "Finished reassigning tags."
fi

echo
echo "If everything looks correct, you probably want to push branch and tags:"
echo "  git push -f origin trunk --tags"
echo
