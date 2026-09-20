#!/usr/bin/env bash
# Run a command and preserve its output under a collision-proof log name together
# with the command line, git revision, a source manifest hash, the binary hash and
# the command's real exit code.
# usage: tools/run-logged.sh <name> <command> [args...]
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
name="$1"; shift
stamp="$(date -u +%Y%m%dT%H%M%S.%NZ)-$$"
dir="$here/artifacts/logs/runs"
mkdir -p "$dir"
log="$dir/$stamp-$name.log"
while [ -e "$log" ]; do stamp="$stamp-x"; log="$dir/$stamp-$name.log"; done
manifest="$(cd "$here" && find engine plugin resources CMakeLists.txt -type f 2>/dev/null | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
testmanifest="$(cd "$here" && find tests tools -type f 2>/dev/null | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
{
    echo "# run: $name"
    echo "# date: $stamp"
    echo "# cwd: $(pwd)"
    echo "# command: $*"
    echo "# git: $(git -C "$here" rev-parse HEAD 2>/dev/null || echo none), $(git -C "$here" status --short 2>/dev/null | wc -l | tr -d ' ') dirty path(s)"
    echo "# source manifest sha256 (engine/plugin/resources/CMakeLists = binary-affecting): $manifest"
    echo "# test/tools manifest sha256: $testmanifest"
    if [ -f "$1" ]; then echo "# binary sha256: $(sha256sum "$1" | cut -d' ' -f1)"; fi
    echo "# ----"
} > "$log"
"$@" >> "$log" 2>&1
code=$?
echo "# exit code: $code" >> "$log"
echo "$code" > "$log.exit"
echo "$log (exit $code)"
exit $code
