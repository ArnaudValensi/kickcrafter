#!/usr/bin/env bash
# Negative controls for tools/package-preflight.sh and the build chain's failure handling.
# Runs in a scratch git repository built from the real record/logs (review 023): the live worktree
# is never modified. Fixtures: failed test run, stale build, sources changed during build, wrong
# staged binary, test log from another executable, failed validator, validator for another module,
# committed test change with an old record, wrong dependency revision, dirty documentation,
# and a build chain whose configure fails after a previously successful build log.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$here"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
failures=0
expect_nonzero() { if "${@:2}" >/dev/null 2>&1; then echo "FAIL (returned 0): $1"; failures=$((failures+1)); else echo "ok (nonzero): $1"; fi; }
expect_zero() { if "${@:2}" >/dev/null 2>&1; then echo "ok (zero): $1"; else echo "FAIL (nonzero): $1"; failures=$((failures+1)); fi; }
record="artifacts/logs/build-record.txt"; testlog="$(ls -t artifacts/logs/runs/*-plugin-tests.log | head -1)"; vlog="$(ls -t artifacts/logs/runs/*-validator.log | head -1)"
[ -f "$record" ] && [ -f "$vlog" ] || { echo "no build record / validator log yet"; exit 1; }

# --- scratch repository mirroring the archived paths + evidence, committed clean
scratch="$tmp/repo"; mkdir -p "$scratch"
for p in engine plugin tests tools resources docs run .github CMakeLists.txt README.md CHANGELOG.md LICENSE THIRD_PARTY_NOTICES.md .gitignore; do [ -e "$p" ] && cp -r "$p" "$scratch/"; done
mkdir -p "$scratch/artifacts/logs/runs" "$scratch/artifacts/vst3" "$scratch/external"
cp "$record" "$scratch/artifacts/logs/build-record.txt"
cp "$testlog" "$testlog.exit" "$vlog" "$vlog.exit" "$scratch/artifacts/logs/runs/"
cp "$(grep '^cmake-cache-copy:' "$record" | cut -d' ' -f2)" "$scratch/artifacts/logs/runs/"
cp -r "artifacts/vst3/KickCrafter Fable.vst3" "$scratch/artifacts/vst3/"
ln -s "$here/external/JUCE" "$scratch/external/JUCE"
( cd "$scratch" && git init -q && git add -A . >/dev/null 2>&1 && git -c user.name=t -c user.email=t@t commit -q -m fixture ) || { echo "scratch repo failed"; exit 1; }
pf() { ( cd "$scratch" && env "$@" tools/package-preflight.sh ); }

expect_zero "preflight: genuine record/logs/binary in a clean scratch repo" pf KCF_X=1
sed 's/^# exit code: 0$/# exit code: 1/; s/0 failure(s): PASS/1 failure(s): FAIL/' "$testlog" > "$tmp/failed.log"; echo 1 > "$tmp/failed.log.exit"
expect_nonzero "preflight: failed test run rejected" pf KCF_TESTLOG="$tmp/failed.log"
sed 's/^manifest-after-build: .*/manifest-after-build: 0000/' "$record" > "$tmp/stale.txt"
expect_nonzero "preflight: stale build record rejected" pf KCF_RECORD="$tmp/stale.txt"
sed 's/^sources-unchanged-during-build: yes/sources-unchanged-during-build: no/' "$record" > "$tmp/changed.txt"
expect_nonzero "preflight: sources changed during the build rejected" pf KCF_RECORD="$tmp/changed.txt"
cp tools/package.sh "$tmp/not-the-plugin.so"
expect_nonzero "preflight: staged binary not matching the record rejected" pf KCF_STAGED_SO="$tmp/not-the-plugin.so"
sed 's/^# binary sha256: .*/# binary sha256: deadbeef/' "$testlog" > "$tmp/other-exe.log"; cp "$testlog.exit" "$tmp/other-exe.log.exit"
expect_nonzero "preflight: test log from another executable rejected" pf KCF_TESTLOG="$tmp/other-exe.log"
sed 's/0 tests failed/1 tests failed/' "$vlog" > "$tmp/bad-validator.log"; cp "$vlog.exit" "$tmp/bad-validator.log.exit"
expect_nonzero "preflight: failed validator log rejected" pf KCF_VALIDATORLOG="$tmp/bad-validator.log"
sed 's/^# module sha256: .*/# module sha256: 1111111111111111111111111111111111111111111111111111111111111111/' "$vlog" > "$tmp/other-module.log"; cp "$vlog.exit" "$tmp/other-module.log.exit"
expect_nonzero "preflight: validator log for another module rejected" pf KCF_VALIDATORLOG="$tmp/other-module.log"
sed 's/^juce: .*/juce: 0000000000000000000000000000000000000000/' "$record" > "$tmp/wrong-dep.txt"
expect_nonzero "preflight: wrong recorded dependency revision rejected" pf KCF_RECORD="$tmp/wrong-dep.txt"
( cd "$scratch" && echo "// changed" >> tests/plugin_tests.cpp && git -c user.name=t -c user.email=t@t commit -qam "test change" )
expect_zero "preflight: a committed test/tool change does not invalidate the record (binary bound to production sources only)" pf KCF_X=1
( cd "$scratch" && echo "// changed" >> engine/KickParams.h && git -c user.name=t -c user.email=t@t commit -qam "source change" )
expect_nonzero "preflight: a committed production-source change with the old record rejected" pf KCF_X=1
( cd "$scratch" && git -c user.name=t -c user.email=t@t revert --no-edit HEAD >/dev/null )
expect_zero "preflight: clean again after reverting the source change" pf KCF_X=1
( cd "$scratch" && echo "temp" >> README.md )
expect_nonzero "preflight: dirty documentation rejected" pf KCF_X=1
( cd "$scratch" && git checkout -q -- README.md )

# --- build chain: a failing configure must not stage or write a record, even with an old success log
chain="$tmp/chain"; mkdir -p "$chain/build" "$chain/tools" "$chain/artifacts/logs/runs" "$chain/artifacts/vst3" "$chain/engine" "$chain/plugin" "$chain/resources" "$chain/tests"
printf 'x' > "$chain/CMakeLists.txt"; printf 'x' > "$chain/engine/a.cpp"; printf 'x' > "$chain/tests/t.cpp"
cp tools/build-and-test.sh "$chain/tools/"; cp tools/run-logged.sh "$chain/tools/"
printf 'previous successful build\nbuild exit=0\n' > "$chain/artifacts/logs/build-plugin.log"
printf '#!/usr/bin/env bash\nexit 1\n' > "$chain/tools/cmake"; chmod +x "$chain/tools/cmake"
touch "$chain/artifacts/logs/build-record.txt.absent"
( cd "$chain" && PATH="$chain/tools:$PATH" bash tools/build-and-test.sh >/dev/null 2>&1 ); chain_exit=$?
if [ "$chain_exit" -ne 0 ] && [ ! -f "$chain/artifacts/logs/build-record.txt" ] && ! ls "$chain/artifacts/vst3/"* >/dev/null 2>&1 && grep -q "chain FAILED (configure)" "$chain/artifacts/logs/chain-status.txt"; then
    echo "ok (nonzero): build chain with a failing configure after an old success log publishes nothing"
else
    echo "FAIL: build chain published something or exited 0 after a failing configure (exit $chain_exit)"; failures=$((failures+1))
fi
echo "package negative controls: $failures failure(s)"; [ "$failures" -eq 0 ]
