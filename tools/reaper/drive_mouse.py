#!/usr/bin/env python3
"""Real mouse input for the REAPER GUI checks (xdotool on the Xvfb display).

Every xdotool/xwininfo call must succeed, the plug-in editor must be found and its
actual size must match the layout file, otherwise the driver exits nonzero
: evidence can only be produced by real input on the real window.

Usage:
  drive_mouse.py locate                          -> geometry dict
  drive_mouse.py geometry                        -> "x,y,w,h" of REAPER's floating FX window
  drive_mouse.py touch  <layout.txt> <seconds>   -> repeated knob + graph-handle drags
  drive_mouse.py read   <layout.txt> <seconds>   -> repeated knob drags (Read-mode check)
  drive_mouse.py click  <layout.txt> <element>
  drive_mouse.py drag   <layout.txt> <element> <dx> <dy>
  drive_mouse.py resize <dx> <dy>                -> drag the editor's corner resizer
"""
import os
import re
import subprocess
import sys
import time

DISPLAY = os.environ.get("KCF_DISPLAY", os.environ.get("DISPLAY", ":102"))
ENV = {"DISPLAY": DISPLAY, "PATH": os.environ.get("PATH", "/usr/bin:/usr/sbin")}


class DriverError(RuntimeError):
    pass


def sh(*args):
    """Run xdotool and fail loudly on any nonzero exit."""
    result = subprocess.run(["xdotool", *args], env=ENV, capture_output=True, text=True)
    if result.returncode != 0:
        raise DriverError(f"xdotool {' '.join(args)} failed ({result.returncode}): {result.stderr.strip()}")
    return result.stdout.strip()


def locate():
    """Find the JUCE editor window (child of REAPER's floating FX window)."""
    tree = subprocess.run(["xwininfo", "-root", "-tree"], env=ENV, capture_output=True, text=True)
    if tree.returncode != 0:
        raise DriverError("xwininfo failed: " + tree.stderr.strip())
    lines = tree.stdout.splitlines()
    for i, line in enumerate(lines):
        if "KickCrafter Fable" in line and "Track" in line:
            m = re.search(r"(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)\s*$", line)
            if not m:
                continue
            fx = tuple(int(v) for v in m.groups())
            for child in lines[i + 1:i + 8]:
                m2 = re.search(r"(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)\s*$", child)
                if m2 and "0x" in child:
                    w, h, _, _, ax, ay = (int(v) for v in m2.groups())
                    if w >= 600:
                        return {"fx": fx, "editor": (ax, ay, w, h)}
            raise DriverError("FX window found but no plug-in view child of a plausible size")
    raise DriverError("plug-in editor window not found on " + DISPLAY)


def raise_editor():
    """ReaScript invocations raise REAPER's main window above the floating FX window;
    bring the plug-in window back on top before every real mouse action."""
    ids = sh("search", "--name", "KickCrafter Fable").split()
    if not ids:
        raise DriverError("no KickCrafter Fable window to raise")
    for wid in ids:
        sh("windowraise", wid)
    time.sleep(0.3)


def load_layout(path):
    layout = {}
    for line in open(path):
        parts = line.split()
        if parts and parts[0] == "editor":
            layout["_size"] = (int(parts[1]), int(parts[2]))
        elif parts and parts[0] == "knob":
            layout["knob:" + parts[1]] = (float(parts[2]), float(parts[3]))
        elif parts and parts[0] == "handle":
            layout["handle:" + parts[1] + ":" + parts[2]] = (float(parts[3]), float(parts[4]))
    if "_size" not in layout:
        raise DriverError(f"layout file {path} has no editor size line")
    return layout


def check_geometry(geom, layout):
    ax, ay, w, h = geom["editor"]
    lw, lh = layout["_size"]
    if (w, h) != (lw, lh):
        raise DriverError(f"editor is {w}x{h} on screen but the layout file describes {lw}x{lh}")


def screen_point(geom, layout, element):
    if element not in layout:
        raise DriverError(f"unknown layout element {element}")
    ax, ay, w, h = geom["editor"]
    x, y = layout[element]
    return int(round(ax + x)), int(round(ay + y))


def drag(geom, layout, element, dx, dy, steps=12, hold=0.02):
    raise_editor()
    check_geometry(geom, layout)
    x, y = screen_point(geom, layout, element)
    sh("mousemove", str(x), str(y))
    time.sleep(0.05)
    sh("mousedown", "1")
    for i in range(1, steps + 1):
        sh("mousemove", str(int(x + dx * i / steps)), str(int(y + dy * i / steps)))
        time.sleep(hold)
    time.sleep(0.05)
    sh("mouseup", "1")
    time.sleep(0.1)
    return (x, y)


def resize(dx, dy):
    raise_editor()
    geom = locate()
    ax, ay, w, h = geom["editor"]
    x, y = ax + w - 5, ay + h - 5
    sh("mousemove", str(x), str(y))
    time.sleep(0.3)
    sh("mousedown", "1")
    for i in range(1, 13):
        sh("mousemove", str(int(x + dx * i / 12)), str(int(y + dy * i / 12)))
        time.sleep(0.05)
    time.sleep(0.3)
    sh("mouseup", "1")
    time.sleep(1.5)
    after = locate()
    if after["editor"][2:] == (w, h):
        raise DriverError(f"corner drag did not resize the editor (still {w}x{h})")
    print(f"resized {w}x{h} -> {after['editor'][2]}x{after['editor'][3]}")
    return after


def main():
    cmd = sys.argv[1]
    if cmd == "resize":
        resize(float(sys.argv[2]), float(sys.argv[3]))
        return
    geom = locate()
    if cmd == "locate":
        print(geom)
        return
    if cmd == "geometry":
        x, y, w, h = geom["fx"][4], geom["fx"][5], geom["fx"][0], geom["fx"][1]
        print(f"{x},{y},{w},{h}")
        return
    if cmd == "editor":            # "ax,ay,w,h" of the plug-in view itself (absolute screen coordinates)
        print(",".join(str(v) for v in geom["editor"]))
        return
    layout = load_layout(sys.argv[2])
    check_geometry(geom, layout)
    if cmd == "click":
        raise_editor()
        x, y = screen_point(geom, layout, sys.argv[3])
        sh("mousemove", str(x), str(y), "click", "1")
        print(f"clicked {sys.argv[3]} at {x},{y}")
        return
    if cmd == "drag":
        p = drag(geom, layout, sys.argv[3], float(sys.argv[4]), float(sys.argv[5]))
        print(f"dragged {sys.argv[3]} from {p} by {sys.argv[4]},{sys.argv[5]}")
        return
    seconds = float(sys.argv[3])
    end = time.time() + seconds
    cycle = 0
    trace = []
    while time.time() < end:
        amount = [40, -25, 55, -45, 30][cycle % 5]
        trace.append(("knob:startFreq", drag(geom, layout, "knob:startFreq", 0, -amount), amount))
        time.sleep(0.6)
        if cmd == "touch":
            d = [35, -20, 50, -40, 25][cycle % 5]
            trace.append(("handle:pitch:sweep", drag(geom, layout, "handle:pitch:sweep", d, 0), d))
            time.sleep(0.6)
        cycle += 1
    if cycle == 0:
        raise DriverError("no drive cycle executed")
    print(f"drove {cycle} cycles, {len(trace)} real drags on window {geom['editor']}")
    for t in trace:
        print(f"  drag {t[0]} at {t[1]} amount {t[2]}")


if __name__ == "__main__":
    try:
        main()
    except DriverError as error:
        print("DRIVER FAIL:", error, file=sys.stderr)
        sys.exit(3)
