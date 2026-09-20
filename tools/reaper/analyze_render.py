#!/usr/bin/env python3
"""Measure the REAPER renders produced by the host scripts. Every mode exits nonzero on
any failed check; every WAV must be PCM24 (REAPER's default), have the expected
channel count, sample rate and duration, and (with --fresh-since) have been written
after the given epoch time so stale files from a previous run can never pass.

control.wav / automation.wav (48 kHz, stereo, 4 s) contain, at 120 BPM:
  0.10 s  A1 vel 127        -> audible, 55 Hz in the hold region, matches the engine reference hit
  0.60 s  A1 vel 40         -> audible but clearly quieter (velocity sensitivity)
  1.10 s  A1 vel 127
  1.15 s  A2 vel 127        -> overlapping voices: more energy than one hit, A2 pitch in an isolated window
  2.00 s  A1 vel 127        -> long hit (Hold envelope 1000 ms) whose tail must be identical in both files
  2.80 s  A1 vel 127        -> triggered while the long hit still sounds; differs between the files
Automation (Read mode): Start Frequency 250 -> 1500 Hz, Shape 0 -> 100 %, Drive 1 -> 4 x, all jumping at
2.5 s while the 2.0 s note is still sounding (its 1 s hold makes shape/drive audibly live if they leaked).

    analyze_render.py <dir> <reference.f32> [--fresh-since EPOCH]   -> main checks
    analyze_render.py <dir> --read      [--fresh-since EPOCH]        -> read-reference.wav == read-after.wav
    analyze_render.py <dir> --lifecycle [--fresh-since EPOCH]        -> editor closed/open + 96k/44.1k renders
    analyze_render.py <dir> --panic     [--fresh-since EPOCH]        -> CC120 / CC123 renders (no VST3 CC delivery, v1.1)
"""
import os
import sys
import wave
from pathlib import Path

import numpy as np

FRESH_SINCE = None


def read_wav(path, rate=None, channels=2, min_seconds=None):
    path = Path(path)
    if not path.exists():
        raise SystemExit(f"FAIL: render {path.name} missing")
    if FRESH_SINCE is not None and path.stat().st_mtime < FRESH_SINCE:
        raise SystemExit(f"FAIL: render {path.name} is stale (older than this run)")
    with wave.open(str(path), "rb") as audio:
        width = audio.getsampwidth()
        actual_rate = audio.getframerate()
        actual_channels = audio.getnchannels()
        frames = audio.readframes(audio.getnframes())
    if width != 3:
        raise SystemExit(f"FAIL: harness requires PCM24 renders; {path.name} has width {width}")
    if rate is not None and actual_rate != rate:
        raise SystemExit(f"FAIL: {path.name} is {actual_rate} Hz, expected {rate}")
    if actual_channels != channels:
        raise SystemExit(f"FAIL: {path.name} has {actual_channels} channels, expected {channels}")
    raw = np.frombuffer(frames, dtype=np.uint8).reshape(-1, 3)
    ints = (raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16))
    ints = np.where(ints >= 1 << 23, ints - (1 << 24), ints)
    data = (ints.astype(np.float64) / float(1 << 23)).reshape(-1, actual_channels)
    if min_seconds is not None and data.shape[0] < min_seconds * actual_rate:
        raise SystemExit(f"FAIL: {path.name} is shorter than {min_seconds} s")
    return data, actual_rate


def energy(x, rate, start, end):
    seg = x[int(start * rate):int(end * rate)]
    return float(np.sum(seg * seg))


def zero_crossing_hz(x, rate, start, end):
    seg = x[int(start * rate):int(end * rate)]
    idx = np.where((seg[:-1] < 0) & (seg[1:] >= 0))[0]
    if len(idx) < 2:
        return 0.0
    frac = seg[idx] / (seg[idx] - seg[idx + 1])
    crossings = idx + frac
    return rate * (len(crossings) - 1) / (crossings[-1] - crossings[0])


