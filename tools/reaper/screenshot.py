#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow>=10"]
# ///
"""Grab the X display (or a region) to a PNG. Real pixels, no synthesis."""
import sys

from PIL import ImageGrab


def main(display, path, region=None):
    image = ImageGrab.grab(xdisplay=display)
    if region:
        x, y, w, h = (int(v) for v in region.split(","))
        image = image.crop((x, y, x + w, y + h))
    image.save(path)
    print(f"saved {path} {image.size[0]}x{image.size[1]}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit("usage: screenshot.py :DISPLAY out.png [x,y,w,h]")
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
