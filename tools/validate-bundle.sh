#!/usr/bin/env bash
# Run the official Steinberg validator on a VST3 bundle and log it with the MODULE's hash: the log
# header records the tested module's SHA-256, the validator tool hash, the command and the
# validator's real exit code. Works on Linux, macOS (bash 3.2, no sha256sum, no %N) and Git Bash
# on Windows: the module inside the bundle is found by platform.
#   tools/validate-bundle.sh [bundle]   default: the staged artifacts/vst3/KickCrafter.vst3
# The validator: KCF_VALIDATOR, then `validator` on PATH, then external/validator/ (fetched by
# tools/fetch-validators.sh validator).
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
case "$(uname -s)" in
    Linux)        module_rel="Contents/x86_64-linux/KickCrafter.so"; exe="" ;;
    Darwin)       module_rel="Contents/MacOS/KickCrafter"; exe="" ;;
    MINGW*|MSYS*) module_rel="Contents/x86_64-win/KickCrafter.vst3"; exe=".exe" ;;
    *) echo "unsupported platform: $(uname -s)" >&2; exit 2 ;;
esac
validator="${KCF_VALIDATOR:-$(command -v validator || true)}"
[ -n "$validator" ] || validator="external/validator/validator$exe"
[ -x "$validator" ] || { echo "VST3 validator not found: set KCF_VALIDATOR, or ./run fetch-validator" >&2; exit 2; }
bundle="${1:-artifacts/vst3/KickCrafter.vst3}"
so="$bundle/$module_rel"
[ -f "$so" ] || { echo "bundle module missing: $so" >&2; exit 2; }
nanos="$(date -u +%N)"; case "$nanos" in *[!0-9]*|'') nanos=000000000 ;; esac   # BSD date has no %N
stamp="$(date -u +%Y%m%dT%H%M%S).${nanos}Z-$$"
log="artifacts/logs/runs/$stamp-validator.log"
mkdir -p artifacts/logs/runs
{
    echo "# run: validator"
    echo "# date: $stamp"
    echo "# command: $validator $bundle"
    echo "# module sha256: $(sha256 "$so" | cut -d' ' -f1)"
    echo "# validator sha256: $(sha256 "$validator" | cut -d' ' -f1)"
    echo "# ----"
} > "$log"
"$validator" "$bundle" >> "$log" 2>&1
code=$?
echo "# exit code: $code" >> "$log"
echo "$code" > "$log.exit"
cp "$log" artifacts/logs/validator.log
echo "$log (exit $code)"
exit $code