class Checker:
    def __init__(self):
        self.failures = []

    def __call__(self, cond, message):
        print(("PASS " if cond else "FAIL ") + message)
        if not cond:
            self.failures.append(message)

    def finish(self, label):
        if self.failures:
            print(f"\n{len(self.failures)} FAILED ({label})")
            return 1
        print(f"\nALL {label} CHECKS PASS")
        return 0


def compare_read_renders(directory):
    check = Checker()
    ref, rate = read_wav(directory / "read-reference.wav", rate=48000, min_seconds=1.4)
    after, _ = read_wav(directory / "read-after.wav", rate=48000, min_seconds=1.4)
    check(ref.shape == after.shape, "read renders have the same length")
    diff = float(np.max(np.abs(ref - after))) if ref.shape == after.shape else float("inf")
    audible = energy(ref[:, 0], rate, 0.10, 0.30)
    print(f"read-mode renders: max abs diff {diff:.3e}, first-note energy {audible:.4f}")
    check(audible > 1e-3, "read-reference render is audible")
    check(diff == 0.0, "post-drag Read-mode render is sample-identical to the undragged envelope reference")
    return check.finish("READ")


def compare_lifecycle_renders(directory):
    check = Checker()
    closed, rate = read_wav(directory / "editor-closed.wav", rate=48000, min_seconds=3.9)
    opened, _ = read_wav(directory / "editor-open.wav", rate=48000, min_seconds=3.9)
    check(closed.shape == opened.shape, "closed/open renders have the same length")
    diff = float(np.max(np.abs(closed - opened))) if closed.shape == opened.shape else float("inf")
    audible = energy(closed[:, 0], rate, 0.10, 0.30)
    print(f"editor closed vs open renders: max abs diff {diff:.3e}, first-note energy {audible:.4f}")
    check(audible > 1e-3, "editor-closed render is audible")
    check(diff == 0.0, "same project (48 kHz) renders identically with the editor closed and open")
    for rate_name, wanted in (("lifecycle-96000", 96000), ("lifecycle-44100", 44100)):
        data, r = read_wav(directory / f"{rate_name}.wav", rate=wanted, min_seconds=1.4)
        e = energy(data[:, 0], r, 0.10, 0.30)
        hz = zero_crossing_hz(data[:, 0], r, 0.16, 0.27)     # hold region of the 0.1 s note: ~6 cycles of 55 Hz
        check(bool(np.all(np.isfinite(data))) and e > 1e-3 and abs(hz - 55.0) < 1.0,
              f"{rate_name}: decoded {r} Hz, finite, audible (energy {e:.4f}), hold pitch {hz:.2f} Hz")
    return check.finish("LIFECYCLE")


def compare_panic_renders(directory):
    """panic-control.wav: one long note at 0.1 s (Hold 1000 ms, no CC).
    panic-cc120.wav: same + CC120 (All Sound Off) at 0.6 s.
    panic-cc123.wav: same + CC123 (All Notes Off) at 0.6 s.
    v1.1: the VST3 exports no synthetic MIDI CC parameters (user request), and VST3 has no
    raw CC events, so neither message reaches the plug-in from REAPER: both renders must be
    sample-identical to the control (the one-shot keeps sounding and ends by itself). The
    processor's own CC120/CC123 handling is covered by the plugin tests."""
    check = Checker()
    control, rate = read_wav(directory / "panic-control.wav", rate=48000, min_seconds=1.9)
    cc120, _ = read_wav(directory / "panic-cc120.wav", rate=48000, min_seconds=1.9)
    cc123, _ = read_wav(directory / "panic-cc123.wav", rate=48000, min_seconds=1.9)
    c, a, b = control[:, 0], cc120[:, 0], cc123[:, 0]
    check(energy(c, rate, 0.7, 1.1) > 1e-3, f"control note still sounds after 0.7 s (energy {energy(c, rate, 0.7, 1.1):.4f})")
    check(bool(np.all(np.isfinite(c))) and float(np.max(np.abs(c))) <= 1.0, "control render finite and bounded")
    results = (directory / "reaper-panic-results.txt").read_text() if (directory / "reaper-panic-results.txt").exists() else ""
    check("CC120 present in panic-cc120" in results and "CC123 present in panic-cc123" in results,
          "the CC events were really written into the MIDI items (negative control for the identity oracle)")
    diff120 = float(np.max(np.abs(a - c)))
    check(diff120 == 0.0, f"CC120 in the MIDI item does not reach the VST3 (no CC emulation, v1.1): render identical to control (max diff {diff120:.2e})")
    diff123 = float(np.max(np.abs(b - c)))
    check(diff123 == 0.0, f"CC123 likewise leaves the one-shot voice untouched (max diff {diff123:.2e})")
    # State stability across the variants: a second control render made after the CC variants
    # must be sample-identical to the first (parameter delivery had settled; a discarded settle
    # render precedes the first control render in kcf_panic.lua).
    control2, _ = read_wav(directory / "panic-control2.wav", rate=48000, min_seconds=1.9)
    stable = float(np.max(np.abs(control2[:, 0] - c)))
    check(stable == 0.0, f"control rendered again after the CC variants is identical (max diff {stable:.2e})")
    check((directory / "panic-settle.wav").exists(), "settle render was produced before the control render")
    return check.finish("PANIC")


