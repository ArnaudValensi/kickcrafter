#!/usr/bin/env bash
# KickCrafter Fable — REAPER host validation driver (isolated config, Xvfb :102 + Openbox).
#
# Every stage exits nonzero on ANY failed required command, wait, driver or analyzer
#: FAIL takes precedence over DONE, stages require their own
# affirmative "PASS:" line, GUI evidence requires the real editor window, renders must be
# freshly produced by this run (mtime after the stage start) with the expected decoded
# sample rate. Stage status lines are appended to artifacts/reaper/stage-status.txt.
#
# Stages:
#   prepare   write the scratch reaper.ini (dummy audio, vstpath = artifacts/vst3)
#   setup     checks 1-4: instantiate, enumerate, set values, envelopes, save/reopen, render x2
#   analyze   measure control.wav/automation.wav against the engine reference hit
#   editor    dismiss dialogs, raise/move the editor, screenshot reaper-editor(.png/-crop.png)
#   menus     open the plug-in's preset dropdown and scale menu with real clicks (needs a WM)
#   touch     check 5: Touch mode, real mouse drags on a knob and a graph handle during playback
#   read      check 6: Read mode, real mouse drags; host value, saved state and rendered audio
#   lifecycle check 7: editor closed/open renders, 96k/44.1k renders with the editor open
#   panic     CC120 / CC123 delivery renders
#   program   host Program exposure check (no "Program" parameter is published, by design)
#   resize    real corner-drag resizes to 125 % and 80 % with mapping checks and captures
#   captures  reaper-automation.png (lanes, no editor/dialogs) + reaper-envelope-picker.png (Track Envelopes window)
#   stop      stop the REAPER instance started here
# Source this file with KCF_HARNESS_LIB=1 to reuse the functions (negative controls).
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
display="${KCF_DISPLAY:-:102}"
results="${KCF_RESULTS:-$here/artifacts/reaper}"
cfgdir="$results/config"
shots="${KCF_SHOTS:-$here/artifacts/screenshots}"
export DISPLAY="$display"
export KCF_TEST_DIR="$results"
mkdir -p "$results" "$cfgdir" "$shots"
reaper="${KCF_REAPER:-/usr/sbin/reaper}"
xdotool_bin="${KCF_XDOTOOL:-xdotool}"
vstpath="$here/artifacts/vst3"
ini="$cfgdir/reaper.ini"
layout="$results/layout-100.txt"
tests_bin="$here/build/tests/kcf_plugin_tests"
stage_failed=0
stage_start_epoch="$(date +%s)"

log() { echo "[$(date -u +%H:%M:%S)] $*"; }
fail() { log "FAIL: $*"; stage_failed=1; return 1; }
shot() { "$here/tools/reaper/screenshot.py" "$display" "$@" || fail "screenshot $1 failed"; }
# A capture that is (nearly) uniform is not evidence of anything: fail loudly.
shot_checked() {   # shot_checked <png> [region]
    shot "$@" || return 1
    "$here/tools/reaper/image_check.py" uniform "$1" || fail "capture $1 is uniform/black (nothing visible on the display)"
}
status_line() {   # status_line <stage> <code>
    echo "$(date -u +%FT%TZ) stage=$1 exit=$2 bundle=$(sha256sum "$vstpath/KickCrafter Fable.vst3/Contents/x86_64-linux/KickCrafter Fable.so" 2>/dev/null | cut -c1-16)" >> "$results/stage-status.txt"
}

prepare_config() {
    cat > "$ini" <<EOF
[REAPER]
alsa_indev=
alsa_outdev=
alsa_rtprio=0
audio_driver=4
dummy_blocksize=256
dummy_numinputs=2
dummy_numoutputs=2
dummy_srate=48000
jack_launchcmd=
jack_rtprio=-1
linux_audio_bits=32
linux_audio_bsize=512
linux_audio_bufs=3
linux_audio_mode=2
linux_audio_nch_in=2
linux_audio_nch_out=2
linux_audio_srate=48000
linux_audio_srateor=1
linux_auto_pasuspend=1
linux_disable_pm=0
linux_mlockall=0
loadlastproj=0
newprojdo=0
numrecent=1
projecttabs=0
renderclosewhendone=4
splash=0
splash_options=0
splashanim=0
versioncheck=0
vstpath=$vstpath
wnd_h=1100
wnd_w=1560
wnd_x=20
wnd_y=20
multinst=0
EOF
    log "config written: $ini (vstpath=$vstpath)"
    # v1.2: isolated user-preset folder for the plug-in inside REAPER (never the user's own
    # ~/.config), seeded with one preset the menus stage selects and verifies.
    rm -rf "$results/user-presets"; mkdir -p "$results/user-presets"
    cat > "$results/user-presets/Harness Kick.xml" <<EOF
<KickCrafterPreset version="3" name="Harness Kick">
  <Params startFreq="250" endFreq="61" sweep="90" hold="132" fade="50" attack="0.4" curve="1" shape="0" drive="1" velocity="100" pitchSource="0"/>
</KickCrafterPreset>
EOF
    log "user preset folder seeded: $results/user-presets (Harness Kick: sweep 90 ms, end 61 Hz; start/hold/shape/drive stay lane-driven)"
}

start_reaper() {   # start_reaper <script.lua>
    log "starting reaper with $1"
    KCF_PRESET_DIR="$results/user-presets" nohup timeout 3600 "$reaper" -newinst -nosplash -cfgfile "$ini" -new "$1" >> "$results/reaper-stdout.log" 2>&1 &
    echo $! > "$results/reaper.pid"
}

