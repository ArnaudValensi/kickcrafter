#!/usr/bin/env bash
# Delivery correspondence preflight. Exits 0 only when:
#   - every archived path (production sources AND documents) is committed (clean),
#   - the build record exists, its sources were unchanged during the build and its application
#     manifest equals the current production sources (engine/ plugin/ resources/ CMakeLists.txt): the
#     binary is bound to what produced it, nothing else (a test, tool or doc edit does not demand a
#     new build; validate what such an edit affects, see docs/development.md "Conventions"),
#   - the recorded dependency revision equals the pinned JUCE commit AND the checked-out tree, with no
#     dirty dependency paths, and the recorded CMake configuration copy exists with the recorded hash,
#   - the staged VST3 hash equals the record's, the record's test executable was the one the newest
#     plugin-test log ran, and that log's real exit code is 0 with 0 failures,
#   - the newest validator log names the SAME module hash, reports 0 failures, exit 0.
# Environment overrides (negative controls): KCF_RECORD, KCF_TESTLOG, KCF_VALIDATORLOG, KCF_STAGED_SO, KCF_SKIP_GIT_CLEAN.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$here"
pinned_juce="f72bad64d29715216226685810c5196bd0d79d77"
record="${KCF_RECORD:-artifacts/logs/build-record.txt}"
staged="${KCF_STAGED_SO:-artifacts/vst3/KickCrafter Fable.vst3/Contents/x86_64-linux/KickCrafter Fable.so}"
testlog="${KCF_TESTLOG:-$(ls -t artifacts/logs/runs/*-plugin-tests.log 2>/dev/null | head -1)}"
validatorlog="${KCF_VALIDATORLOG:-$(ls -t artifacts/logs/runs/*-validator.log 2>/dev/null | head -1)}"
die() { echo "PREFLIGHT FAIL: $*" >&2; exit 1; }
field() { grep "^$1:" "$record" | head -1 | cut -d' ' -f2; }
archived="engine plugin tests tools resources docs run .github CMakeLists.txt README.md CHANGELOG.md LICENSE THIRD_PARTY_NOTICES.md .gitignore"
if [ "${KCF_SKIP_GIT_CLEAN:-0}" != "1" ] && [ -n "$(git status --porcelain -- $archived)" ]; then
    git status --porcelain -- $archived >&2; die "archived paths are not committed"
fi
[ -f "$record" ] || die "no build record ($record)"
[ "$(field sources-unchanged-during-build)" = "yes" ] || die "sources changed during the recorded build"
app_manifest="$(find engine plugin resources CMakeLists.txt -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
[ "$(field manifest-after-build)" = "$app_manifest" ] || die "build record application manifest $(field manifest-after-build) != current $app_manifest"
[ "$(field juce)" = "$pinned_juce" ] || die "build record dependency revision $(field juce) is not the pinned $pinned_juce"
[ "$(git -C external/JUCE rev-parse HEAD)" = "$pinned_juce" ] || die "checked-out JUCE is not the pinned revision"
[ "$(field juce-dirty-paths)" = "0" ] || die "dependency tree had dirty paths at build time"
[ -z "$(git -C external/JUCE status --porcelain)" ] || die "dependency tree is dirty now"
cache_copy="$(field cmake-cache-copy)"
[ -n "$cache_copy" ] && [ -f "$cache_copy" ] || die "recorded CMake configuration copy missing"
[ "$(sha256sum "$cache_copy" | cut -d' ' -f1)" = "$(field cmake-cache)" ] || die "CMake configuration copy does not match the recorded hash"
rec_so="$(field vst3-so)"
[ -f "$staged" ] || die "staged bundle missing"
staged_sha="$(sha256sum "$staged" | cut -d' ' -f1)"
[ "$staged_sha" = "$rec_so" ] || die "staged VST3 hash $staged_sha differs from the build record $rec_so"
rec_test="$(field test-exe)"
[ -n "$testlog" ] && [ -f "$testlog" ] || die "no plugin-test log"
grep -q "^# binary sha256: $rec_test$" "$testlog" || die "newest plugin-test log did not run the recorded test executable"
grep -q "^# exit code: 0$" "$testlog" || die "newest plugin-test log did not exit 0"
[ -f "$testlog.exit" ] && [ "$(cat "$testlog.exit")" = "0" ] || die "plugin-test exit sidecar is not 0"
grep -q "0 failure(s): PASS" "$testlog" || die "plugin-test log has failures"
[ -n "$validatorlog" ] && [ -f "$validatorlog" ] || die "no validator log"
grep -q "^# module sha256: $staged_sha$" "$validatorlog" || die "newest validator log does not name the staged module $staged_sha"
grep -q "0 tests failed" "$validatorlog" && grep -q "^# exit code: 0$" "$validatorlog" || die "validator log is not a clean pass"
[ -f "$validatorlog.exit" ] && [ "$(cat "$validatorlog.exit")" = "0" ] || die "validator exit sidecar is not 0"
echo "preflight ok: git $(git rev-parse --short HEAD), app $app_manifest, vst3 $staged_sha, tests $(basename "$testlog"), validator $(basename "$validatorlog")"
