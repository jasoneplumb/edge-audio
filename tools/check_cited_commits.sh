#!/bin/sh
# Intent: every commit hash cited in the tracked Markdown documents names a
# commit that is an ancestor of HEAD, so a history rewrite cannot leave a
# document pointing at a commit that is no longer published.
#
# A repository convention for public releases, not a SPEC.md id.
# Usage: check_cited_commits.sh <repo-root>
# A cited hash is a 7-character lowercase hex token with at least one digit
# and one letter. Exits 77 (skipped) outside a Git work tree or in a shallow
# clone, where ancestry cannot be decided.
set -u
set -f
root=${1:?usage: check_cited_commits.sh <repo-root>}
cd "$root" || exit 2

top=$(git rev-parse --show-toplevel 2>/dev/null || true)
if [ -z "$top" ] || [ "$(cd "$top" && pwd -P)" != "$(pwd -P)" ]; then
  echo "check_cited_commits: $root is not the top of a Git work tree; skipped"
  exit 77
fi
if [ "$(git rev-parse --is-shallow-repository)" = true ]; then
  echo "check_cited_commits: shallow clone; skipped (fetch the full history to run it)"
  exit 77
fi

fail=0
cited=0
for md in $(git -c core.quotePath=false ls-files '*.md'); do
  [ -f "$md" ] || continue
  for h in $(grep -o -E '(^|[^0-9A-Za-z])[0-9a-f]{7}([^0-9A-Za-z]|$)' "$md" | tr -cd '0-9a-f\n'); do
    case $h in *[a-f]*) ;; *) continue ;; esac
    case $h in *[0-9]*) ;; *) continue ;; esac
    cited=$((cited + 1))
    if ! full=$(git rev-parse -q --verify "$h^{commit}" 2>/dev/null); then
      echo "check_cited_commits: $md cites $h, which names no commit in this repository"
      fail=1
    elif ! git merge-base --is-ancestor "$full" HEAD; then
      echo "check_cited_commits: $md cites $h, which is not an ancestor of HEAD"
      fail=1
    fi
  done
done

[ "$fail" -eq 0 ] && echo "check_cited_commits: OK ($cited citations, all ancestors of HEAD)"
exit "$fail"