run_script() {     # run_script <script.lua>: run in the already running instance
    log "running $1 in the running instance"
    "$reaper" -nonewinst -cfgfile "$ini" "$1" >> "$results/reaper-stdout.log" 2>&1 || fail "reaper -nonewinst $1 returned $?"
}

# Wait for a stage results file. Returns 1 when the file contains a FAIL line (checked
# FIRST), when it never reaches DONE within the timeout, or when the required PASS
# pattern is absent. Returns 0 only for FAIL-free, DONE, PASS-containing results.
wait_for_result() {   # wait_for_result <results-file> <timeout-seconds> <required-pass-pattern>
    local file="$1" timeout_s="$2" pattern="$3" waited=0
    while [ $waited -lt "$timeout_s" ]; do
        if [ -f "$file" ] && grep -q "^FAIL" "$file"; then log "stage reported FAIL in $file"; return 1; fi
        if [ -f "$file" ] && grep -q "^DONE" "$file"; then
            if grep -q "$pattern" "$file"; then return 0; fi
            log "stage completed without '$pattern' in $file"; return 1
        fi
        sleep 2; waited=$((waited + 2))
    done
    log "timeout waiting for $file"; return 1
}

wait_for_marker() {   # wait_for_marker <file> <timeout-seconds>
    local file="$1" timeout_s="$2" waited=0
    while [ $waited -lt "$timeout_s" ]; do
        [ -f "$file" ] && return 0
        sleep 1; waited=$((waited + 1))
    done
    log "ready marker $file never appeared"; return 1
}

# Close REAPER's modeless dialogs (render-complete, render warning, About/evaluation) the
# normal way: focus + Escape, then WM_DELETE_WINDOW if one survives. Never iconify them:
# they are transient for REAPER's window group and Openbox iconifies the whole group with
# them (every window, including the editor, became hidden; captures were black).
dismiss_dialogs() {
    local w
    "$here/tools/reaper/x11_screensaver.py" "$display" >/dev/null 2>&1 || true
    for name in "About REAPER" "Render Warning" "Finished in"; do
        for w in $($xdotool_bin search --name "$name" 2>/dev/null); do
            $xdotool_bin windowactivate --sync "$w" 2>/dev/null; sleep 0.3
            $xdotool_bin key Escape 2>/dev/null; sleep 0.5
            if xdotool_search_alive "$w"; then $xdotool_bin windowquit "$w" 2>/dev/null; sleep 0.5; fi
            if xdotool_search_alive "$w"; then log "dialog '$name' ($w) is still present after Escape and WM_DELETE_WINDOW"; fi
        done
    done
    restore_iconified
    sleep 0.5
}
xdotool_search_alive() { $xdotool_bin getwindowname "$1" >/dev/null 2>&1; }

# De-iconify every hidden REAPER client window (the group side effect described above).
restore_iconified() {
    local w
    for w in $($xdotool_bin search --classname REAPER 2>/dev/null); do
        if xprop -id "$w" WM_STATE 2>/dev/null | grep -q "Iconic"; then
            $xdotool_bin windowmap "$w" 2>/dev/null; $xdotool_bin windowactivate "$w" 2>/dev/null
            log "restored iconified window $w ($($xdotool_bin getwindowname "$w" 2>/dev/null))"
        fi
    done
}

# Raise + position the editor; fails (return 1) when the plug-in window is absent or not viewable.
raise_editor() {   # raise_editor <x> <y>
    local ed
    ed=$($xdotool_bin search --name "KickCrafter Fable" 2>/dev/null | head -1)
    [ -n "$ed" ] || { fail "plug-in editor window not found"; return 1; }
    restore_iconified
    $xdotool_bin windowactivate --sync "$ed" 2>/dev/null
    $xdotool_bin windowraise "$ed" && $xdotool_bin windowmove "$ed" "$1" "$2" || { fail "cannot raise/move the editor"; return 1; }
    sleep 1.5
    xwininfo -id "$ed" 2>/dev/null | grep -q "Map State: IsViewable" || { fail "plug-in editor window is not viewable (hidden/iconified)"; return 1; }
    "$here/tools/reaper/drive_mouse.py" geometry > "$results/editor-geometry.txt" || { fail "editor geometry unavailable"; return 1; }
    log "editor geometry $(cat "$results/editor-geometry.txt")"
}

fresh_layout() {   # fresh_layout <percent>: layout file from the test executable for the given scale
    DISPLAY="$display" "$tests_bin" --layout "$1" > "$results/layout-$1.txt" 2>/dev/null || { fail "layout dump at $1 % failed"; return 1; }
    grep -q "^editor " "$results/layout-$1.txt" || { fail "layout dump at $1 % has no editor line"; return 1; }
}

# Host value of a parameter: stdout carries ONLY one validated decimal number; every
# diagnostic goes to stderr so callers can compare parsed values.
readback() {   # readback <param-index>
    local script="$results/readback.lua" value
    cat > "$script" <<EOF
local d = os.getenv("KCF_TEST_DIR"); local track = reaper.GetTrack(0, 0)
local f = io.open(d .. "/readback.txt", "w"); f:write(string.format("%.6f", reaper.TrackFX_GetParamNormalized(track, 0, $1))); f:close()
EOF
    rm -f "$results/readback.txt"
    run_script "$script" >&2 || return 1
    for i in 1 2 3 4 5 6 7 8 9 10; do [ -f "$results/readback.txt" ] && break; sleep 0.5; done
    [ -f "$results/readback.txt" ] || { fail "readback produced no value" >&2; return 1; }
    value="$(cat "$results/readback.txt")"
    [[ "$value" =~ ^[0-9]+\.[0-9]+$ ]] || { fail "readback is not a number: '$value'" >&2; return 1; }
    echo "$value"
}