def compare_program_renders(directory):
    """program-control.wav: long note at 0.1 s (Hold 1000 ms), second note at 1.4 s, program 0.
    program-automation.wav: same plus a Program envelope jumping to preset 7 (Gabber) at 0.6 s."""
    check = Checker()
    control, rate = read_wav(directory / "program-control.wav", rate=48000, min_seconds=2.4)
    auto, _ = read_wav(directory / "program-automation.wav", rate=48000, min_seconds=2.4)
    c, a = control[:, 0], auto[:, 0]
    s0, s1, s2 = int(0.1 * rate), int(1.4 * rate), int(2.4 * rate)
    same = float(np.max(np.abs(a[s0:s1] - c[s0:s1])))
    later = float(np.max(np.abs(a[s1:s2] - c[s1:s2])))
    check(energy(c, rate, 0.7, 1.2) > 1e-3, "long note audible past the program change")
    check(same == 0.0, f"active voice unchanged by the host Program envelope jump at 0.6 s (max diff {same:.2e})")
    check(later > 0.05, f"note after the Program envelope jump uses the new program (max diff {later:.3f})")
    return check.finish("PROGRAM"), later


def main(directory, reference_hit):
    check = Checker()
    control, rate = read_wav(directory / "control.wav", rate=48000, min_seconds=3.9)
    automation, _ = read_wav(directory / "automation.wav", rate=48000, min_seconds=3.9)
    left, right = control[:, 0], control[:, 1]

    check(bool(np.all(np.isfinite(control))) and bool(np.all(np.isfinite(automation))), "renders are finite")
    check(float(np.max(np.abs(left - right))) < 1e-6, "dual mono output (L == R)")
    check(energy(left, rate, 0.0, 0.099) == 0.0, "silence before the first note")
    e1 = energy(left, rate, 0.10, 0.30)
    e2 = energy(left, rate, 0.60, 0.80)
    e3 = energy(left, rate, 1.10, 1.35)
    tail_silence = energy(left, rate, 1.40, 1.99)
    check(e1 > 1e-3, f"first note audible (energy {e1:.4f})")
    check(0.05 * e1 < e2 < 0.5 * e1, f"velocity 40 note is quieter but audible (energy {e2:.4f} vs {e1:.4f})")
    check(e3 > 1.3 * e1, f"overlapping notes carry more energy than one hit ({e3:.4f} vs {e1:.4f})")
    check(tail_silence == 0.0, f"silence between hits and after (energy {tail_silence:.3e})")
    hz = zero_crossing_hz(left, rate, 0.16, 0.27)          # hold region 0.147..0.279 s: ~6 cycles of 55 Hz
    check(abs(hz - 55.0) < 0.8, f"hold-region pitch of the first note = {hz:.2f} Hz (expected 55)")
    # A1 (1.10 s) ends at 1.279 s, A2 (1.15 s) at 1.329 s: 1.282..1.326 holds only the A2 voice (4.8 cycles).
    hz2 = zero_crossing_hz(left, rate, 1.282, 1.326)
    check(abs(hz2 - 110.0) < 2.0, f"second overlapping voice keeps its own pitch in an isolated window = {hz2:.2f} Hz (expected 110)")

    ref = np.fromfile(reference_hit, dtype="<f4").astype(np.float64)
    check(len(ref) == 8592, f"engine reference hit has 8592 samples ({len(ref)})")
    start = int(0.10 * rate)
    seg = left[start:start + len(ref)]
    err = float(np.max(np.abs(seg - ref))) if len(seg) == len(ref) else float("inf")
    check(err < 2e-4, f"REAPER render of the default hit matches the engine reference sample by sample (max err {err:.2e})")

    a_left = automation[:, 0]
    s0, s1 = int(2.0 * rate), int(2.8 * rate)
    same = float(np.max(np.abs(a_left[s0:s1] - left[s0:s1])))
    check(same == 0.0, f"long note unchanged by the Start Frequency/Shape/Drive envelope jumps at 2.5 s (max diff {same:.2e})")
    s2 = int(3.05 * rate)
    diff = float(np.max(np.abs(a_left[s1:s2] - left[s1:s2])))
    check(diff > 0.05, f"note triggered after the jump differs (max diff {diff:.3f})")
    check(float(np.max(np.abs(a_left[:s0] - left[:s0]))) == 0.0, "notes before the envelope jump are identical in both renders")
    hz_long = zero_crossing_hz(left, rate, 2.5, 2.7)
    check(hz_long > 0 and abs(hz_long - 55.0) < 0.8, f"long note (Hold envelope 1000 ms) holds 55 Hz at 2.5-2.7 s ({hz_long:.2f} Hz)")
    # The 2.8 s note also runs under the 1000 ms Hold envelope: it ends at 2.8 + 1.047 = 3.847 s.
    check(energy(left, rate, 3.86, 3.99) == 0.0, "silence after the last hit (3.86-3.99 s)")
    check(energy(left, rate, 3.30, 3.80) > 1e-3, "last long note still audible at 3.3-3.8 s (Hold envelope in effect)")
    # v1.3: velocity-off.wav is the control project rendered with the Velocity Sensitivity switch Off
    # (set through the host): the velocity-40 note plays at full level, everything else is unchanged.
    off, _ = read_wav(directory / "velocity-off.wav", rate=48000, min_seconds=3.9)
    o_left = off[:, 0]
    check(bool(np.all(np.isfinite(off))), "velocity-off render is finite")
    o1 = energy(o_left, rate, 0.10, 0.30)
    o2 = energy(o_left, rate, 0.60, 0.80)
    check(abs(o1 - e1) < 1e-6 * max(e1, 1e-12), f"switch Off: velocity-127 note unchanged (energy {o1:.4f} vs {e1:.4f})")
    check(abs(o2 - o1) < 1e-6 * max(o1, 1e-12), f"switch Off: velocity-40 note as loud as velocity 127 (energy {o2:.4f} vs {o1:.4f})")
    check(o2 > 1.9 * e2, f"switch Off vs On: the velocity-40 note is much louder ({o2:.4f} vs {e2:.4f})")
    first = float(np.max(np.abs(o_left[int(0.10 * rate):int(0.55 * rate)] - left[int(0.10 * rate):int(0.55 * rate)])))
    check(first == 0.0, f"switch Off: first hit sample-identical to the control render (max diff {first:.2e})")
    return check.finish("RENDER")


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--fresh-since" in args:
        i = args.index("--fresh-since")
        FRESH_SINCE = float(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        raise SystemExit(__doc__)
    directory = Path(args[0])
    mode = args[1]
    if mode == "--read":
        raise SystemExit(compare_read_renders(directory))
    if mode == "--lifecycle":
        raise SystemExit(compare_lifecycle_renders(directory))
    if mode == "--panic":
        raise SystemExit(compare_panic_renders(directory))
    if mode == "--program":
        raise SystemExit(compare_program_renders(directory)[0])
    raise SystemExit(main(directory, mode))
