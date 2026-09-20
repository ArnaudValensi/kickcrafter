#!/usr/bin/env bash
# Negative controls for the host harness: every fixture below models a
# failure that used to be reported as success; each must now yield a nonzero status.
# No REAPER, display or mouse is used: the runner is sourced as a library and the mouse driver
# is exercised with a fake xdotool/xwininfo on PATH.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
export KCF_RESULTS="$tmp/results" KCF_SHOTS="$tmp/shots" KCF_HARNESS_LIB=1 KCF_XDOTOOL="$tmp/bin/xdotool" KCF_REAPER="$tmp/bin/reaper"
mkdir -p "$KCF_RESULTS" "$KCF_SHOTS" "$tmp/bin"
source "$here/tools/reaper/run_reaper_validation.sh"
failures=0
expect_nonzero() {   # expect_nonzero <label> <command...>
    if "${@:2}" >/dev/null 2>&1; then echo "FAIL (returned 0): $1"; failures=$((failures + 1)); else echo "ok (nonzero): $1"; fi
}
expect_zero() { if "${@:2}" >/dev/null 2>&1; then echo "ok (zero): $1"; else echo "FAIL (nonzero): $1"; failures=$((failures + 1)); fi; }

# 1. FAIL + DONE in a results file must lose against the PASS requirement.
printf 'FAIL: something\nDONE\n' > "$tmp/r1.txt"
expect_nonzero "wait_for_result: FAIL then DONE" wait_for_result "$tmp/r1.txt" 4 "^PASS:"
printf 'PASS: fine\nFAIL: later\nDONE\n' > "$tmp/r2.txt"
expect_nonzero "wait_for_result: PASS then FAIL then DONE" wait_for_result "$tmp/r2.txt" 4 "^PASS:"
printf 'DONE\n' > "$tmp/r3.txt"
expect_nonzero "wait_for_result: DONE without the PASS line" wait_for_result "$tmp/r3.txt" 4 "^PASS:"
printf 'PASS: fine\nDONE\n' > "$tmp/r4.txt"
expect_zero "wait_for_result: PASS then DONE" wait_for_result "$tmp/r4.txt" 4 "^PASS:"
# 2. Timeout without DONE; absent ready marker.
printf 'OBSERVING\n' > "$tmp/r5.txt"
expect_nonzero "wait_for_result: never DONE (timeout)" wait_for_result "$tmp/r5.txt" 3 "^PASS:"
expect_nonzero "wait_for_marker: marker never appears" wait_for_marker "$tmp/never-ready" 2

# 3. Failed mouse driver: fake xdotool returning 1, fake xwininfo with a plausible tree.
cat > "$tmp/bin/xdotool" <<'EOF'
#!/usr/bin/env bash
exit 1
EOF
cat > "$tmp/bin/xwininfo" <<'EOF'
#!/usr/bin/env bash
cat <<'T'
xwininfo: Window id: 0x1 (the root window) (has no name)
  Root window id: 0x1 (the root window) (has no name)
  Parent window id: 0x0 (none)
     2 children:
     0x20db47 "VST3i: KickCrafter Fable (Arnaud Valensi) - Track 1": ("REAPER" "REAPER")  1000x668+300+60  +300+60
        1 child:
        0x20db46 (has no name): ()  1000x640+0+28  +300+88
T
EOF
chmod +x "$tmp/bin/xdotool" "$tmp/bin/xwininfo"
printf 'editor 1000 640 scale 1.000\nknob startFreq 692.0 132.0\n' > "$tmp/layout-100.txt"
expect_nonzero "drive_mouse: xdotool failure propagates" env PATH="$tmp/bin:$PATH" "$here/tools/reaper/drive_mouse.py" drag "$tmp/layout-100.txt" knob:startFreq 0 -40
printf 'editor 1250 800 scale 1.250\nknob startFreq 865.0 165.0\n' > "$tmp/layout-125.txt"
cat > "$tmp/bin/xdotool" <<'EOF'
#!/usr/bin/env bash
[ "$1" = "search" ] && echo 0x20db47
exit 0
EOF
expect_nonzero "drive_mouse: layout size mismatch (125 % layout on a 1000x640 editor)" env PATH="$tmp/bin:$PATH" "$here/tools/reaper/drive_mouse.py" drag "$tmp/layout-125.txt" knob:startFreq 0 -40
expect_zero "drive_mouse: matching layout with a healthy fake xdotool" env PATH="$tmp/bin:$PATH" "$here/tools/reaper/drive_mouse.py" drag "$tmp/layout-100.txt" knob:startFreq 0 -40
cat > "$tmp/bin/xwininfo" <<'EOF'
#!/usr/bin/env bash
echo "     0x20001b \"automation - REAPER v7.80\": (\"REAPER\" \"REAPER\")  1560x1100+20+20  +20+20"
EOF
expect_nonzero "drive_mouse: editor window absent" env PATH="$tmp/bin:$PATH" "$here/tools/reaper/drive_mouse.py" locate

