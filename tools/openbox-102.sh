#!/usr/bin/env bash
# Start Openbox on the REAPER harness display :102 with a task-local cache (never touches user settings).
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
export DISPLAY="${KCF_DISPLAY:-:102}"
export XDG_CACHE_HOME="$here/artifacts/reaper/xdg-cache"
export XDG_CONFIG_HOME="$here/artifacts/reaper/xdg-config"
mkdir -p "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME"
if pgrep -f "openbox.*--config-file /etc/xdg/openbox/rc.xml" >/dev/null && [ "$(pgrep -f 'openbox' -a | grep -c ':102' )" -gt 0 ]; then echo "openbox already running"; fi
echo "starting openbox on $DISPLAY (pid file artifacts/reaper/openbox.pid)"
exec openbox --sm-disable --config-file /etc/xdg/openbox/rc.xml
