#!/usr/bin/env bash
# Start a headless X display with a window manager: Xvfb + Openbox, 1600x1200, idempotent.
#   tools/xvfb-display.sh          -> :104, used by the GUI tests, the chain and the memory gate
#   tools/xvfb-display.sh :102     -> the REAPER host harness display (KCF_DISPLAY)
# Two displays so the plugin/leak tests never share a screen with the mouse-driven REAPER stages.
# Openbox is started with a task-local XDG_CACHE_HOME and the system rc.xml (no user settings).
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
display="${1:-:104}"
log="$here/artifacts/logs/xvfb-${display#:}.log"
if ! DISPLAY="$display" xdotool getdisplaygeometry >/dev/null 2>&1; then
    nohup Xvfb "$display" -screen 0 1600x1200x24 -nolisten tcp >> "$log" 2>&1 &
    echo $! > "$here/artifacts/logs/xvfb-${display#:}.pid"
    for i in $(seq 1 50); do DISPLAY="$display" xdotool getdisplaygeometry >/dev/null 2>&1 && break; sleep 0.2; done
    DISPLAY="$display" xdotool getdisplaygeometry >/dev/null 2>&1 || { echo "Xvfb $display did not start (see $log)"; exit 1; }
    echo "started Xvfb $display"
else
    echo "Xvfb $display already running"
fi
"$here/tools/reaper/x11_screensaver.py" "$display" >/dev/null 2>&1 || true
if ! DISPLAY="$display" xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "window id"; then
    export XDG_CACHE_HOME="$here/artifacts/reaper/xdg-cache-${display#:}"
    mkdir -p "$XDG_CACHE_HOME"
    DISPLAY="$display" nohup openbox --sm-disable --config-file /etc/xdg/openbox/rc.xml >> "$log" 2>&1 &
    echo $! > "$here/artifacts/logs/openbox-${display#:}.pid"
    sleep 1
    DISPLAY="$display" xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "window id" || { echo "openbox on $display did not start (see $log)"; exit 1; }
    echo "started openbox on $display"
else
    echo "window manager already running on $display"
fi
