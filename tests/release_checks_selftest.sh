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

# The files the checker would see in <root>, staged once in a new repository;
# fresh() gives each case its own copy of it.
base=$work/base
mkdir "$base" || exit 1
top=$(git -C "$root" rev-parse --show-toplevel 2>/dev/null || true)
if [ -n "$top" ] && [ "$(cd "$top" && pwd -P)" = "$root" ]; then
  (cd "$root" && git -c core.quotePath=false ls-files | while IFS= read -r f; do
     [ -f "$f" ] && printf '%s\n' "$f"; done | tar cf - -T -) | (cd "$base" && tar xf -)
else
  (cd "$root" && tar cf - --exclude=./.git --exclude='./build*' .) | (cd "$base" && tar xf -)
fi
(cd "$base" && g init -q && g add -A) || exit 1
t=$work/t
fresh() { rm -rf "$t" && cp -R "$base" "$t" || exit 1; }
track() { (cd "$t" && g add -f "$@") || exit 1; }
replace() { # <file> <sed expression>: edit a copied file in place, portably
  sed "$2" "$t/$1" > "$work/sed" && cat "$work/sed" > "$t/$1"
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

fresh; replace CHANGELOG.md '1a\
## validation notes
'
expect pass "a '## v' heading that is not a version is ignored" release

fresh; replace CHANGELOG.md "s/^## v$version /## v9.9.9 /"
expect fail "the changelog names another release version" release

fresh; printf '\nPre-release v%s-alpha.\n' "$version" >> "$t/CHANGELOG.md"
expect fail "a suffixed release version in the changelog" release

fresh; printf '\nPast `v%s-alpha`.\n' "$version" >> "$t/STATE.md"; track STATE.md
expect pass "the generated STATE.md may name a suffixed tag" release

fresh; printf '\n[gone](docs/missing.md)\n' >> "$t/README.md"
expect fail "a broken Markdown link" release

fresh; printf '\nRun `tools/missing_tool.sh`.\n' >> "$t/README.md"
expect fail "a repository path that does not exist" release

fresh; printf '\nSee `MISSING_REPORT.md`.\n' >> "$t/README.md"
expect fail "a named document that does not exist" release

fresh; printf 'RIFF' > "$t/capture.wav"; track capture.wav
expect fail "tracked captured audio" release

fresh; printf 'x' > "$t/notes copy.txt"; track "notes copy.txt"
expect fail "a tracked path with whitespace" release

fresh; awk 'BEGIN { for (i = 0; i < 5000; i++) print "0123456789012345678901234567890123456789012345678901234567890" }' \
         > "$t/big.txt"; track big.txt
expect fail "a tracked file over the size limit" release

fresh; printf 'ok\0binary' > "$t/blob.dat"; track blob.dat
expect fail "a tracked binary file" release

fresh; printf '/* %s@%s */\n' someone example.com >> "$t/src/config.c"
expect fail "an email address in a source file" release

fresh; printf '/* %s%s */\n' ghp_ abcdefghijklmnopqrstuvwxyz0123456789 >> "$t/src/config.c"
expect fail "an access token in a source file" release

fresh; printf '%sBEGIN RSA PRIVATE KEY%s\n' ----- ----- >> "$t/README.md"
expect fail "private key material" release

fresh; printf '\nBuilt in /%s/%s/edge-audio.\n' Users someone >> "$t/README.md"
expect fail "a home-directory path" release

fresh; replace src/version.c "s/return \"$version\"/return \"0.0.0\"/"
expect fail "src/version.c disagrees with the changelog" release

fresh; replace adapters/soundguardian/idf_component.yml "s/^version: .*/version: \"0.0.0\"/"
expect fail "the component manifest disagrees with the changelog" release

fresh; replace ".github/release-notes/v$version.md" '1s/.*/# Release/'
expect fail "the release notes heading names another version" release

fresh; replace README.md 's/spec-v[0-9.]*-/spec-v0.0-/'
expect fail "the README spec badge disagrees with SPEC.md" release

fresh; rm "$t/NOTICE"
expect fail "a missing NOTICE" release

# --- check_cited_commits.sh: a synthetic history --------------------------------
cited() { sh "$root/tools/check_cited_commits.sh" "$h"; }
h=$work/h
mkdir "$h" && cd "$h" || exit 1
g init -q && echo one > one.txt && g add one.txt && g commit -q -m one || exit 1
first=$(git rev-parse --short=7 HEAD)
first_full=$(git rev-parse HEAD)
printf 'Built at `%s`, see /commit/%s.\n' "$first" "$first_full" > NOTES.md
g add NOTES.md && g commit -q -m two || exit 1
expect pass "cited ancestor commits, short and full" cited

g checkout -q -b side && echo side > side.txt && g add side.txt && g commit -q -m side || exit 1
side=$(git rev-parse --short=7 HEAD)
side_full=$(git rev-parse HEAD)
g checkout -q - || exit 1

printf 'Also `%s`.\n' "$side" >> NOTES.md
expect fail "a cited commit that is not an ancestor" cited

g checkout -q -- NOTES.md && printf 'See %s,%s.\n' "$first" "$side" >> NOTES.md
expect fail "a non-ancestor right after another hash" cited

g checkout -q -- NOTES.md && printf 'See /commit/%s.\n' "$side_full" >> NOTES.md
expect fail "a full-length non-ancestor hash" cited

g checkout -q -- NOTES.md && printf 'Also `%s`.\n' 0b0e0f1 >> NOTES.md
expect fail "a cited hash that names no commit" cited

g checkout -q -- NOTES.md && printf 'Upstream %s.\n' 0b0e0f1 > THIRD_PARTY_NOTICES.md && g add THIRD_PARTY_NOTICES.md
expect pass "THIRD_PARTY_NOTICES.md may cite other projects' commits" cited
cd "$root" || exit 1

echo "release check self-test: $((checks - fails))/$checks passed"
[ "$fails" -eq 0 ]
