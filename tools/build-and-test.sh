#!/usr/bin/env bash
# Build + test chain (single compiler job). Long: run it inside tmux or another persistent shell.
#
# Per-run evidence: configure and build logs are fresh files for THIS run,
# their exit statuses are carried explicitly, nothing is staged and no build record is written
# unless configure, build and the test run all succeeded, and the chain's exit code is the real
# outcome (never the status of a trailing echo). The build record binds the staged module and the
# test executable to the application manifest (production sources), the dependency revision and a
# preserved copy of the CMake configuration; the tests/tools manifest is recorded for information.
set -uo pipefail
export CMAKE_BUILD_PARALLEL_LEVEL=1
here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
status="artifacts/logs/chain-status.txt"
cfg_log="artifacts/logs/runs/$stamp-configure.log"
build_log="artifacts/logs/runs/$stamp-build.log"
mkdir -p artifacts/logs/runs
echo "chain start $stamp" > "$status"
manifest() { find engine plugin resources CMakeLists.txt -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1; }
testmanifest() { find tests tools -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1; }
manifest_before="$(manifest)"
testmanifest_before="$(testmanifest)"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > "$cfg_log" 2>&1   # creates build/ on a fresh checkout
cfg_exit=$?
echo "configure exit=$cfg_exit ($cfg_log)" >> "$status"
if [ "$cfg_exit" -ne 0 ]; then echo "chain FAILED (configure) $(date -u +%FT%TZ)" >> "$status"; exit 1; fi

( cd build && ninja -j1 -k 0 kcf_plugin_tests kcf_engine_tests KickCrafterFable_VST3 ) > "$build_log" 2>&1
build_exit=$?
echo "build exit=$build_exit ($build_log)" >> "$status"
# Compatibility copies for readers of the old fixed paths (always fresh for this run).
cp "$cfg_log" artifacts/logs/configure.log; cp "$build_log" artifacts/logs/build-plugin.log
if [ "$build_exit" -ne 0 ]; then echo "chain FAILED (build) $(date -u +%FT%TZ)" >> "$status"; exit 1; fi
[ -x build/tests/kcf_plugin_tests ] || { echo "chain FAILED (no test executable) $(date -u +%FT%TZ)" >> "$status"; exit 1; }

tools/xvfb-display.sh >/dev/null 2>&1 || { echo "chain FAILED (test display :104 unavailable) $(date -u +%FT%TZ)" >> "$status"; exit 1; }
test_line="$(DISPLAY=:104 tools/run-logged.sh plugin-tests ./build/tests/kcf_plugin_tests)"
test_exit=$?
echo "$test_line" >> "$status"
if [ "$test_exit" -ne 0 ]; then echo "chain FAILED (tests exit $test_exit) $(date -u +%FT%TZ)" >> "$status"; exit 1; fi
test_log="${test_line%% (exit*}"

manifest_after="$(manifest)"
testmanifest_after="$(testmanifest)"
if [ "$manifest_before" != "$manifest_after" ]; then
    echo "chain FAILED (sources changed during the run) $(date -u +%FT%TZ)" >> "$status"; exit 1
fi

mkdir -p artifacts/reaper
DISPLAY=:104 ./build/tests/kcf_plugin_tests --layout 100 > artifacts/reaper/layout-100.txt 2>&1 || { echo "chain FAILED (layout dump) $(date -u +%FT%TZ)" >> "$status"; exit 1; }
mkdir -p artifacts/vst3 && rm -rf "artifacts/vst3/KickCrafter Fable.vst3" && cp -r "build/KickCrafterFable_artefacts/Release/VST3/KickCrafter Fable.vst3" artifacts/vst3/ || { echo "chain FAILED (staging) $(date -u +%FT%TZ)" >> "$status"; exit 1; }
so="artifacts/vst3/KickCrafter Fable.vst3/Contents/x86_64-linux/KickCrafter Fable.so"
sha256sum "$so" >> "$status"
cp build/CMakeCache.txt "artifacts/logs/runs/$stamp-CMakeCache.txt"
{
    echo "build-record $stamp"
    echo "git: $(git rev-parse HEAD 2>/dev/null || echo none)"
    echo "manifest-before-build: $manifest_before"
    echo "manifest-after-build: $manifest_after"
    echo "tests-tools-manifest: $testmanifest_after"
    echo "sources-unchanged-during-build: yes"
    echo "juce: $(git -C external/JUCE rev-parse HEAD)"
    echo "juce-dirty-paths: $(git -C external/JUCE status --porcelain | wc -l | tr -d ' ')"
    echo "cmake-cache-copy: artifacts/logs/runs/$stamp-CMakeCache.txt"
    echo "cmake-cache: $(sha256sum build/CMakeCache.txt | cut -d' ' -f1)"
    echo "configure-log: $cfg_log"
    echo "build-log: $build_log"
    echo "vst3-so: $(sha256sum "$so" | cut -d' ' -f1)"
    echo "test-exe: $(sha256sum build/tests/kcf_plugin_tests | cut -d' ' -f1)"
    echo "plugin-test-log: $test_log"
} > artifacts/logs/build-record.txt
cp artifacts/logs/build-record.txt "artifacts/logs/runs/$stamp-build-record.txt"
echo "chain done $(date -u +%FT%TZ)" >> "$status"
exit 0
