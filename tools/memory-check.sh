#!/usr/bin/env bash
# Memory-leak gate (docs/development.md). Two passes, every run logged with real exit codes, tool
# identity, options, source manifests and the exact binary hashes (artifacts/logs/memory/).
#
#   pass A (default)   standalone LeakSanitizer preloaded into the ORDINARY build's test executables
#                      and into the real VST3 host driving the STAGED (delivered) module and the
#                      Steinberg validator: no instrumented rebuild, delivered code paths.
#   pass B (--diag)    the diagnostic ASan/LSan build in build-leak (KCF_SANITIZE_PLUGIN=ON): the same
#                      programs with compiled-in ASan instrumentation of this project's own sources (engine,
#                      plugin, tests); JUCE module TUs are compiled -O1 -g1 uninstrumented (an instrumented
#                      juce_gui_basics TU was OOM-killed on the shared machine) and the ASan/LSan runtime
#                      is linked into every executable and the module (leak detection is process-wide).
#
# Each pass runs the leak-free controls AND the intentional-leak negative controls (which must fail
# with a LeakSanitizer report). Exit 0 only if every leak-free run reports no leaks and every
# negative control is detected. Usage: tools/memory-check.sh [--diag] [--cycles N] [--display :N]
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$here"
mode="A"; cycles=""; display="${KCF_DISPLAY:-:104}"
while [ $# -gt 0 ]; do
    case "$1" in
        --diag) mode="B" ;;
        --cycles) cycles="$2"; shift ;;
        --display) display="$2"; shift ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
    shift
done
export DISPLAY="$display"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
outdir="artifacts/logs/memory/$stamp-pass$mode"
mkdir -p "$outdir"
validator="${KCF_VALIDATOR:-$(command -v validator || true)}"   # Steinberg VST3 SDK validator (build it from the SDK, or set KCF_VALIDATOR)
[ -n "$validator" ] && [ -x "$validator" ] || { echo "VST3 validator not found: set KCF_VALIDATOR to the Steinberg SDK validator executable" >&2; exit 2; }
staged_so="artifacts/vst3/KickCrafter.vst3/Contents/x86_64-linux/KickCrafter.so"
if [ "$mode" = "A" ]; then
    bdir="build"; module="$staged_so"; module_kind="delivered (staged) module, uninstrumented"
    lsan_so="$(gcc -print-file-name=liblsan.so)"
    [ -f "$lsan_so" ] || { echo "liblsan.so not found"; exit 2; }
    preload="LD_PRELOAD=$lsan_so"
else
    bdir="build-leak"; module="$bdir/KickCrafter_artefacts/Release/VST3/KickCrafter.vst3/Contents/x86_64-linux/KickCrafter.so"
    module_kind="diagnostic ASan/LSan build (build-leak): own sources instrumented, JUCE -O1 -g1 uninstrumented, sanitizer runtime linked"
    preload=""
fi
suppressions="tools/lsan-suppressions.txt"
common_opts="detect_leaks=1:fast_unwind_on_malloc=0:malloc_context_size=30:print_suppressions=1:report_objects=0"
[ -f "$suppressions" ] && common_opts="$common_opts:suppressions=$suppressions"
export LSAN_OPTIONS="$common_opts"
export ASAN_OPTIONS="$common_opts:abort_on_error=0:detect_odr_violation=0"
failures=0
manifest="$(find engine plugin resources CMakeLists.txt -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
testmanifest="$(find tests tools -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
{
    echo "memory-check pass $mode $stamp"
    echo "git: $(git rev-parse HEAD 2>/dev/null || echo none), $(git status --short | wc -l | tr -d ' ') dirty path(s)"
    echo "application manifest: $manifest"
    echo "tests/tools manifest: $testmanifest"
    echo "tool: $(gcc --version | head -1) AddressSanitizer/LeakSanitizer runtime $(gcc -print-file-name=libasan.so) / $(gcc -print-file-name=liblsan.so)"
    echo "LSAN_OPTIONS=$LSAN_OPTIONS"
    echo "preload: ${preload:-none (compiled-in instrumentation)}"
    echo "module under test: $module ($module_kind)"
    echo "module sha256: $(sha256sum "$module" | cut -d' ' -f1)"
    echo "kcf_leak_tests sha256: $(sha256sum "$bdir/tests/kcf_leak_tests" | cut -d' ' -f1)"
    echo "kcf_vst3_host sha256: $(sha256sum "$bdir/tests/kcf_vst3_host" | cut -d' ' -f1)"
    echo "validator: $validator sha256 $(sha256sum "$validator" | cut -d' ' -f1)"
    echo "display: $display"
    [ -f "$suppressions" ] && { echo "suppressions file:"; sed 's/^/    /' "$suppressions"; }
} > "$outdir/identity.txt"
cat "$outdir/identity.txt"

