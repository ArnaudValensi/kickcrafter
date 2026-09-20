#!/usr/bin/env bash
# Run every host stage in order on the staged bundle; stop at the first failing stage.
# Each stage's exit code is recorded in artifacts/reaper/stage-status.txt by the runner.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
runner="$here/tools/reaper/run_reaper_validation.sh"
log="$here/artifacts/reaper/all-stages.log"
mkdir -p "$here/artifacts/reaper"
echo "all-stages start $(date -u +%FT%TZ) bundle $(sha256sum "$here/artifacts/vst3/KickCrafter Fable.vst3/Contents/x86_64-linux/KickCrafter Fable.so" | cut -c1-16)" > "$log"
[ "${1:-setup}" = "setup" ] && "$runner" stop >> "$log" 2>&1
from="${1:-setup}"; started=0
for stage in setup analyze editor menus touch read lifecycle panic program resize captures; do
    [ "$stage" = "$from" ] && started=1
    [ "$started" = 1 ] || continue
    echo "===== $stage $(date -u +%T)" >> "$log"
    if "$runner" "$stage" >> "$log" 2>&1; then echo "stage $stage: PASS" >> "$log"; else
        echo "stage $stage: FAIL (exit $?)" >> "$log"; echo "all-stages STOPPED at $stage" >> "$log"
        # keep the failed run's log: all-stages.log is overwritten by the next run
        cp "$log" "$here/artifacts/reaper/all-stages-$(date -u +%Y%m%dT%H%M%SZ)-$stage-fail.log"
        exit 1
    fi
done
echo "all-stages COMPLETE $(date -u +%FT%TZ)" >> "$log"
