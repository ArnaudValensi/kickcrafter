#!/usr/bin/env bash
# Run the official Steinberg validator on the staged bundle and log it with the MODULE's hash
#: the log header records the tested .so SHA-256, the validator tool hash, the
# command and the validator's real exit code.
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
validator="${KCF_VALIDATOR:-$(command -v validator || true)}"   # Steinberg VST3 SDK validator (build it from the SDK, or set KCF_VALIDATOR)
[ -n "$validator" ] && [ -x "$validator" ] || { echo "VST3 validator not found: set KCF_VALIDATOR to the Steinberg SDK validator executable" >&2; exit 2; }
bundle="${1:-artifacts/vst3/KickCrafter.vst3}"
so="$bundle/Contents/x86_64-linux/KickCrafter.so"
[ -f "$so" ] || { echo "bundle module missing: $so" >&2; exit 2; }
stamp="$(date -u +%Y%m%dT%H%M%S.%NZ)-$$"
log="artifacts/logs/runs/$stamp-validator.log"
mkdir -p artifacts/logs/runs
{
    echo "# run: validator"
    echo "# date: $stamp"
    echo "# command: $validator $bundle"
    echo "# module sha256: $(sha256sum "$so" | cut -d' ' -f1)"
    echo "# validator sha256: $(sha256sum "$validator" | cut -d' ' -f1)"
    echo "# ----"
} > "$log"
"$validator" "$bundle" >> "$log" 2>&1
code=$?
echo "# exit code: $code" >> "$log"
echo "$code" > "$log.exit"
cp "$log" artifacts/logs/validator.log
echo "$log (exit $code)"
exit $code
