#!/usr/bin/env bash
# Open the plug-in's editor in Steinberg's own editorhost (the SDK sample whose window code is
# the closest public relative of Cubase's) and require it to stay alive: the host is started on
# the bundle, left running for N seconds with the editor window open, then killed. Still alive
# at the end = PASS (exit 0); gone before = FAIL with the host's exit code (a crash at the
# opening of the editor, which is what a tester's Cubase 11 on Windows did on 2026-09-26).
# Logged like the validator: module hash, tool hash, command, real exit code.
#   tools/editorhost.sh [--seconds N] [bundle]   default 20 s, default bundle the staged one
# editorhost: KCF_EDITORHOST, then external/editorhost/ (tools/fetch-validators.sh editorhost).
# macOS and Windows (Git Bash) only; it opens a window, so a desktop session is required.
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
seconds=20
if [ "${1:-}" = "--seconds" ]; then seconds="${2:?seconds}"; shift 2; fi
case "$(uname -s)" in
    Darwin)       module_rel="Contents/MacOS/KickCrafter"; default="external/editorhost/editorhost.app/Contents/MacOS/editorhost" ;;
    MINGW*|MSYS*) module_rel="Contents/x86_64-win/KickCrafter.vst3"; default="external/editorhost/editorhost.exe" ;;
    Linux)        module_rel="Contents/x86_64-linux/KickCrafter.so"; default="external/editorhost/editorhost" ;;
    *) echo "unsupported platform: $(uname -s)" >&2; exit 2 ;;
esac
editorhost="${KCF_EDITORHOST:-$default}"
[ -x "$editorhost" ] || { echo "editorhost not found: set KCF_EDITORHOST, or ./run fetch-editorhost (macOS and Windows)" >&2; exit 2; }
bundle="${1:-artifacts/vst3/KickCrafter.vst3}"
so="$bundle/$module_rel"
[ -f "$so" ] || { echo "bundle module missing: $so" >&2; exit 2; }
nanos="$(date -u +%N)"; case "$nanos" in *[!0-9]*|'') nanos=000000000 ;; esac
stamp="$(date -u +%Y%m%dT%H%M%S).${nanos}Z-$$"
log="artifacts/logs/runs/$stamp-editorhost.log"
mkdir -p artifacts/logs/runs
{
    echo "# run: editorhost"
    echo "# date: $stamp"
    echo "# command: $editorhost $bundle   (alive for $seconds s = PASS)"
    echo "# module sha256: $(sha256 "$so" | cut -d' ' -f1)"
    echo "# editorhost sha256: $(sha256 "$editorhost" | cut -d' ' -f1)"
    echo "# ----"
} > "$log"
"$editorhost" "$bundle" >> "$log" 2>&1 &
pid=$!
elapsed=0; code=0
while [ "$elapsed" -lt "$seconds" ]; do
    if ! kill -0 "$pid" 2>/dev/null; then wait "$pid"; code=$?; [ "$code" -eq 0 ] && code=1; break; fi   # gone early: a crash, or an exit that is one too
    sleep 1; elapsed=$((elapsed + 1))
done
if kill -0 "$pid" 2>/dev/null; then
    echo "editorhost alive after $seconds s with the editor open: PASS" >> "$log"
    kill "$pid" 2>/dev/null; sleep 1; kill -9 "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
    code=0
else
    echo "editorhost exited after $elapsed s (exit $code): FAIL" >> "$log"
fi
echo "# exit code: $code" >> "$log"
echo "$code" > "$log.exit"
cp "$log" artifacts/logs/editorhost.log
echo "$log (exit $code)"
exit $code
