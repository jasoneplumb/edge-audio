#!/bin/sh
# Intent: failure-path tests for tools/check_release.sh and
# tools/check_cited_commits.sh: the unmodified tree passes, and each planted
# defect makes its checker fail, so a regression in a checker cannot pass
# silently.
#
# A repository convention for public releases, not a SPEC.md id.
# Usage: release_checks_selftest.sh <repo-root>    (exits 77, skipped, without git)
set -u
root=$(cd "${1:?usage: release_checks_selftest.sh <repo-root>}" && pwd -P) || exit 2
command -v git > /dev/null 2>&1 || { echo "git not available; skipped"; exit 77; }
work=$(mktemp -d "${TMPDIR:-/tmp}/ea_release_selftest.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
trap 'exit 1' INT TERM

# Scratch repositories must not pick up the user's signing, hooks or identity.
g() { git -c user.name=selftest -c user.email=selftest -c commit.gpgsign=false \
          -c core.hooksPath=/dev/null "$@"; }

checks=0
fails=0
expect() { # <pass|fail> <description> <command...>
  want=$1; what=$2; shift 2
  checks=$((checks + 1))
  if "$@" > "$work/out" 2>&1; then got=pass; else got=fail; fi
  if [ "$got" = "$want" ]; then
    echo "ok - $what"
  else
    echo "not ok - $what (expected $want, got $got)"
    sed 's/^/  | /' "$work/out"
    fails=$((fails + 1))
  fi
}

# A copy of the files the checker would see in <root>, staged in a new repository.
fresh() {
  t=$work/t
  rm -rf "$t" && mkdir "$t" || exit 1
  top=$(git -C "$root" rev-parse --show-toplevel 2>/dev/null || true)
  if [ -n "$top" ] && [ "$(cd "$top" && pwd -P)" = "$root" ]; then
    (cd "$root" && git -c core.quotePath=false ls-files | while IFS= read -r f; do
       [ -f "$f" ] && printf '%s\n' "$f"; done | tar cf - -T -) | (cd "$t" && tar xf -)
  else
    (cd "$root" && tar cf - --exclude=./.git --exclude='./build*' .) | (cd "$t" && tar xf -)
  fi
  (cd "$t" && g init -q && g add -A) || exit 1
}
release() { sh "$root/tools/check_release.sh" "$t" "$@"; }
version=$(sed -n 's/.*return "\([^"]*\)";.*/\1/p' "$root/src/version.c")

# --- check_release.sh -----------------------------------------------------------
fresh
expect pass "unmodified tree passes" release
expect pass "the release tag passes" release --tag "v$version"
expect fail "a suffixed tag is rejected" release --tag "v$version-alpha"
expect fail "a tag for another version is rejected" release --tag "v9.9.9"

fresh; printf '\nSee v%s-rc1.\n' "$version" >> "$t/README.md"
expect fail "a suffixed release version in the README" release

fresh; printf '\n[gone](docs/missing.md)\n' >> "$t/README.md"
expect fail "a broken Markdown link" release

fresh; printf '\nRun `tools/missing_tool.sh`.\n' >> "$t/README.md"
expect fail "a repository path that does not exist" release

fresh; printf 'RIFF' > "$t/capture.wav"; (cd "$t" && g add -f capture.wav)
expect fail "tracked captured audio" release

fresh; printf '/* %s@%s */\n' someone example.com >> "$t/src/config.c"
expect fail "an email address in a source file" release

fresh; sed "s/return \"$version\"/return \"0.0.0\"/" "$root/src/version.c" > "$t/src/version.c"
expect fail "src/version.c disagrees with the changelog" release

fresh; rm "$t/NOTICE"
expect fail "a missing NOTICE" release

# --- check_cited_commits.sh: a synthetic history --------------------------------
h=$work/h
mkdir "$h" && cd "$h" || exit 1
g init -q && echo one > one.txt && g add one.txt && g commit -q -m one || exit 1
first=$(git rev-parse --short=7 HEAD)
printf 'Built at `%s`.\n' "$first" > NOTES.md && g add NOTES.md && g commit -q -m two || exit 1
expect pass "a cited ancestor commit" sh "$root/tools/check_cited_commits.sh" "$h"

g checkout -q -b side && echo side > side.txt && g add side.txt && g commit -q -m side || exit 1
side=$(git rev-parse --short=7 HEAD)
g checkout -q - || exit 1
printf 'Also `%s`.\n' "$side" >> NOTES.md
expect fail "a cited commit that is not an ancestor" sh "$root/tools/check_cited_commits.sh" "$h"

g checkout -q -- NOTES.md && printf 'Also `%s`.\n' 0b0e0f1 >> NOTES.md
expect fail "a cited hash that names no commit" sh "$root/tools/check_cited_commits.sh" "$h"
cd "$root" || exit 1

echo "release check self-test: $((checks - fails))/$checks passed"
[ "$fails" -eq 0 ]
