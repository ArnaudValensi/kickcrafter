#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy>=1.26", "pillow>=10"]
# ///
"""Pixel checks on real captures of the validation display. Exit 0 when the check holds.

usage: image_check.py uniform <png>                       fail when the capture is uniform (black, nothing visible)
       image_check.py popup <png> <cx> <cy> <x0> <x1>     fail when no popup hangs below the point (cx, cy):
                                                          the strip 24..120 px below it, cx+x0..cx+x1 wide, must
                                                          show text-like contrast on a dark panel
"""
import sys
import numpy as np
from PIL import Image


def uniform(png):
    a = np.asarray(Image.open(png).convert("L")).astype(float)
    print("capture std", round(a.std(), 2))
    return a.std() > 8


def popup(png, cx, cy, x0, x1):
    im = np.asarray(Image.open(png).convert("L")).astype(int)
    box = im[cy + 24 : cy + 120, cx + x0 : cx + x1]
    print("popup region std", round(box.std(), 2), "mean", round(box.mean(), 1))
    return box.size > 0 and box.std() > 12 and box.mean() < 90


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "uniform":
        ok = uniform(args[1])
    elif len(args) == 6 and args[0] == "popup":
        ok = popup(args[1], *(int(v) for v in args[2:]))
    else:
        sys.exit(__doc__)
    sys.exit(0 if ok else 1)