# 4. Analyzer: wrong sample rate under the right file name, and stale files.
src="$here/artifacts/reaper/control.wav"
if [ -f "$src" ]; then
    mkdir -p "$tmp/wav"
    for n in editor-closed editor-open lifecycle-96000 lifecycle-44100; do cp "$src" "$tmp/wav/$n.wav"; done
    expect_nonzero "analyze --lifecycle: 48 kHz files named 96000/44100 are rejected" "$here/tools/reaper/analyze_render.py" "$tmp/wav" --lifecycle
    cp "$src" "$tmp/wav/read-reference.wav"; cp "$src" "$tmp/wav/read-after.wav"
    touch -d '2020-01-01' "$tmp/wav/read-reference.wav" "$tmp/wav/read-after.wav"
    expect_nonzero "analyze --read: stale renders (older than the run) are rejected" "$here/tools/reaper/analyze_render.py" "$tmp/wav" --read --fresh-since "$(date +%s)"
    touch "$tmp/wav/read-reference.wav" "$tmp/wav/read-after.wav"
    expect_zero "analyze --read: fresh identical renders pass" "$here/tools/reaper/analyze_render.py" "$tmp/wav" --read --fresh-since "$(( $(date +%s) - 60 ))"
    rm -f "$tmp/wav/read-after.wav"
    expect_nonzero "analyze --read: missing render is rejected" "$here/tools/reaper/analyze_render.py" "$tmp/wav" --read
else
    echo "skip (no control.wav yet): analyzer fixtures"; failures=$((failures + 1))
fi

# 5. Saved-state checker: numeric CLI tolerance and failing comparisons.
rpp="$here/artifacts/reaper/read-mode.rpp"
if [ -f "$rpp" ]; then
    expect_zero "check_saved_state: string CLI tolerance is parsed as a number" "$here/tools/reaper/check_saved_state.py" "$rpp" startFreq 79.62 0.01
    expect_nonzero "check_saved_state: wrong expected value fails" "$here/tools/reaper/check_saved_state.py" "$rpp" startFreq 250 0.001
    expect_nonzero "check_saved_state: absent parameter fails" "$here/tools/reaper/check_saved_state.py" "$rpp" nosuchparam 1 0.1
else
    echo "skip (no read-mode.rpp yet): saved-state fixtures"; failures=$((failures + 1))
fi

# 5b. Readback oracle: unchanged values with different timestamps must NOT count as a change.
expect_nonzero "value_changed: identical host values (timestamps irrelevant)" value_changed 0.500000 0.500000
expect_nonzero "value_changed: non-numeric readback rejected" value_changed "[09:21:21] running x" 0.500000
expect_zero "value_changed: really changed value" value_changed 0.300000 0.519269
# readback() must print only a number even though run_script logs: mock reaper writes the file.
printf '#!/usr/bin/env bash\nprintf 0.500000 > "$KCF_TEST_DIR/readback.txt"; exit 0\n' > "$tmp/bin/reaper"
chmod +x "$tmp/bin/reaper"
out="$(readback 1 2>/dev/null)"; if [ "$out" = "0.500000" ]; then echo "ok (zero): readback stdout is exactly the number"; else echo "FAIL: readback stdout was '$out'"; failures=$((failures + 1)); fi

# 6. A stage function whose required wait fails must exit nonzero even if later commands succeed.
fake_stage() { wait_for_result "$tmp/r1.txt" 2 "^PASS:" || fail "fixture wait"; echo "later command succeeds"; return $stage_failed; }
stage_failed=0
expect_nonzero "stage: failed wait followed by a successful command still fails" fake_stage
stage_failed=0

echo "negative controls: $failures failure(s)"
[ "$failures" -eq 0 ]