# True when two readback values differ by more than 1e-4 (both must be valid numbers).
value_changed() {   # value_changed <before> <after>
    [[ "$1" =~ ^[0-9]+\.[0-9]+$ && "$2" =~ ^[0-9]+\.[0-9]+$ ]] || return 1
    awk -v a="$1" -v b="$2" 'BEGIN { d = a - b; if (d < 0) d = -d; exit (d > 0.0001 ? 0 : 1) }'
}

stage_setup() {
    prepare_config
    date +%s > "$results/setup-start-epoch"
    rm -f "$results"/reaper-setup-results.txt "$results"/control.wav "$results"/automation.wav "$results"/velocity-off.wav "$results"/touch-ready "$results"/read-ready
    start_reaper "$here/tools/reaper/kcf_setup.lua"
    wait_for_result "$results/reaper-setup-results.txt" 600 "^PASS: opening the native editor" || fail "setup stage"
    grep -q "^PASS: instrument discovery" "$results/reaper-setup-results.txt" || fail "setup: discovery/persistence PASS line missing"
    grep -v '^PARAM' "$results/reaper-setup-results.txt"
    echo "host parameter rows: $(grep -c '^PARAM' "$results/reaper-setup-results.txt")"
    for f in control.wav automation.wav velocity-off.wav; do [ -f "$results/$f" ] || fail "$f not rendered"; done
    return $stage_failed
}

stage_analyze() {
    local engine_tests="${KCF_ENGINE_TESTS:-}"     # the engine test binary dumps the reference hit (build/ from the chain, or an engine-only tree)
    for candidate in "$here/build/tests/kcf_engine_tests" "$here/build-engine/tests/kcf_engine_tests"; do
        [ -n "$engine_tests" ] || { [ -x "$candidate" ] && engine_tests="$candidate"; }
    done
    [ -n "$engine_tests" ] || { fail "kcf_engine_tests not built (build/tests or build-engine/tests)"; return 1; }
    [ -f "$results/reference-hit-48k.f32" ] || "$engine_tests" --dump-default-hit "$results/reference-hit-48k.f32" || fail "reference hit dump"
    local since; since="$(cat "$results/setup-start-epoch" 2>/dev/null || echo 0)"
    "$here/tools/reaper/analyze_render.py" "$results" "$results/reference-hit-48k.f32" --fresh-since "$since" | tee "$results/analyze-render.txt"
    [ "${PIPESTATUS[0]}" -eq 0 ] || fail "render analysis"
    return $stage_failed
}

stage_editor() {
    dismiss_dialogs
    raise_editor 300 60 || return 1
    fresh_layout 100 || return 1
    "$here/tools/reaper/drive_mouse.py" locate > "$results/editor-locate.txt" || fail "editor locate"
    grep -q "1000, 640)" "$results/editor-locate.txt" || fail "editor is not 1000x640 at 100 %: $(cat "$results/editor-locate.txt")"
    shot_checked "$shots/reaper-editor.png"
    shot_checked "$shots/reaper-editor-crop.png" "$(cat "$results/editor-geometry.txt")"
    return $stage_failed
}

# Formatted host value of a parameter (the plug-in's own text, e.g. "640.0 Hz"); stdout carries only that text.
readback_text() {   # readback_text <param-index>
    local script="$results/readback-text.lua" value
    cat > "$script" <<EOF
local d = os.getenv("KCF_TEST_DIR"); local track = reaper.GetTrack(0, 0)
local ok, text = reaper.TrackFX_GetFormattedParamValue(track, 0, $1, "")
local f = io.open(d .. "/readback-text.txt", "w"); f:write(ok and text or "<unavailable>"); f:close()
EOF
    rm -f "$results/readback-text.txt"
    run_script "$script" >&2 || return 1
    for i in 1 2 3 4 5 6 7 8 9 10; do [ -f "$results/readback-text.txt" ] && break; sleep 0.5; done
    [ -f "$results/readback-text.txt" ] || { fail "readback-text produced no value" >&2; return 1; }
    value="$(cat "$results/readback-text.txt")"
    [ -n "$value" ] && [ "$value" != "<unavailable>" ] || { fail "readback-text unavailable for parameter $1" >&2; return 1; }
    echo "$value"
}

# Bypass (mode=bypass) or re-activate (mode=restore) every envelope of track 1 so a
# native preset selection is not overridden by the Read envelopes created by setup
# (the separate Read-authority stage is untouched: it re-creates its own envelope points).
envelopes_mode() {   # envelopes_mode bypass|restore
    local script="$results/envelopes-$1.lua" from to
    if [ "$1" = "bypass" ]; then from=1; to=0; else from=0; to=1; fi
    cat > "$script" <<EOF
local d = os.getenv("KCF_TEST_DIR"); local track = reaper.GetTrack(0, 0)
local changed, total = 0, reaper.CountTrackEnvelopes(track)
for i = 0, total - 1 do
  local env = reaper.GetTrackEnvelope(track, i)
  local ok, chunk = reaper.GetEnvelopeStateChunk(env, "", false)
  if ok then
    local new, n = chunk:gsub("\nACT $from ", "\nACT $to ")
    if n > 0 and reaper.SetEnvelopeStateChunk(env, new, false) then changed = changed + 1 end
  end
end
reaper.UpdateArrange()
local f = io.open(d .. "/envelopes-$1.txt", "w"); f:write(string.format("%d of %d envelopes set to ACT $to", changed, total)); f:close()
EOF
    rm -f "$results/envelopes-$1.txt"
    run_script "$script" || return 1
    for i in 1 2 3 4 5 6 7 8 9 10; do [ -f "$results/envelopes-$1.txt" ] && break; sleep 0.5; done
    [ -f "$results/envelopes-$1.txt" ] || { fail "envelope $1 script produced no result"; return 1; }
    log "envelopes $1: $(cat "$results/envelopes-$1.txt")"
    grep -q "^[1-9][0-9]* of" "$results/envelopes-$1.txt" || { fail "envelope $1 changed nothing"; return 1; }
    sleep 1.0
}

