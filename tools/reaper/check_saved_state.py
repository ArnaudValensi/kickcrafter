#!/usr/bin/env python3
"""Inspect the opaque plug-in state saved by REAPER inside an .rpp file.

Decodes the base64 VST chunk, finds the KickCrafterFable XML and prints the
requested parameter (or root attribute). With an expected value the exit code is
0 only when the saved value matches within the tolerance (relative to max(1, |expected|)).

    check_saved_state.py project.rpp <parameter> [expected] [tolerance]
    check_saved_state.py project.rpp --attr lastProgramChangeThread [expected-text]
"""
import base64
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def find_state_xml(project_text):
    for group in re.finditer(r"(?:^[ \t]+[A-Za-z0-9+/=]+\r?\n)+", project_text, re.MULTILINE):
        encoded = "".join(group.group().split())
        try:
            decoded = b"".join(base64.b64decode(block) for block in re.findall(r"[^=]+={0,2}", encoded))
        except Exception:
            continue
        start = decoded.find(b"<KickCrafterFable")
        if start < 0:
            continue
        end = decoded.find(b"</KickCrafterFable>", start)
        if end < 0:
            end = decoded.find(b"/>", start)
            if end < 0:
                continue
            end += 2
        else:
            end += len(b"</KickCrafterFable>")
        return decoded[start:end].decode("utf-8", errors="ignore")
    return None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[0]
    text = Path(path).read_text()
    xml_text = find_state_xml(text)
    if xml_text is None:
        print("FAIL: no KickCrafterFable state found in", path)
        return 1
    root = ET.fromstring(xml_text)
    if argv[1] == "--attr":
        name = argv[2]
        value = root.get(name)
        print(f"saved state version={root.get('version')} {name}={value}")
        if len(argv) > 3:
            ok = value == argv[3]
            print(("PASS" if ok else "FAIL") + f": saved {name} '{value}' vs expected '{argv[3]}'")
            return 0 if ok else 1
        return 0
    parameter = argv[1]
    params = root.find("Params")
    raw = params.get(parameter) if params is not None else None
    if raw is None:
        print(f"FAIL: parameter {parameter} absent from saved state")
        return 1
    value = float(raw)
    print(f"saved state version={root.get('version')} {parameter}={value}")
    if len(argv) > 2:
        expected = float(argv[2])
        tolerance = float(argv[3]) if len(argv) > 3 else 0.02
        ok = math.isfinite(value) and abs(value - expected) <= tolerance * max(1.0, abs(expected))
        print(("PASS" if ok else "FAIL") + f": saved {parameter} {value} vs expected {expected} (tol {tolerance})")
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
