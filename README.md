# KickCrafter

A kick-drum synthesizer plug-in. One kick per instance, played with MIDI, every hit frozen the
moment it starts.

![KickCrafter editor](docs/screenshots/editor.png)

KickCrafter is a Linux VST3 instrument. The sound is a wavetable oscillator morphing between sine
and square, a pitch sweep with an adjustable curve, an attack / hold / fade envelope, per-hit drive
and a soft limiter. Every Note On snapshots all parameters into the new voice, so knobs, graph
handles, automation, presets and A/B switching only ever shape the *next* hit: voices already
sounding are never retuned.

- Sixteen voices, so fast rolls and long tails overlap without cutting each other.
- Pitch, amplitude and waveform graphs that draw the next hit and can be dragged.
- Factory and user presets, A/B comparison, a resizable window.
- Free software, AGPL-3.0-or-later.

## Installation

Binary releases are on their way. Until then, build the plug-in from source: it takes a few
minutes, see [docs/development.md](docs/development.md#building-from-source).

Copy the `KickCrafter Fable.vst3` bundle into a folder your host scans, for example `~/.vst3/`, and
rescan plug-ins. The plug-in appears as **KickCrafter Fable** (vendor Arnaud Valensi) in the
instrument list. Insert it on a track and send it MIDI notes.

## Playing it

- **Notes.** Every MIDI channel is accepted by default; **MIDI Channel** restricts input to one
  channel. Note Off is ignored: hits are one-shot and end by themselves. A Note On with velocity 0
  does not trigger.
- **Pitch Source.** *Fixed* (default): the **End** knob is the note of the kick and the played note
  number is ignored (55 Hz = A1). *MIDI Note*: the played note sets the end frequency of the sweep,
  so the kick can be played melodically.
- **Velocity.** On (default): the MIDI velocity scales the level linearly, a velocity-96 note plays
  at 96/127 of the level. Off: every note plays at full level.
- **Audition** fires an A1 hit at full velocity through the same path as a MIDI note.
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

## Parameters

| Name | Range | Default | What it does |
|---|---|---|---|
| Start Frequency | 20–2000 Hz | 250 Hz | where the pitch sweep starts |
| End Frequency | 20–440 Hz | 55 Hz | the note of the kick (Pitch Source = Fixed) |
| Sweep Time | 0–250 ms | 47 ms | how long the pitch takes to reach End |
| Sweep Curve | 1–30 | 1 | 1 = straight pitch drop, higher = the pitch drops faster at first |
| Hold Time | 0–1000 ms | 132 ms | how long the hit continues after the sweep (hit = sweep + hold) |
| Attack | 0.4–50 ms | 0.4 ms | ramp-in at the start of the hit |
| Fade Out | 0–100 % | 50 % | fade-out length as a fraction of the hit (0 % = 10 ms, 100 % = the whole hit) |
| Shape | 0–100 % | 0 % | 0 = sine, 100 = square |
| Drive | 0–4 × | 1 × | per-hit gain before the limiter; above 1 × it saturates |
| Velocity Sensitivity | Off / On | On | whether MIDI velocity scales the level |
| Pitch Source | Fixed / MIDI Note | Fixed | where the end frequency comes from |
| MIDI Channel | Omni, 1–16 | Omni | input channel filter |

All twelve can be automated. Automation is applied at block boundaries; MIDI notes are
sample-accurate. Projects saved by earlier versions load as they were; the
[changelog](CHANGELOG.md) lists the few controls to check when a parameter's meaning changed.

## Presets

The preset list has a *Factory* section (eight presets shipped in the plug-in) and a *User* section
(your own). The `...` button saves, renames, deletes, opens the user presets folder and rescans it.
User presets are plain XML files in `~/.config/KickCrafterFable/Presets/`, one per preset, easy to
back up or share; "• edited" after a name means the current values differ from the loaded preset.

## Known limitations

- Linux x86_64 VST3 only. macOS, Windows, AU, LV2 and CLAP are not built or validated.
- No pitch bend, MPE, LFOs, multi-point envelopes, sample import, sequencer, kit or WAV export.
- The square waveform is not band-limited (about 21 dB of aliasing below the harmonics at
  44.1/48 kHz, 25 dB at 96 kHz), by design. Use Shape below 100 % or a higher sample rate for
  cleaner square-heavy sounds.

## Documentation

- [docs/architecture.md](docs/architecture.md): how the synthesis works, in one diagram.
- [docs/development.md](docs/development.md): building, tests, validation and packaging.
- [CHANGELOG.md](CHANGELOG.md).

## Licence

KickCrafter is free software under the GNU Affero General Public License, version 3 or later
(`LICENSE`). It uses JUCE under its AGPLv3 option, the Steinberg VST3 SDK under GPLv3 and the
IBM Plex fonts under the SIL Open Font License; see `THIRD_PARTY_NOTICES.md`. VST is a trademark of
Steinberg Media Technologies GmbH.
