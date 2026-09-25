#!/usr/bin/env bash
# Run Tracktion's pluginval on a VST3 bundle (the editor, the parameters, block-size and
# sample-rate changes, state, threads) and log it like the Steinberg validator: module hash,
# tool hash, command, real exit code. Linux, macOS (bash 3.2) and Git Bash on Windows.
#   tools/pluginval.sh [--level N] [bundle]   level 1..10 (default 5: above it, timing-sensitive
#                                             checks fail on loaded machines); default bundle: the
#                                             staged artifacts/vst3/KickCrafter.vst3
# pluginval: KCF_PLUGINVAL, then `pluginval` on PATH, then external/pluginval/ (fetched by
# tools/fetch-validators.sh pluginval). pluginval opens the editor: on a headless Linux, run under
# xvfb-run or with DISPLAY set. Its own report lands under artifacts/logs/pluginval/.
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
level=5
if [ "${1:-}" = "--level" ]; then level="${2:?level}"; shift 2; fi
case "$(uname -s)" in
    Linux)        module_rel="Contents/x86_64-linux/KickCrafter.so"; default="external/pluginval/pluginval" ;;
    Darwin)       module_rel="Contents/MacOS/KickCrafter"; default="external/pluginval/pluginval.app/Contents/MacOS/pluginval" ;;
    MINGW*|MSYS*) module_rel="Contents/x86_64-win/KickCrafter.vst3"; default="external/pluginval/pluginval.exe" ;;
    *) echo "unsupported platform: $(uname -s)" >&2; exit 2 ;;
esac
pluginval="${KCF_PLUGINVAL:-$(command -v pluginval || true)}"
[ -n "$pluginval" ] || pluginval="$default"
[ -x "$pluginval" ] || { echo "pluginval not found: set KCF_PLUGINVAL, or ./run fetch-pluginval" >&2; exit 2; }
bundle="${1:-artifacts/vst3/KickCrafter.vst3}"
so="$bundle/$module_rel"
[ -f "$so" ] || { echo "bundle module missing: $so" >&2; exit 2; }
nanos="$(date -u +%N)"; case "$nanos" in *[!0-9]*|'') nanos=000000000 ;; esac
stamp="$(date -u +%Y%m%dT%H%M%S).${nanos}Z-$$"
log="artifacts/logs/runs/$stamp-pluginval.log"
mkdir -p artifacts/logs/runs artifacts/logs/pluginval
{
    echo "# run: pluginval"
    echo "# date: $stamp"
    echo "# command: $pluginval --strictness-level $level --validate-in-process --output-dir artifacts/logs/pluginval --validate $bundle"
    echo "# module sha256: $(sha256 "$so" | cut -d' ' -f1)"
    echo "# pluginval sha256: $(sha256 "$pluginval" | cut -d' ' -f1)"
    echo "# ----"
} > "$log"
"$pluginval" --strictness-level "$level" --validate-in-process --output-dir artifacts/logs/pluginval --validate "$bundle" >> "$log" 2>&1
code=$?
echo "# exit code: $code" >> "$log"
echo "$code" > "$log.exit"
cp "$log" artifacts/logs/pluginval.log
echo "$log (exit $code)"
exit $code
