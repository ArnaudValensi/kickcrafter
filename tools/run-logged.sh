#!/usr/bin/env bash
# Run a command and preserve its output under a collision-proof log name together
# with the command line, git revision, a source manifest hash, the binary hash and
# the command's real exit code.
# usage: tools/run-logged.sh <name> <command> [args...]
# CI runs this on Linux, macOS (bash 3.2, no sha256sum, no %N in date) and Git Bash on Windows.
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
name="$1"; shift
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
# Same lines as `xargs sha256sum` (hash, two spaces, path), one file per line, hashed once more.
manifest_of() { find "$@" -type f 2>/dev/null | LC_ALL=C sort | while IFS= read -r f; do sha256 "$f"; done | sha256 | cut -d' ' -f1; }
nanos="$(date -u +%N)"; case "$nanos" in *[!0-9]*|'') nanos=000000000 ;; esac   # BSD date has no %N
stamp="$(date -u +%Y%m%dT%H%M%S).${nanos}Z-$$"
dir="$here/artifacts/logs/runs"
mkdir -p "$dir"
log="$dir/$stamp-$name.log"
while [ -e "$log" ]; do stamp="$stamp-x"; log="$dir/$stamp-$name.log"; done
manifest="$(cd "$here" && manifest_of engine plugin resources CMakeLists.txt)"
testmanifest="$(cd "$here" && manifest_of tests tools)"
{
    echo "# run: $name"
    echo "# date: $stamp"
    echo "# cwd: $(pwd)"
    echo "# command: $*"
    echo "# git: $(git -C "$here" rev-parse HEAD 2>/dev/null || echo none), $(git -C "$here" status --short 2>/dev/null | wc -l | tr -d ' ') dirty path(s)"
    echo "# source manifest sha256 (engine/plugin/resources/CMakeLists = binary-affecting): $manifest"
    echo "# test/tools manifest sha256: $testmanifest"
    if [ -f "$1" ]; then echo "# binary sha256: $(sha256 "$1" | cut -d' ' -f1)"; fi
    echo "# ----"
} > "$log"
"$@" >> "$log" 2>&1
code=$?
echo "# exit code: $code" >> "$log"
echo "$code" > "$log.exit"
echo "$log (exit $code)"
exit $code
