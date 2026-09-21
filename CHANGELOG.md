# Changelog

All notable changes to KickCrafter. Versions follow the `project()` version in `CMakeLists.txt`.
State documents carry a schema number; every schema since 1 is still read.

## 1.5.0 — 2026-09-21

- The plug-in is named **KickCrafter** everywhere; it was "KickCrafter Fable" inside hosts. The
  bundle is now `KickCrafter.vst3` (`KickCrafter.component` on macOS) and the archives are named
  `kickcrafter-<version>-...`. The plug-in codes are unchanged, so hosts that match by identity
  keep their projects; a host that matches by file name needs the plug-in reinserted. Saved state
  written under the old root tag still loads. The user preset folder is `KickCrafter/Presets`
  under the platform's application data folder; a library found under the old
  `KickCrafterFable/Presets` is moved there once.
- Velocity Sensitivity is Off by default (every note at full level, as the original); the
  factory presets follow. Projects saved by earlier versions keep their stored value. No state or
  preset schema change.
- Pitch graph: the curve handle follows the mouse (it used to move against it on a falling
  sweep). Dragging down steepens a falling sweep, dragging up steepens a rising one.
- Every graph handle is a circle (the square knee and the diamond time handles are gone); every
  knob arc has the same copper colour and the unit marks next to the knob titles are gone.
- The wordmark reads "KICKCRAFTER" (no "FABLE"); the plug-in name inside hosts is unchanged.
- The last-note display reads `A1 vel 100` instead of `A1 v100`; captions and the footer start
  every segment with a capital.

## 1.4.0 — 2026-09-20

- Builds on macOS (VST3 and Audio Unit, one universal binary for Apple Silicon and Intel, macOS 11
  or later) and on Windows (VST3, x64) besides Linux (VST3, x86_64). The macOS and Windows builds
  come from GitHub Actions, which also runs the engine tests on each platform; they are not yet
  validated in a host by the maintainer, unlike the Linux build.
- The user preset folder follows each platform's convention: `~/.config/KickCrafterFable/Presets/`
  on Linux (unchanged), `~/Library/Application Support/KickCrafterFable/Presets/` on macOS,
  `%APPDATA%\KickCrafterFable\Presets\` on Windows. `KCF_PRESET_DIR` still overrides it.
- Binary releases on GitHub: one archive per platform, named after the version, with the licence
  texts and an `INSTALL.txt`. No state or preset schema change.

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
