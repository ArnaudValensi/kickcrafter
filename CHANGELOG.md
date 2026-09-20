# Changelog

All notable changes to KickCrafter. Versions follow the `project()` version in `CMakeLists.txt`.
State documents carry a schema number; every schema since 1 is still read.

## 1.3.0 — 2026-09-20

- Velocity Sensitivity is a switch (On/Off, default On) instead of a 0–100 % amount. Off plays
  every note at full level, as the Daisy original. State schema 4 and preset schema 4; older
  documents migrate (any amount above 0 % becomes On). Host-side copies of the value (automation
  lanes, stored parameter values) are not migrated and read On at 0.5 and above.
- All nine dials have the same size (the two frequency dials used to be smaller).

## 1.2.0 — 2026-09-19

- User presets: Factory and User sections in the preset list; `...` menu with Save, Save as…,
  Rename…, Delete, Open user presets folder and Rescan. User presets are plain XML files in
  `~/.config/KickCrafterFable/Presets/` (`$KCF_PRESET_DIR` overrides the folder).
- Factory presets are XML files under `resources/presets/`, compiled into the binary.
- A project remembers the loaded preset by kind and name; a removed user preset shows "(missing)"
  and keeps its values. State schema 3.
- Dialogs are owned by the editor and cannot outlive it; files that fail to load are listed as
  skipped and reported by Rescan.

## 1.1.0 — 2026-09-19

- Pitch Source lists Fixed first and defaults to it (state schema 2, schema-1 documents migrate).
- Graphs: the knee moves only the sweep time, the curve handle spans the full 1–30 range, time
  handles can be dragged past the visible axis, the waveform shows attack / sweep end / fade
  markers.
- Knob arcs use the colour of the graph that edits the same value.
- Re-picking the already selected preset reloads it.
- No synthetic "MIDI CC" host parameters (JUCE's emulation is switched off): the host lists the
  twelve real parameters plus Bypass.

## 1.0.0 — 2026-09-19

- First release: Linux x86_64 VST3 (and Standalone), JUCE 8.0.9, AGPL-3.0-or-later.
- Wavetable kick engine derived from the Daisy KickCrafter firmware, per-hit parameter snapshot,
  16-voice pool with declicked stealing, bus soft limiter, factory presets, A/B, scalable UI.