# run <name> <expect: clean|leak> <command...>
run() {
    local name="$1" expect="$2"; shift 2
    local log="$outdir/$name.log" code verdict
    echo "# command: $*" > "$log"
    echo "# expect: $expect" >> "$log"
    ( eval "$preload" "$@" ) >> "$log" 2>&1
    code=$?
    echo "# exit code: $code" >> "$log"
    echo "$code" > "$log.exit"
    local leaks
    if grep -q "ERROR: LeakSanitizer: detected memory leaks" "$log"; then leaks=yes; else leaks=no; fi
    local summary; summary="$(grep -h "SUMMARY: .*Sanitizer" "$log" | tail -1)"
    if [ "$expect" = "clean" ]; then
        if [ "$leaks" = "no" ] && [ "$code" -eq 0 ]; then verdict="PASS (no leaks, exit 0)"; else verdict="FAIL (leaks=$leaks, exit $code)"; failures=$((failures + 1)); fi
    else
        if [ "$leaks" = "yes" ] && [ "$code" -ne 0 ]; then verdict="PASS (negative control detected, exit $code)"; else verdict="FAIL (negative control NOT detected: leaks=$leaks, exit $code)"; failures=$((failures + 1)); fi
    fi
    echo "$name: $verdict ${summary:+| $summary}" | tee -a "$outdir/summary.txt"
}

cyc=""; [ -n "$cycles" ] && cyc="--cycles $cycles"
run leak-tests            clean "$bdir/tests/kcf_leak_tests $cyc"
run leak-tests-negative   leak  "$bdir/tests/kcf_leak_tests --cycles 2 --intentional-leak"
run vst3-host             clean "$bdir/tests/kcf_vst3_host ${cycles:+--cycles $cycles} '$module'"
run vst3-host-no-view     clean "$bdir/tests/kcf_vst3_host --cycles 3 --no-view '$module'"
run vst3-host-negative    leak  "$bdir/tests/kcf_vst3_host --cycles 1 --intentional-leak '$module'"
if [ "$mode" = "A" ]; then
    # Steinberg validator (uninstrumented host binary) with the standalone leak checker preloaded:
    # host-owned allocations are attributed by module in the report (see the analysis step).
    run validator-preload clean "'$validator' 'artifacts/vst3/KickCrafter.vst3'"
else
    # The diagnostic module links libasan; an uninstrumented host can load it only with the ASan
    # runtime preloaded (ASan must be the first library in the process).
    asan_so="$(gcc -print-file-name=libasan.so)"
    run validator-diag    clean "LD_PRELOAD=$asan_so '$validator' '$bdir/KickCrafter_artefacts/Release/VST3/KickCrafter.vst3'"
fi
# Attribution helper: which modules appear in reported leak stacks (if any).
for f in "$outdir"/*.log; do
    if grep -q "detected memory leaks" "$f"; then
        echo "--- leak stack modules in $(basename "$f"):" >> "$outdir/summary.txt"
        grep -o "(/[^)+]*" "$f" | sort | uniq -c | sort -rn | head -12 >> "$outdir/summary.txt"
    fi
done
echo "memory-check pass $mode: $failures failure(s); evidence in $outdir" | tee -a "$outdir/summary.txt"
[ "$failures" -eq 0 ]