# Editor-relative centre of a top-bar control from a layout dump ("topbar presetBox x y").
layout_point() {   # layout_point <layout-file> <control> -> "x,y" (integers)
    awk -v c="$2" '$1 == "topbar" && $2 == c { printf "%d,%d", $3, $4; found = 1; exit } END { exit (found ? 0 : 1) }' "$1"
}

# Give the plug-in window the focus before clicking one of its controls: every ReaScript run
# raises REAPER's main window, and under Openbox the first click on an unfocused window may only
# focus it (the preset popup then never opened: 21:09 attempt of v1.2).
focus_editor() {
    local ed
    ed=$($xdotool_bin search --name "KickCrafter Fable" 2>/dev/null | head -1)
    [ -n "$ed" ] || { fail "plug-in editor window not found"; return 1; }
    $xdotool_bin windowactivate --sync "$ed" 2>/dev/null; sleep 0.4
}

# Click the popup row that is currently highlighted (located by its exact highlight colour in a
# real capture, inside the preset popup's region below the combo). A real mouse click on the row
# is what a user does; Return-key delivery into the popup proved unreliable under the WM.
click_highlighted_item() {   # click_highlighted_item <capture.png> <editor-x> <editor-y> [x0 y0 x1 y1 (editor offsets of the search region)]
    local point x0="${4:-228}" y0="${5:-50}" x1="${6:-430}" y1="${7:-400}"
    point="$("$here/tools/reaper/find_highlight.py" "$1" $(($2 + x0)) $(($3 + y0)) $(($2 + x1)) $(($3 + y1)))" \
        || { fail "no highlighted popup row found in $1"; return 1; }
    log "clicking highlighted popup row at $point"
    $xdotool_bin mousemove "${point%,*}" "${point#*,}" click 1 || { fail "click on the highlighted row"; return 1; }
}

