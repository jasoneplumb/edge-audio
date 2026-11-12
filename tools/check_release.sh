#!/bin/sh
# Intent: release-readiness checks that need no target hardware or network:
# nothing restricted is tracked, the license files are in place, the version
# strings agree (and no suffixed form of the release version appears), and
# documentation links and repository paths resolve.
#
# A repository convention for public releases, not a SPEC.md id.
# Usage: check_release.sh <repo-root> [--tag vX.Y.Z]
#   --tag  also require the tag to be a plain vX.Y.Z naming the release version
#
# File list: `git ls-files` when <repo-root> is the top of a Git work tree;
# otherwise (a source archive) every file, minus .git/ and build*/ directories.
set -u
set -f # word-split file lists below without glob expansion
root=${1:?usage: check_release.sh <repo-root> [--tag vX.Y.Z]}
shift
tag=""
while [ $# -gt 0 ]; do
  case $1 in
    --tag) tag=${2:?--tag needs a value}; shift 2 ;;
    *) echo "check_release: unknown argument: $1" >&2; exit 2 ;;
  esac
done
cd "$root" || exit 2
fail=0
err() { echo "check_release: $*"; fail=1; }

semver='[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*'
max_bytes=262144
self=tools/check_release.sh

top=$(git rev-parse --show-toplevel 2>/dev/null || true)
if [ -n "$top" ] && [ "$(cd "$top" && pwd -P)" = "$(pwd -P)" ]; then
  # A file deleted in the work tree but not yet committed is still listed.
  files=$(git -c core.quotePath=false ls-files | while IFS= read -r f; do
            [ -f "$f" ] && printf '%s\n' "$f"; done)
else
  files=$(find . -name .git -prune -o -type d -name 'build*' -prune -o -type f -print \
          | sed 's|^\./||' | sort)
fi
[ -n "$files" ] || { echo "check_release: no files found under $root"; exit 1; }

