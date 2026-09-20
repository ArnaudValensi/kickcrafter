#!/usr/bin/env python3
"""Disable and reset the X screen saver on the validation display.

Xvfb blanks its screen after its default idle timeout; while blanked, captures are black
and synthetic pointer input does not reach mapped windows. The harness calls this before
each stage and before every capture. Uses libX11 through ctypes (no xset on this machine).
usage: x11_screensaver.py :DISPLAY [query]
"""
import ctypes
import sys

DONT_PREFER_BLANKING = 0
DEFAULT_EXPOSURES = 2
SCREEN_SAVER_RESET = 0


def main(display, query_only=False):
    x11 = ctypes.CDLL("libX11.so.6")
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XGetScreenSaver.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                                    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
    x11.XSetScreenSaver.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    x11.XForceScreenSaver.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    dpy = x11.XOpenDisplay(display.encode())
    if not dpy:
        raise SystemExit(f"cannot open display {display}")
    timeout, interval, blanking, exposures = (ctypes.c_int() for _ in range(4))
    x11.XGetScreenSaver(dpy, ctypes.byref(timeout), ctypes.byref(interval), ctypes.byref(blanking), ctypes.byref(exposures))
    print(f"screensaver before: timeout={timeout.value}s interval={interval.value}s blanking={blanking.value} exposures={exposures.value}")
    if not query_only:
        x11.XSetScreenSaver(dpy, 0, 0, DONT_PREFER_BLANKING, DEFAULT_EXPOSURES)
        x11.XForceScreenSaver(dpy, SCREEN_SAVER_RESET)
        x11.XSync(dpy, 0)
        x11.XGetScreenSaver(dpy, ctypes.byref(timeout), ctypes.byref(interval), ctypes.byref(blanking), ctypes.byref(exposures))
        print(f"screensaver after: timeout={timeout.value}s (0 = disabled), saver reset")
    x11.XCloseDisplay(dpy)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    main(sys.argv[1], len(sys.argv) > 2 and sys.argv[2] == "query")
