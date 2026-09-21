#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy>=1.26", "pillow>=10"]
# ///
"""Locate the highlighted popup-menu row in a real capture and print its centre "x,y".

usage: find_highlight.py <png> <x0> <y0> <x1> <y1> [r,g,b] [tolerance]
Searches the given screen region for pixels of the menu highlight colour (default: the
KickCrafter theme's selected-row colour 113,66,36) and prints the centre of their
bounding box. Exits 1 when no plausible highlighted row exists (fewer than 400 pixels or a
bounding box that is not row-shaped)."""
import sys

import numpy as np
from PIL import Image


def main(argv):
    if len(argv) < 6:
        raise SystemExit(__doc__)
    png, x0, y0, x1, y1 = argv[1], *(int(v) for v in argv[2:6])
    colour = tuple(int(c) for c in (argv[6] if len(argv) > 6 else "113,66,36").split(","))
    tolerance = int(argv[7]) if len(argv) > 7 else 8
    image = np.asarray(Image.open(png).convert("RGB")).astype(int)
    region = image[y0:y1, x0:x1]
    mask = np.ones(region.shape[:2], dtype=bool)
    for channel, wanted in enumerate(colour):
        mask &= np.abs(region[..., channel] - wanted) <= tolerance
    ys, xs = np.nonzero(mask)
    if len(ys) < 400:
        print(f"no highlighted row: {len(ys)} matching pixels in region", file=sys.stderr)
        return 1
    left, right, top, bottom = xs.min() + x0, xs.max() + x0, ys.min() + y0, ys.max() + y0
    width, height = right - left + 1, bottom - top + 1
    if width < 80 or height < 12 or height > 60:
        print(f"highlight bounding box is not row-shaped: {width}x{height} at {left},{top}", file=sys.stderr)
        return 1
    print(f"{(left + right) // 2},{(top + bottom) // 2}")
    print(f"highlighted row bbox {left},{top} {width}x{height}, {len(ys)} pixels", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