# --- 1. Nothing restricted, generated or binary is tracked -------------------
# Captured audio, recordings, build output, archives, keys, and ESP-IDF /
# PlatformIO state that belongs to a local checkout.
restricted=$(printf '%s\n' "$files" | while IFS= read -r f; do
  base=${f##*/}
  lower=$(printf '%s' "$base" | tr 'A-Z' 'a-z')
  bad=""
  case $lower in (*.wav|*.pcm|*.raw|*.earc|*.flac|*.mp3|*.ogg|*.opus|*.m4a|*.aac) bad=audio ;; esac
  case $lower in (*.bin|*.elf|*.map|*.o|*.obj|*.a|*.so|*.dylib|*.dll|*.exe) bad="build output" ;; esac
  case $lower in (*.zip|*.tar|*.gz|*.tgz|*.xz|*.7z) bad=archive ;; esac
  case $lower in (*.pem|*.key|*.p12|*.pfx|*.der|*.jks|*.keystore|.env|id_rsa*|id_ed25519*) bad=credential ;; esac
  case $lower in (sdkconfig|sdkconfig.old|dependencies.lock|junit.xml) bad="local build state" ;; esac
  [ -z "$bad" ] || echo "check_release: $bad file is tracked: $f"
  case /$f in
    (*/build/*|*/managed_components/*|*/.pio/*)
      echo "check_release: build or dependency directory is tracked: $f" ;;
  esac
  size=$(wc -c < "$f" | tr -d ' ')
  [ "$size" -le "$max_bytes" ] || echo "check_release: $f is $size bytes (limit $max_bytes)"
  if [ "$size" -gt 0 ] && ! grep -Iq . "$f"; then
    echo "check_release: binary file is tracked: $f"
  fi
done)
if [ -n "$restricted" ]; then printf '%s\n' "$restricted"; fail=1; fi

# Credentials, personal addresses and local paths in tracked text (grep -I skips
# binaries). The patterns are written so that this script does not match itself.
scan() { # <description> <extended regex> [<regex of allowed matches>]
  hits=$(printf '%s\n' "$files" | grep -vxF "$self" | tr '\n' '\0' \
         | xargs -0 grep -I -n -o -E -e "$2" -- 2>/dev/null)
  [ -n "${3:-}" ] && hits=$(printf '%s\n' "$hits" | grep -v -E -e "$3")
  if [ -n "$hits" ]; then
    err "$1:"
    printf '%s\n' "$hits" | sed 's/^/  /'
  fi
}
scan "private key material" '-----BEGIN [A-Z ]*PRIVATE KEY-----'
scan "access token" '(ghp_|gho_|ghs_|github_pat_)[A-Za-z0-9_]{20,}|AKIA[0-9A-Z]{16}|sk-ant-[A-Za-z0-9_-]{20,}|xox[abprs]-[A-Za-z0-9-]{10,}'
scan "email address" '[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}' \
     ':(noreply@anthropic\.com|noreply@github\.com)$'
scan "absolute home-directory path" '/(Users|home)/[A-Za-z][A-Za-z0-9._-]*'

grep -qx 'build/' .gitignore 2>/dev/null || err ".gitignore does not exclude build/"

# --- 2. License files ---------------------------------------------------------
require() { # <file> <fixed string>
  if [ ! -f "$1" ]; then err "$1 is missing"; return; fi
  grep -qF -e "$2" "$1" || err "$1 does not contain '$2'"
}
require LICENSE "Apache License"
require LICENSE "Version 2.0, January 2004"
require LICENSE "END OF TERMS AND CONDITIONS"
require NOTICE "Licensed under the Apache License, Version 2.0"
require NOTICE "Copyright"
require THIRD_PARTY_NOTICES.md "ESP-IDF"
require THIRD_PARTY_NOTICES.md "M5Unified"

# --- 3. Version strings -------------------------------------------------------
# The release version is the newest released CHANGELOG entry ("## vX.Y.Z — date";
# an "## Unreleased" section may sit above it).
version=$(sed -n 's/^## v\([^ ]*\).*/\1/p' CHANGELOG.md 2>/dev/null | head -n 1)
if ! printf '%s\n' "$version" | grep -qx "$semver"; then
  err "newest CHANGELOG.md release heading is 'v$version', not a plain vX.Y.Z"
else
  grep -q "^## v$version — [0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\$" CHANGELOG.md \
    || err "CHANGELOG.md heading for v$version lacks its date (## v$version — YYYY-MM-DD)"
  grep -qF "return \"$version\";" src/version.c \
    || err "src/version.c does not report \"$version\""
  grep -qx "version: \"$version\"" adapters/soundguardian/idf_component.yml \
    || err "adapters/soundguardian/idf_component.yml does not declare version \"$version\""
  grep -qF "\`v$version\`" README.md || err "README.md does not name \`v$version\`"
  notes=.github/release-notes/v$version.md
  if [ ! -s "$notes" ]; then
    err "$notes is missing or empty"
  else
    [ "$(head -n 1 "$notes")" = "# v$version" ] || err "$notes does not start with '# v$version'"
  fi
  # No suffixed form (v0.1.0-alpha, 0.1.0-rc1, ...) of the release version anywhere.
  vre=$(printf '%s' "$version" | sed 's/\./\\./g')
  scan "suffixed form of release version $version" "$vre-[0-9A-Za-z]"
  if [ -n "$tag" ]; then
    printf '%s\n' "$tag" | grep -qx "v$semver" || err "tag '$tag' is not a plain vX.Y.Z"
    [ "$tag" = "v$version" ] || err "tag '$tag' does not match the release version v$version"
  fi
fi
# The README's spec version matches SPEC.md's status line.
spec=$(sed -n 's/^> - \*\*Status:\*\* v\([0-9][0-9.]*\).*/\1/p' SPEC.md 2>/dev/null | head -n 1)
if [ -z "$spec" ]; then
  err "SPEC.md has no '> - **Status:** vX.Y' line"
else
  grep -qF "spec-v$spec-" README.md || err "README.md spec badge does not show v$spec"
  grep -qF "(v$spec)" README.md || err "README.md does not cite SPEC.md (v$spec)"
fi

# --- 4. Documentation references resolve --------------------------------------
md_files=$(printf '%s\n' "$files" | grep '\.md$')
for md in $md_files; do
  dir=$(dirname "$md")
  # Inline links: [text](target)
  for target in $(grep -o -E '\]\([^) ]+\)' "$md" | sed 's/^](//; s/)$//'); do
    case $target in http://*|https://*|mailto:*|'#'*) continue ;; esac
    path=${target%%#*}
    [ -e "$dir/$path" ] || err "$md: broken link ($target)"
  done
  # Repository paths in prose: src/..., include/..., adapters/..., tools/..., tests/..., .github/...
  for p in $(grep -o -E '(^|[^A-Za-z0-9_./-])(src|include|adapters|tools|tests|\.github)/[A-Za-z0-9_./-]*' "$md" \
               | sed -E 's|^[^A-Za-z0-9_./-]||; s|\.$||'); do
    [ -e "$p" ] || [ -e "$p.c" ] || [ -e "$p.h" ] || err "$md: path does not exist ($p)"
  done
  # Root documents named in backticks: `NAME.md`
  for d in $(grep -o -E '`[A-Za-z0-9_-]+\.md`' "$md" | tr -d '`'); do
    [ -e "$d" ] || err "$md: names a document that does not exist ($d)"
  done
done

[ "$fail" -eq 0 ] && echo "check_release: OK (release version v$version, $(printf '%s\n' "$files" | wc -l | tr -d ' ') files)"
exit "$fail"