# Real popup-menu interaction (needs the window manager on :102): preset dropdown, then scale menu.
# The preset test is isolated from the setup project's Read envelopes (bypassed for its duration),
# selects the LAST item (Gabber) with a wrapping Up key (independent of which item is highlighted
# initially) and verifies the plug-in's own formatted host values against the Gabber preset.
stage_menus() {
    dismiss_dialogs
    raise_editor 300 60 || return 1
    fresh_layout 100 || return 1
    local ed ex ey
    ed="$("$here/tools/reaper/drive_mouse.py" editor)" || { fail "editor geometry"; return 1; }
    ex="$(echo "$ed" | cut -d, -f1)"; ey="$(echo "$ed" | cut -d, -f2)"
    [ "$(echo "$ed" | cut -d, -f3-4)" = "1000,640" ] || fail "editor is not at 100 % for the menu test ($ed)"
    envelopes_mode bypass || return 1
    local combo combox comboy scalex scaley
    combo="$(layout_point "$layout" presetBox)" || { fail "layout dump has no top-bar preset box"; envelopes_mode restore; return 1; }
    combox="${combo%,*}"; comboy="${combo#*,}"
    local scalept; scalept="$(layout_point "$layout" scaleButton)" || { fail "layout dump has no scale button"; envelopes_mode restore; return 1; }
    scalex="${scalept%,*}"; scaley="${scalept#*,}"
    local before after
    before="$(readback 8)" || { envelopes_mode restore; return 1; }     # Drive (index 8): Gabber sets 4 x
    log "host values before the preset menu: start=$(readback_text 0), sweep=$(readback_text 2), curve=$(readback_text 6), shape=$(readback_text 7), drive=$(readback_text 8)"
    focus_editor || return 1
    log "preset combo at editor offset $combox,$comboy (from the layout dump); scale button at $scalex,$scaley"
    $xdotool_bin mousemove $((ex + combox)) $((ey + comboy)) click 1 || fail "preset combo click"
    sleep 1.0; shot "$shots/menu-presets-open.png"
    # the popup hangs below the combo: its rows are text on a dark panel (image_check.py popup)
    "$here/tools/reaper/image_check.py" popup "$shots/menu-presets-open.png" $((ex + combox)) $((ey + comboy)) -80 80 || fail "preset popup not visible"
    $xdotool_bin key Up Up || fail "menu key"       # wraps to the last item (the seeded user preset), then Gabber (the "User" header is skipped)
    sleep 0.5; shot "$shots/menu-presets-highlighted.png"
    click_highlighted_item "$shots/menu-presets-highlighted.png" "$ex" "$ey" || { envelopes_mode restore; return 1; }
    sleep 1.5
    after="$(readback 8)" || { envelopes_mode restore; return 1; }
    log "Drive before/after preset menu: $before -> $after"
    value_changed "$before" "$after" || fail "preset menu selection did not change the host value of Drive"
    # Gabber = start 640 Hz, sweep 34 ms, curve 1.60, shape 100 %, drive 4.00 x (plugin/Presets.cpp)
    local idx expected got ok=1
    for spec in "0|640.0 Hz" "2|34.0 ms" "6|1.60" "7|100 %" "8|4.00 x"; do
        idx="${spec%%|*}"; expected="${spec#*|}"
        got="$(readback_text "$idx")" || { ok=0; continue; }
        if [ "$got" = "$expected" ]; then log "host parameter $idx = '$got' (Gabber expects '$expected')"; else log "MISMATCH host parameter $idx = '$got', Gabber expects '$expected'"; ok=0; fi
    done
    [ "$ok" -eq 1 ] || fail "the selected preset is not Gabber (host values do not match)"
    shot_checked "$shots/menu-presets-after.png"
    envelopes_mode restore || return 1
    # Back to the first item (Reference) through the same popup so later stages find the
    # reference layout (graph handles sit where the default-parameter layout dump says).
    focus_editor || return 1
    $xdotool_bin mousemove $((ex + combox)) $((ey + comboy)) click 1 || fail "preset combo click (back to Reference)"
    sleep 1.0
    $xdotool_bin key Down || fail "menu key"          # first item from no highlight, or wraps from the last
    sleep 0.5; shot "$shots/menu-presets-highlighted-reference.png"
    click_highlighted_item "$shots/menu-presets-highlighted-reference.png" "$ex" "$ey" || return 1
    sleep 1.5
    ok=1
    for spec in "2|47.0 ms" "4|50 %" "5|0.40 ms" "6|1.00" "8|1.00 x"; do
        idx="${spec%%|*}"; expected="${spec#*|}"
        got="$(readback_text "$idx")" || { ok=0; continue; }
        if [ "$got" = "$expected" ]; then log "host parameter $idx = '$got' (Reference expects '$expected')"; else log "MISMATCH host parameter $idx = '$got', Reference expects '$expected'"; ok=0; fi
    done
    [ "$ok" -eq 1 ] || fail "returning to the Reference preset through the menu did not restore the reference values"
    # Third pass (v1.1 fix): edit a knob so the combo shows "Reference • edited",
    # then pick Reference AGAIN (the already selected item). The stock ComboBox ignored that click;
    # PresetBox must reload the preset. Sweep Time (index 2) is used because it has no Read lane
    # in this project (Drive/Start/Hold/Shape are envelope-authoritative and would be overridden).
    printf 'local track = reaper.GetTrack(0, 0); reaper.TrackFX_SetParamNormalized(track, 0, 2, 0.5)\n' > "$results/edit-sweep.lua"
    run_script "$results/edit-sweep.lua" || return 1
    sleep 1.0
    got="$(readback_text 2)" || return 1
    [ "$got" != "47.0 ms" ] || fail "sweep edit before the re-pick did not apply (still '$got')"
    focus_editor || return 1
    $xdotool_bin mousemove $((ex + combox)) $((ey + comboy)) click 1 || fail "preset combo click (re-pick)"
    sleep 1.0
    $xdotool_bin key Down || fail "menu key"          # Reference: the item already selected
    sleep 0.5; shot "$shots/menu-presets-repick.png"
    click_highlighted_item "$shots/menu-presets-repick.png" "$ex" "$ey" || return 1
    sleep 1.5
    got="$(readback_text 2)" || return 1
    log "Sweep Time after re-picking the already selected Reference: '$got' (expects '47.0 ms')"
    [ "$got" = "47.0 ms" ] || fail "re-picking the already selected preset did not reload it"
    # Fourth pass (v1.2): the user preset seeded in $KCF_PRESET_DIR is the last popup item
    # (section "User" after the factory bank). Select it with a real click, verify its values,
    # then go back to Reference (first item) the same way.
    focus_editor || return 1
    $xdotool_bin mousemove $((ex + combox)) $((ey + comboy)) click 1 || fail "preset combo click (user preset)"
    sleep 1.0
    $xdotool_bin key Up || fail "menu key"          # wraps to the last item: the user preset
    sleep 0.5; shot "$shots/menu-presets-user.png"
    click_highlighted_item "$shots/menu-presets-user.png" "$ex" "$ey" || return 1
    sleep 1.5
    got="$(readback_text 2)" || return 1
    log "Sweep Time after picking the user preset 'Harness Kick': '$got' (expects '90.0 ms')"
    [ "$got" = "90.0 ms" ] || fail "the user preset from the folder was not applied"
    got="$(readback_text 1)" || return 1
    [ "$got" = "61.0 Hz" ] || fail "user preset end frequency mismatch (got '$got')"
    # Fifth pass (v1.2): the "..." actions menu by a real click; "Save as..."
    # is its second row (Save, Save as..., Rename..., Delete, ---, Open folder, Rescan); the
    # name prompt opens prefilled ("Harness Kick", selected); typing replaces it and Return
    # saves. The proof is the new file in the isolated user folder with the current values
    # (sweep 90 ms / end 61 Hz from "Harness Kick") and the prompt window being gone.
    local menupt menux menuy dlg
    menupt="$(layout_point "$layout" presetMenu)" || { fail "layout dump has no preset actions button"; envelopes_mode restore; return 1; }
    menux="${menupt%,*}"; menuy="${menupt#*,}"
    focus_editor || return 1
    log "preset actions button at editor offset $menux,$menuy (from the layout dump)"
    $xdotool_bin mousemove $((ex + menux)) $((ey + menuy)) click 1 || fail "preset actions button click"
    sleep 1.0; shot "$shots/menu-actions-open.png"
    "$here/tools/reaper/image_check.py" popup "$shots/menu-actions-open.png" $((ex + menux)) $((ey + menuy)) -40 120 || fail "preset actions popup not visible"
    $xdotool_bin key Down Down || fail "menu key"   # second row: "Save as..."
    sleep 0.5; shot "$shots/menu-actions-highlighted.png"
    # search region starts below the "..." button (its own hot highlight shares the colour and
    # merged with the row in the first run, moving the centre onto "Save")
    click_highlighted_item "$shots/menu-actions-highlighted.png" "$ex" "$ey" $((menux - 20)) $((menuy + 22)) $((menux + 260)) 300 || { envelopes_mode restore; return 1; }
    sleep 1.5
    dlg="$($xdotool_bin search --name "^Save preset as$" 2>/dev/null | head -n 1)"
    [ -n "$dlg" ] || { fail "the 'Save preset as' prompt did not open"; envelopes_mode restore; return 1; }
    $xdotool_bin windowactivate --sync "$dlg" || fail "activate the name prompt"
    sleep 0.5; shot_checked "$shots/dialog-save-as.png"
    # Under Openbox on Xvfb the activated JUCE window did not receive typed keys (the field kept
    # its selected prefilled text); a real click into the name field (fixed 400x216 layout:
    # the field spans the width at y ~ 128) makes the JUCE peer take X input focus like a user would.
    local dgeo dx dy
    dgeo="$($xdotool_bin getwindowgeometry --shell "$dlg")" || { fail "name prompt geometry"; envelopes_mode restore; return 1; }
    dx="$(echo "$dgeo" | sed -n 's/^X=//p')"; dy="$(echo "$dgeo" | sed -n 's/^Y=//p')"
    log "name prompt window $dlg at $dx,$dy: clicking into the name field"
    $xdotool_bin mousemove $((dx + 200)) $((dy + 128)) click 1 || fail "click into the name field"
    sleep 0.5
    $xdotool_bin key ctrl+a || fail "prompt select all"
    $xdotool_bin type --delay 40 "Harness Saved" || fail "prompt typing"
    sleep 0.3; shot "$shots/dialog-save-as-typed.png"
    $xdotool_bin key Return || fail "prompt Return"
    sleep 1.5
    if xdotool_search_alive "$dlg"; then fail "the name prompt is still open after Return"; fi
    local saved="$results/user-presets/Harness Saved.xml"
    [ -f "$saved" ] || { fail "Save as did not create '$saved' (folder: $(ls -1 "$results/user-presets" 2>/dev/null | tr '\n' ' '))"; envelopes_mode restore; return 1; }
    log "saved user preset file: $(tr -d '\n' < "$saved" | cut -c1-300)"
    grep -q 'name="Harness Saved"' "$saved" || fail "saved preset has the wrong name"
    grep -q 'sweep="90"' "$saved" || fail "saved preset does not carry the current sweep (90 ms)"
    grep -Eq 'endFreq="61(\.0[0-9]*)?"' "$saved" || fail "saved preset does not carry the current end frequency (61 Hz)"   # 7 significant digits of the log-mapped value
    # the list now ends with the saved preset (User section sorted by name): capture it, then close the popup
    focus_editor || return 1
    $xdotool_bin mousemove $((ex + combox)) $((ey + comboy)) click 1 || fail "preset combo click (after Save as)"
    sleep 1.0
    $xdotool_bin key Up || fail "menu key"
    sleep 0.5; shot "$shots/menu-presets-saved.png"
    $xdotool_bin key Escape || fail "menu escape"
    sleep 0.8
    focus_editor || return 1
    $xdotool_bin mousemove $((ex + combox)) $((ey + comboy)) click 1 || fail "preset combo click (Reference after user)"
    sleep 1.0
    $xdotool_bin key Down || fail "menu key"        # first selectable item: Reference (section headers are skipped)
    sleep 0.5; shot "$shots/menu-presets-highlighted-reference2.png"
    click_highlighted_item "$shots/menu-presets-highlighted-reference2.png" "$ex" "$ey" || return 1
    sleep 1.5
    got="$(readback_text 2)" || return 1
    [ "$got" = "47.0 ms" ] || fail "Reference after the user preset did not restore the sweep (got '$got')"
    # Scale menu (logical 952,28): pick 125 %, editor must become 1250x800.
    focus_editor || return 1
    $xdotool_bin mousemove $((ex + scalex)) $((ey + scaley)) click 1 || fail "scale button click"
    sleep 1.0; shot "$shots/menu-scale-open.png"
    $xdotool_bin key Down Down Down Return || fail "scale menu keys"
    sleep 1.5
    "$here/tools/reaper/drive_mouse.py" locate | tee "$results/editor-locate-125.txt" | grep -q "1250, 800)" || fail "scale menu did not resize to 125 %"
    shot_checked "$shots/menu-scale-125.png"
    # back to 100 % through the menu
    ed="$("$here/tools/reaper/drive_mouse.py" editor)" || { fail "editor geometry at 125 %"; return 1; }
    ex="$(echo "$ed" | cut -d, -f1)"; ey="$(echo "$ed" | cut -d, -f2)"
    fresh_layout 125 || return 1
    local scale125; scale125="$(layout_point "$results/layout-125.txt" scaleButton)" || { fail "no 125 % layout for the scale button"; return 1; }
    $xdotool_bin mousemove $((ex + ${scale125%,*})) $((ey + ${scale125#*,})) click 1 || fail "scale button click at 125 %"
    sleep 1.0; $xdotool_bin key Down Down Return || fail "scale menu keys"
    sleep 1.5
    "$here/tools/reaper/drive_mouse.py" locate | grep -q "1000, 640)" || fail "scale menu did not return to 100 %"
    log "PASS: preset and scale popup menus work with real clicks under the window manager"
    return $stage_failed
}

# The layout dump used to aim the mouse comes from the default parameter set; graph handle
# positions depend on the timing values, so those must be at their defaults before a handle drag.
require_reference_layout_state() {
    local spec idx expected got ok=1
    for spec in "2|47.0 ms" "3|132 ms" "4|50 %" "5|0.40 ms" "6|1.00"; do
        idx="${spec%%|*}"; expected="${spec#*|}"
        got="$(readback_text "$idx")" || { ok=0; continue; }
        [ "$got" = "$expected" ] || { log "host parameter $idx = '$got', the layout dump assumes '$expected'"; ok=0; }
    done
    [ "$ok" -eq 1 ] || { fail "timing parameters are not at the reference values the layout dump assumes"; return 1; }
}

stage_touch() {
    rm -f "$results/touch-ready" "$results/touch-driver-done" "$results/reaper-touch-results.txt"
    fresh_layout 100 || return 1
    require_reference_layout_state || return 1
    run_script "$here/tools/reaper/kcf_touch.lua" || return 1
    wait_for_marker "$results/touch-ready" 30 || fail "touch stage never became ready"
    sleep 2; dismiss_dialogs; raise_editor 300 60 || return 1
    "$here/tools/reaper/drive_mouse.py" touch "$layout" 32 | tee "$results/touch-driver.txt"
    [ "${PIPESTATUS[0]}" -eq 0 ] || fail "touch mouse driver"
    shot_checked "$shots/reaper-touch-during.png"
    touch "$results/touch-driver-done"
    wait_for_result "$results/reaper-touch-results.txt" 120 "^PASS: native knob" || fail "touch stage"
    grep -v "^POINT" "$results/reaper-touch-results.txt"
    return $stage_failed
}

stage_read() {
    rm -f "$results/read-ready" "$results/read-driver-done" "$results/reaper-read-results.txt" "$results/read-reference.wav" "$results/read-after.wav" "$results/read-mode.rpp"
    fresh_layout 100 || return 1
    local since; since="$(date +%s)"
    run_script "$here/tools/reaper/kcf_read.lua" || return 1
    wait_for_marker "$results/read-ready" 40 || fail "read stage never became ready"
    sleep 2; dismiss_dialogs; raise_editor 300 60 || return 1
    "$here/tools/reaper/drive_mouse.py" read "$layout" 14 | tee "$results/read-driver.txt"
    [ "${PIPESTATUS[0]}" -eq 0 ] || fail "read mouse driver"
    shot_checked "$shots/reaper-read-during.png"
    touch "$results/read-driver-done"
    wait_for_result "$results/reaper-read-results.txt" 120 "^PASS: host Read envelope" || fail "read stage"
    grep -E "PASS|FAIL|playing" "$results/reaper-read-results.txt"
    "$here/tools/reaper/check_saved_state.py" "$results/read-mode.rpp" startFreq 79.62 0.01 | tee -a "$results/reaper-read-results.txt"
    [ "${PIPESTATUS[0]}" -eq 0 ] || fail "saved-state check"
    "$here/tools/reaper/analyze_render.py" "$results" --read --fresh-since "$since" | tee -a "$results/reaper-read-results.txt"
    [ "${PIPESTATUS[0]}" -eq 0 ] || fail "read render comparison"
    return $stage_failed
}

stage_lifecycle() {
    rm -f "$results/reaper-lifecycle-results.txt" "$results"/editor-closed.wav "$results"/editor-open.wav "$results"/lifecycle-96000.wav "$results"/lifecycle-44100.wav
    local since; since="$(date +%s)"
    run_script "$here/tools/reaper/kcf_lifecycle.lua" || return 1
    wait_for_result "$results/reaper-lifecycle-results.txt" 300 "^PASS: editor close/reopen" || fail "lifecycle stage"
    cat "$results/reaper-lifecycle-results.txt"
    "$here/tools/reaper/analyze_render.py" "$results" --lifecycle --fresh-since "$since" | tee -a "$results/reaper-lifecycle-results.txt"
    [ "${PIPESTATUS[0]}" -eq 0 ] || fail "lifecycle render analysis"
    return $stage_failed
}

stage_panic() {
    rm -f "$results/reaper-panic-results.txt" "$results"/panic-*.wav
    local since; since="$(date +%s)"
    run_script "$here/tools/reaper/kcf_panic.lua" || return 1
    wait_for_result "$results/reaper-panic-results.txt" 300 "^PASS: three panic renders" || fail "panic stage"
    "$here/tools/reaper/analyze_render.py" "$results" --panic --fresh-since "$since" | tee -a "$results/reaper-panic-results.txt"
    [ "${PIPESTATUS[0]}" -eq 0 ] || fail "panic render analysis"
    return $stage_failed
}

stage_program() {
    rm -f "$results/reaper-program-results.txt"
    run_script "$here/tools/reaper/kcf_program.lua" || return 1
    wait_for_result "$results/reaper-program-results.txt" 60 "^PASS: no host Program" || fail "program exposure stage"
    cat "$results/reaper-program-results.txt"
    return $stage_failed
}

# Real corner-drag resizes with mapping checks (End Frequency has no envelope: index 1).
stage_resize() {
    dismiss_dialogs
    raise_editor 300 60 || return 1
    fresh_layout 125 || return 1; fresh_layout 80 || return 1
    "$here/tools/reaper/drive_mouse.py" resize 250 160 || fail "resize to 125 %"
    "$here/tools/reaper/drive_mouse.py" locate | grep -q "1250, 800)" || fail "editor is not 1250x800 after the corner drag"
    shot_checked "$shots/editor-resized.png"
    local b a
    b="$(readback 1)" || return 1
    "$here/tools/reaper/drive_mouse.py" drag "$results/layout-125.txt" knob:endFreq 0 -60 || fail "knob drag at 125 %"
    a="$(readback 1)" || return 1
    log "End Frequency at 125 %: $b -> $a"; value_changed "$b" "$a" || fail "knob drag at 125 % did not change the host value"
    "$here/tools/reaper/drive_mouse.py" resize -450 -288 || fail "resize to 80 %"
    "$here/tools/reaper/drive_mouse.py" locate | grep -q "800, 512)" || fail "editor is not 800x512 after the corner drag"
    shot_checked "$shots/editor-min.png"
    b="$(readback 1)" || return 1
    "$here/tools/reaper/drive_mouse.py" drag "$results/layout-80.txt" knob:endFreq 0 40 || fail "knob drag at 80 %"
    a="$(readback 1)" || return 1
    log "End Frequency at 80 %: $b -> $a"; value_changed "$b" "$a" || fail "knob drag at 80 % did not change the host value"
    "$here/tools/reaper/drive_mouse.py" resize 200 128 || fail "resize back to 100 %"
    "$here/tools/reaper/drive_mouse.py" locate | grep -q "1000, 640)" || fail "editor is not back at 1000x640"
    log "PASS: corner resizes 100 -> 125 -> 80 -> 100 % with correct mouse mapping"
    return $stage_failed
}

# Final host captures on the running instance: the arrangement with its automation lanes and no
# editor/dialogs (reaper-automation.png), and REAPER's own "Track 1 - Envelopes" window opened by a
# real click on the track panel's envelope button (reaper-envelope-picker.png): the automation
# picker that must list the application controls without synthetic MIDI-CC rows.
stage_captures() {
    local script="$results/captures-editor.lua" main env
    dismiss_dialogs
    # Reopen the setup project (its Hold/Start/Shape/Drive Read lanes are the automation that the
    # snapshot renders used; later stages replaced the track content), then hide the floating editor.
    cat > "$script" <<EOF
reaper.Main_openProject("noprompt:$results/automation.rpp")
local t = reaper.GetTrack(0, 0); reaper.SetOnlyTrackSelected(t); reaper.TrackFX_Show(t, 0, 2)
EOF
    run_script "$script" || return 1
    sleep 2.5
    sleep 1.5; dismiss_dialogs
    main=$($xdotool_bin search --name "REAPER v7.80" 2>/dev/null | head -1)
    [ -n "$main" ] || { fail "REAPER main window not found"; return 1; }
    $xdotool_bin windowactivate --sync "$main" 2>/dev/null; sleep 0.5
    local visible="" w
    for w in $($xdotool_bin search --name "KickCrafter Fable" 2>/dev/null || true); do
        xwininfo -id "$w" 2>/dev/null | grep -q "IsViewable" && visible="$visible $w"
    done
    [ -z "$visible" ] || { fail "editor window still visible for the automation capture ($visible)"; return 1; }
    shot_checked "$shots/reaper-automation.png" || return 1
    # Track panel envelope button: main-window client offset (36,146) at the default TCP layout.
    eval "$($xdotool_bin getwindowgeometry --shell "$main")"
    $xdotool_bin mousemove $((X + 36)) $((Y + 146)) click 1 || { fail "envelope button click"; return 1; }
    local waited=0; env=""
    while [ $waited -lt 10 ]; do env=$($xdotool_bin search --name "Envelopes" 2>/dev/null | head -1); [ -n "$env" ] && break; sleep 0.5; waited=$((waited + 1)); done
    [ -n "$env" ] || { fail "the Track Envelopes window did not open"; return 1; }
    log "envelope window: $($xdotool_bin getwindowname "$env")"
    $xdotool_bin windowmove "$env" 320 150 2>/dev/null; $xdotool_bin windowactivate --sync "$env" 2>/dev/null; sleep 1.0
    shot_checked "$shots/reaper-envelope-picker.png" || return 1
    $xdotool_bin windowquit "$env" 2>/dev/null; sleep 0.5
    printf 'local t = reaper.GetTrack(0, 0); reaper.TrackFX_Show(t, 0, 3)\n' > "$script"
    run_script "$script" || return 1                   # editor back
    sleep 1.5; raise_editor 300 60 || return 1
    log "PASS: automation-lane and Track Envelopes window captures taken on the running instance"
    return $stage_failed
}

stage_stop() {
    if [ -f "$results/reaper.pid" ]; then kill "$(cat "$results/reaper.pid")" 2>/dev/null; sleep 2; fi
    pkill -f "cfgfile $ini" 2>/dev/null; log "reaper stopped"
}

if [ "${KCF_HARNESS_LIB:-0}" = "1" ]; then return 0 2>/dev/null || true; fi

stage="${1:-help}"
case "$stage" in
    prepare) prepare_config; code=$? ;;
    setup) stage_setup; code=$? ;;
    analyze) stage_analyze; code=$? ;;
    editor) stage_editor; code=$? ;;
    menus) stage_menus; code=$? ;;
    touch) stage_touch; code=$? ;;
    read) stage_read; code=$? ;;
    lifecycle) stage_lifecycle; code=$? ;;
    panic) stage_panic; code=$? ;;
    program) stage_program; code=$? ;;
    resize) stage_resize; code=$? ;;
    captures) stage_captures; code=$? ;;
    stop) stage_stop; code=$? ;;
    *) sed -n 2,24p "$0"; code=2 ;;
esac
[ "$stage" != "help" ] && status_line "$stage" "$code"
log "stage $stage exit $code"
exit $code
