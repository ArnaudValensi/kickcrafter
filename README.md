# KickCrafter

A kick-drum synthesizer plug-in. One kick per instance, played with MIDI, every hit frozen the
moment it starts.

![KickCrafter editor](docs/screenshots/editor.png)

KickCrafter is a Linux VST3 instrument derived from the Daisy KickCrafter hardware firmware. The
sound is a 2048-point wavetable oscillator morphing between sine and square, a pitch sweep with an
adjustable curve, a linear attack / hold / fade envelope, per-hit drive and a soft limiter on the
bus. Every Note On snapshots all synthesis parameters into the new voice, so knobs, graph handles,
automation, presets and A/B switching only ever shape the *next* hit: voices already sounding are
never retuned.

- Linux x86_64, VST3 (and a Standalone build for UI work). Built with JUCE 8, C++20, CMake.
- Twelve host-automatable parameters with stable IDs, block-accurate automation, sample-accurate MIDI.
- Sixteen voices with declicked voice stealing; nothing is allocated or locked on the audio thread.
- Interactive pitch, amplitude and waveform graphs that draw the next hit and can be dragged.
- Eight factory presets, unlimited user presets as plain XML files, A/B comparison, resizable UI.
- Free software: AGPL-3.0-or-later.

## Installation

Binary releases are on their way (built and tested by CI). Until then, build the plug-in from
source: it takes a few minutes, see [docs/development.md](docs/development.md#building-from-source).

Install by copying the `KickCrafter Fable.vst3` bundle into a folder your host scans, for example
`~/.vst3/`, and rescan plug-ins. The plug-in appears as **KickCrafter Fable** (vendor Arnaud Valensi)
in the instrument list. Insert it on a track and send it MIDI notes.

## Playing it

- **Notes.** Every MIDI channel is accepted by default; **MIDI Channel** restricts input to one
  channel. Note Off is ignored: hits are one-shot and end by themselves. A Note On with velocity 0
  does not trigger.
- **Pitch Source.** *Fixed* (default): the **End** knob is the note of the kick and the played note
  number is ignored (55 Hz = A1 reproduces the reference sound). *MIDI Note*: the played note sets
  the end frequency of the sweep, so the kick can be played melodically.
- **Velocity.** On (default): the MIDI velocity scales the level linearly, a velocity-96 note plays
  at 96/127 of the level. Off: every note plays at full level, as the hardware did.
- **Audition** fires an A1 hit at full velocity through the same realtime path as a MIDI note.
- **Knobs.** Drag, mouse wheel, Shift for fine control, double-click to reset, click the value to
  type it (`440`, `440 Hz`, or a note name such as `A1` or `C#2` for the frequency knobs). The arc
  colour of a knob is the colour of the graph that edits the same value: copper = pitch graph,
  amber = amplitude graph, ivory = level and tone.
- **Graphs.** Circles move frequencies, diamonds move times, the square knee moves the sweep time.
  Dragging a time handle past the right edge keeps growing the value; the axis rescales on release.
  Shift-drag is fine, double-click resets the handle's parameter. The waveform shows the next hit
  with markers for the attack, the end of the sweep and the start of the fade.
- **A/B.** `A` and `B` hold two complete settings; `A>B` copies the current one into the other slot.
  Both slots are saved with the project.
- **Window size.** Drag the host window corner (80–160 %) or pick a size in the `%` menu. The size
  is saved with the project.

### Presets

The preset list has two sections. *Factory* holds the eight presets shipped in the plug-in (read
only). *User* holds your own, one plain XML file each in `~/.config/KickCrafterFable/Presets/`
(set `KCF_PRESET_DIR` to use another folder). The `...` button offers Save (overwrites the loaded
user preset; on a factory preset it behaves as Save as), Save as…, Rename…, Delete, Open user
presets folder and Rescan. "• edited" after the name means the current values differ from the
loaded preset.

A project remembers its preset by kind and name. If the file has been removed, the name shows
"(missing)" and the sound is unchanged: the values live in the project. A preset file looks like
this; values are in the units the knobs display, out-of-range values are clamped and malformed files
are skipped (the list shows how many, Rescan tells why):

```xml
<KickCrafterPreset version="4" name="Deep Sub">
  <Params startFreq="140" endFreq="55" sweep="38" hold="820" fade="88" attack="0.4"
          curve="2.5" shape="0" drive="1.15" velocity="1" pitchSource="0"/>
</KickCrafterPreset>
```

Factory presets are the files under `resources/presets/`; edit or add files there (the numeric
prefix fixes the order) and rebuild to change the bank.

## Parameters

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `startFreq` | Start Frequency | 20–2000 Hz, log | 250 Hz | sweep start |
| `endFreq` | End Frequency | 20–440 Hz, log | 55 Hz | the note, in Fixed mode |
| `sweep` | Sweep Time | 0–250 ms | 47 ms | |
| `hold` | Hold Time | 0–1000 ms | 132 ms | hit length = sweep + hold |
| `fade` | Fade Out | 0–100 % | 50 % | fraction of the hit: 0 % = 10 ms fade, 100 % = whole hit |
| `attack` | Attack | 0.4–50 ms | 0.4 ms | linear ramp-in |
| `curve` | Sweep Curve | 1–30, log | 1 | pitch = start + (end − start)·(1 − (1 − t)^curve) |
| `shape` | Shape | 0–100 % | 0 % | 0 = sine, 100 = square (table morph) |
| `drive` | Drive | 0–4 × | 1 × | per-hit gain before the bus limiter |
| `velocity` | Velocity Sensitivity | Off / On | On | |
| `pitchSource` | Pitch Source | Fixed / MIDI Note | Fixed | |
| `midiChannel` | MIDI Channel | Omni, 1–16 | Omni | not a synthesis parameter |

Automation is applied at block boundaries (the last automation point of a block is in effect for
the whole block); MIDI notes are sample-accurate within the block. All Sound Off (CC 120) stops
every voice with a short declick and All Notes Off (CC 123) is a deliberate no-op, but VST3 hosts
do not deliver raw CC messages to the plug-in (see [docs/architecture.md](docs/architecture.md)).

Project state is XML with a schema number. Every schema since 1.0 is still read; older documents
are migrated on load (Pitch Source order, Velocity switch, preset identity). Host-side copies of
parameter values, such as automation lanes written by an older version, are not migrated: the
[changelog](CHANGELOG.md) lists what to check when opening old projects.

## Known limitations

- Linux x86_64 VST3 and Standalone only. macOS, Windows, AU, LV2 and CLAP are not built or validated.
- Automation is block-accurate, not sample-accurate.
- No pitch bend, MPE, LFOs, multi-point envelopes, sample import, sequencer, kit or WAV export.
- The naive square wavetable aliases at high start frequencies (about 21 dB below the harmonics at
  44.1/48 kHz, 25 dB at 96 kHz); this is deliberate fidelity to the hardware. Use Shape below 100 %
  or a higher sample rate for cleaner square-heavy sounds.
- The 16-voice steal policy cuts the tail closest to its end (declicked) rather than dropping the new note.

## Documentation

- [docs/architecture.md](docs/architecture.md): the synthesis block diagram, the per-hit
  snapshot rule, the gain and limiting topology, state and preset formats, deviations from the hardware.
- [docs/development.md](docs/development.md): building, the test suites, the VST3 validator, the
  REAPER host validation harness, the memory-leak gate and packaging.
- [CHANGELOG.md](CHANGELOG.md).

## Licence

KickCrafter is free software under the GNU Affero General Public License, version 3 or later
(`LICENSE`). It uses JUCE under its AGPLv3 option, the Steinberg VST3 SDK under GPLv3, IBM Plex
fonts under the SIL Open Font License and a formula from DaisySP (MIT); see
`THIRD_PARTY_NOTICES.md`. VST is a trademark of Steinberg Media Technologies GmbH.
