# Oscillator timbre: waveform selector, Tilt, Tone

Status: planned (2026-09-25), to be implemented later. Decisions taken with the owner over three
conversations (2026-09-24 and 2026-09-25); the alternatives that lost are recorded under each
decision, not re-litigated. Three points the owner has agreed to in discussion but not seen
written are marked **(confirm)**: band-limited tables, the top-bar move of the input settings, and
the knob names. Confirm them with the owner before the first commit, do not stall on them.

Give the single oscillator more timbres while keeping the instrument what it is: one kick per
instance, every Note On freezes everything, one knob keeps one meaning, and the default sound is
the original's pure sine. Today the oscillator morphs between a sine table and a square table
(Shape). After this epic it morphs from the sine to a chosen target (Triangle, Square or Saw), a
Tilt knob bends the waveform to add even harmonics, a Tone knob closes a low-pass that follows the
pitch, and every table is band-limited so a sweep starting high does not alias.

The goal behind it, in the owner's words: cover about every kick there is, including the hard
styles (tribe, hardtek, hardstyle, gabber), for live use where any setting must still sound like a
kick. What this epic does *not* do, and why, is recorded under "Out of scope".

This file is self-sufficient: a session with no memory of the conversations can implement the
epic from it. Read it, then `CLAUDE.md`, `README.md` ("How it works"), `docs/development.md`,
`engine/KickEngine.h`, `engine/KickEngine.cpp`, `engine/KickParams.h`, `plugin/Parameters.h`,
`plugin/Parameters.cpp`, `plugin/PluginProcessor.cpp` (state schema), `plugin/Presets.cpp`
(preset schema), `plugin/PluginEditor.cpp` (the knob grid), `plugin/ui/Knob.cpp`,
`plugin/ui/TopBar.cpp`, `resources/presets/`, `tests/engine_tests.cpp` and
`tests/plugin_tests.cpp`. The original program the engine derives from is
`https://github.com/ArnaudValensi/kick-crafter-daisy` (`src/main.cpp`: a sine-to-square morph,
no other waveform, no filter).

## Scope

In: band-limited wavetables (the foundation, first); a Waveform choice parameter naming the
morph target of Shape; a Tilt parameter (phase distortion); a Tone parameter (pitch-following
low-pass); per-voice DC compensation for the tilted waveforms; the editor: the selector in the
Shape knob's cell, two new knobs, the input settings moved to the top bar; state schema 5 and
preset schema 5; three new factory presets; README, CHANGELOG, CLAUDE.md and the synthesis
diagram updated; version 1.6.0.

Out of scope, decided, recorded at the end: a distortion stage, a high-pass filter, a transient
("click") layer, a second oscillator, a filter envelope of its own.

## Settled decisions

1. **Band-limited tables, per harmonic count, sample-rate independent (confirm).** Each
   non-sine waveform exists as a set of 2048-point tables with 1024, 512, 256, ..., 2, 1
   harmonics (eleven levels), generated once in the `Wavetables` constructor by additive synthesis
   (each level is the previous one plus the next band of harmonics, so the whole set costs about
   `3 shapes × 1024 harmonics × 2048 points` sine evaluations, well under a second, once per
   engine, never on the audio thread). At render time a voice picks, per sample, the largest level
   whose top harmonic stays below Nyquist for the instantaneous frequency
   (`H ≤ floor(0.5 × sampleRate / f)`) and reads it by linear interpolation as today. Because the
   levels are defined by harmonic count and not by frequency, the same tables serve every sample
   rate; only the choice depends on `sampleRate`. Switching level mid-sweep is a small step in
   the upper harmonics; if a test hears it (a discontinuity above the harmonic content), crossfade
   the two neighbouring levels over the octave instead of switching. The sine table is untouched
   and keeps its own path, so a hit with Shape at 0 renders bit-identically to today (decision 8).

   Why: a square or a saw read raw during a sweep that starts at 1 or 2 kHz folds harmonics above
   Nyquist into inharmonic garbage; the saw makes it much worse than today's square, and "always a
   kick" cannot tolerate it. The README's sentence "The square table is not band-limited, on
   purpose" recorded fidelity to the original; the owner chose the user's experience over that
   fidelity. Alternatives rejected: oversampling (CPU for sixteen voices, for no gain over
   mipmapped tables); PolyBLEP (does not apply to a morph between arbitrary tables); leaving the
   square raw and band-limiting only the new shapes (two behaviours for one knob).

2. **Waveform: the target of the Shape morph, a choice parameter.** ID `waveform`, an
   `AudioParameterChoice` with `Triangle`, `Square`, `Saw` in that order, default `Square`
   (index 1). Shape keeps its ID, range and meaning: 0 % is the sine, 100 % is the chosen target,
   the morph is the same linear crossfade as today (`lerp (sine, target, morph)`). The triangle
   table is the standard one (odd harmonics, `1/n²`, alternating sign); the saw is the standard
   rising saw (all harmonics, `1/n`); the square is the one the engine has (odd harmonics, `1/n`),
   band-limited per decision 1.

   Why a selector and not one longer morph path (sine → triangle → square → saw on one knob):
   a chain only gives crossfades between neighbours, and the fades that matter for a kick all
   start at the sine (the sub); a chain loses today's sine → square fade (at 50 % it is not a
   triangle) and never offers sine → saw directly. Why Triangle at all: cheap, and the mildest
   filling of the sine. Why not a fourth shape (pulse with width): one more parameter for a
   variant of the square.

3. **Tilt: phase distortion, a continuous knob.** ID `tilt`, 0 to 100 %, default 0 %, stored as
   `float tilt` (0..1) in `KickParams`. The phase read from the table is warped before the
   lookup: with `a = 0.5 × (1 − tilt × 0.9)` (so `a` runs from 0.5 down to 0.05, never 0), a
   normalised phase `p < a` maps to `p / (2a)` and `p ≥ a` maps to `0.5 + (p − a) / (2 (1 − a))`.
   The same warped phase reads both the sine and the target table, so Tilt and Shape combine.
   At 0 % the warp is the identity, bit-identically (guard the branch: no warp arithmetic at all
   when `tilt == 0`).

   Band limit for Tilt: the warp adds harmonics of its own, roughly `1 / a` of them. The
   effective tilt of a voice sample is reduced when the instantaneous frequency is high:
   `tiltEffective = tilt × clamp ((0.5 × sampleRate / f − 2) / 30, 0, 1)`, so a 2 kHz start at
   48 kHz (Nyquist/f = 12) keeps a third of the tilt and a 55 Hz tail keeps all of it. The exact
   constants may move by ear; the acceptance test (decision 10) is what fixes them.

   Why Tilt: today nothing in the instrument produces even harmonics (Shape gives odd ones,
   Drive saturates symmetrically); the "fat", "boxy" body of a saturated 808 or a tribe kick is
   even harmonics. Why both Tilt and the selector: they overlap only near the saw (a tilted sine
   resembles a rounded saw, without the saw's exact spectrum) and each has territory the other
   does not (the exact triangle and saw spectra; the tilted square and every point of the
   Shape × Tilt plane). Alternative rejected: a second oscillator.

4. **DC compensation per voice, no bus filter.** A tilted waveform has a non-zero mean over a
   cycle. `VoiceSnapshot::make` (or `KickVoice::start`) computes the mean of one warped cycle of
   the voice's morphed waveform at the tilt it will use (2048 table reads, allocation-free,
   microseconds; `noteOn` runs on the audio thread, so no more than that) and the voice subtracts
   it from every sample. With `tilt == 0` the mean is zero by construction and nothing is
   subtracted, so decision 8 holds. Note that the band-limit reduction of decision 3 makes the
   effective tilt vary along the sweep; the compensation uses the tilt at the tail (`endHz`),
   where the voice spends its time, and the residual during the sweep is a short, small,
   fading offset. Alternative rejected: a one-pole high-pass DC blocker on the bus (would change
   every existing sound by a phase shift and a fraction of a dB, breaking decision 8).

5. **Tone: a low-pass that follows the pitch.** ID `tone`, a knob in octaves above the
   instantaneous frequency, 1.0 to 8.0, default 8.0, displayed `Off` at 8.0 and `+N.N oct`
   otherwise. Per voice, a two-pole low-pass (a TPT state-variable filter, no resonance
   parameter, Q fixed at `1/√2`) whose cutoff is `f × 2^tone`, clamped to `0.45 × sampleRate`;
   the coefficient is recomputed per sample (a `tan` per sample per voice is affordable: sixteen
   voices at 48 kHz is under a million per second) or, if a profile shows it, every 16 samples.
   At 8.0 the filter is bypassed entirely (not "open": no processing, so decision 8 holds), and
   the bypass is decided per voice at Note On like everything else.

   Why pitch-following and not a cutoff envelope of its own: the pitch sweep already is the
   kick's envelope of brightness; a cutoff a fixed number of octaves above it opens on the attack
   and closes on the tail without two or three more parameters, and it is the one thing a filter
   in the host's chain cannot do (the chain does not know the instantaneous pitch). Why a
   low-pass and not a high-pass at the note: nothing exists below the fundamental in this signal
   (one table per voice, harmonics only, zero-mean tables, DC compensated), and a steep high-pass
   near the fundamental softens the transient by its group delay; a high-pass for mixing purposes
   belongs to the track. Why the filter stays although the distortion does not: pre-shaping what
   an external distortion receives is what makes that distortion musical on this kick (bright
   attack, dark tail), and it also serves alone (a rounder kick).

6. **Parameter order, schemas, migration.** `waveform`, `tilt`, `tone` are appended, in that
   order, after `pitchSource` in `synthesisIds` and before `midiChannel` in `allIds`
   (`midiChannel` stays last: it is not a synthesis parameter). The tests that index
   `synthesisIds.size() - 2` for `velocity` are updated. The A/B "Other" slot array grows with
   `synthesisIds`. State schema becomes 5, preset schema becomes 5; a schema 1 to 4 document
   loads with `waveform = Square`, `tilt = 0`, `tone = 8` (the defaults), which reproduces its
   sound exactly for Shape at 0 and closely otherwise (decision 8); the version policy comments
   in `PluginProcessor.cpp` and `Presets.cpp` gain the line for 5. Preset XML: three new
   attributes `waveform` (`0`, `1`, `2`), `tilt` (0..100), `tone` (1..8); `toXml` writes all,
   `fromXml` accepts their absence in a schema ≤ 4 document and requires them in a schema 5 one.
   The eight existing factory presets get the three attributes with the default values and keep
   their names and every other value.

7. **Editor.** Eleven knobs. The right panel becomes a grid of 3 columns × 4 rows of knob cells
   (12 cells): the nine knobs in today's order, then Tilt, then Tone, then the Velocity switch in
   the twelfth cell (styled as today, aligned on the same row). The two input settings that
   remain, Pitch source (Fixed / MIDI Note) and MIDI channel, move to the top bar, right of the
   preset controls and left of the LED, with their labels above them as today **(confirm)**. The
   editor stays 1000 × 640 logical pixels; no control gets narrower than it is today; the top bar
   may grow to two rows if one row cannot hold them without shrinking the preset box below 200 px.
   The selector of decision 2 lives in the Shape knob's cell: the caption line under the dial (the
   line that shows the note name under the frequency knobs, empty for the others) shows the
   target's name (`Triangle`, `Square`, `Saw`); a click on it cycles to the next, a right-click
   opens a menu with the three, the tooltip says both; the caption is a polled binding like the
   knob's value, never a listener (`CLAUDE.md`). The value label of Shape stays a percentage.
   Tilt's value reads `N %`; Tone's reads `Off` or `+N.N oct`. The tooltips say what each does in
   one sentence and name the sound it is for. Every knob arc stays copper (1.5.0). The graphs
   need no change: the waveform preview already renders the hit with the voice code.

   Why the input settings move: they describe how notes are interpreted (the pitch source, the
   channel), which is what the LED and the voice count next to them already report; they are not
   synthesis controls and had the last row only because it was free. Alternatives rejected: a
   fourth knob column (dials of 87 px, too small); shrinking the graphs (they are the instrument's
   main display).

8. **The default sound is bit-identical to today.** With Shape at 0 (whatever the target), Tilt
   at 0 and Tone at 8, the rendered hit is bit-identical to the current engine's, for every other
   parameter value. This is a test (decision 10), and it is why the sine keeps its own table, the
   warp has an identity branch, the DC compensation is zero at tilt 0 and the filter is bypassed
   rather than open. Hits with Shape above 0 change slightly (the square is now band-limited);
   the owner accepts it, the five factory presets concerned (Techno Punch, Tight Click, Square
   Growl, Long Boom, Gabber) are listened to and kept unless they lost their character, in which
   case their Shape value is retuned by ear and the change is noted in the journal.

9. **Presets.** Three new factory presets, appended after Gabber (the order is the file prefix):
   `09-hardtek.xml` "Hardtek" (Saw target, Shape around 40 %, Tilt around 50 %, Tone around
   3 oct, long hold, MIDI Note source so it plays as the bass line), `10-fat-808.xml` "Fat 808"
   (Square target, Shape low, Tilt around 60 %, Tone around 2 oct, low start frequency, long hold,
   high fade), `11-tribal-tom.xml` "Tribal Tom" (Triangle target, Shape around 70 %, Tilt 0,
   Tone around 4 oct, medium sweep, curve above 1). The values are starting points: tune by ear,
   record the final values in the journal, and ask the owner to listen at the end (invariant 5 of
   the epic skill). The factory-bank test (the "v1.1 table" case) grows by three rows.

10. **Validation.** Engine tests: (a) decision 8 as a bit-identity test over the eight current
    factory presets with Shape forced to 0, against a reference rendered by the current engine
    (record the reference hashes in the test from a render made *before* the engine changes,
    on the commit this epic starts from); (b) aliasing: a sustained tone at the highest start
    frequency (2 kHz at 48 kHz) with Saw at 100 % and Tilt at 100 % has no spectral line above
    −50 dB relative to the fundamental at a frequency that is not a multiple of the fundamental
    (a 4096-point FFT over the tail; use the same tolerance for Square and Triangle); (c) each
    target at 100 % matches its analytic spectrum on the first ten harmonics within 1 dB at
    55 Hz; (d) Tilt at 100 % on a sine at 55 Hz produces a second harmonic above −20 dB and a
    cycle mean below 1e-4 after compensation; (e) Tone: at 2 octaves the third harmonic of a
    55 Hz saw is down by more than 12 dB relative to Tone off, and the render stays finite and
    declicked (the existing steal test with Tone engaged); (f) determinism across block sizes
    with the three new parameters set. Plugin tests: the three parameters exist with the ranges,
    defaults and texts above; state schema 5 round-trips and a schema 4 document loads with the
    defaults; preset schema 5 round-trips and a schema 4 preset loads; the factory bank test; the
    editor test finds the two knobs, the selector caption (click cycles, right-click menu, host
    refresh without a gesture) and the moved input settings, and the geometry rules of decision 7.
    Gates: `./run validate` after the engine work and after the editor work (the REAPER `analyze`
    stage compares a render with the engine's reference hit: unchanged, since the default is
    bit-identical), `./run memory` both passes before the version tag (a new filter and new tables
    are new code paths in the voice). Captures under `artifacts/screenshots/` read by eye for the
    new grid and the top bar.

11. **Documents.** README "How it works": the table's rows for the oscillator, a new row for
    Tilt, a new row for Tone, the sentence about the square not being band-limited removed and
    replaced by one sentence on the mipmapped tables; the "Controls" bullets for the selector and
    the moved settings; the parameters table (three rows, "All fifteen can be automated"). The
    synthesis diagram (`tools/diagrams/synthesis_diagram.py`) gains the Tone filter between the
    amplifier and the drive, and the Tilt input on the oscillator; render it, look at it, commit
    script and SVG together. `CHANGELOG.md`: the 1.6.0 entry (state schema 5, preset schema 5).
    `CLAUDE.md`: the closed decisions gain one line each for the band-limited tables, the
    pitch-following filter, and the input settings in the top bar; the open decision about the
    Start Frequency range stays open. `docs/development.md`: the preset XML example gains the
    three attributes; nothing else unless a tool changes.

12. **Version 1.6.0**, one release at the end, tagged and pushed by the owner (or on the owner's
    explicit request, as the 1.5.x releases were).

## Out of scope, decided

- **No distortion stage.** The owner leaves saturation and distortion to the track's chain
  (clipper, saturator, EQ): a source with the right material at the right moment (this epic's
  Tone) is what such a chain needs, and a distortion with its own tone control is a product of
  its own. Drive stays the original's soft limiter, a light colour. If one day a distortion is
  wanted inside, the notes of 2026-09-25 hold the design (Soft / Hard / Asymmetric character in
  Drive's cell, the Tone filter after it).
- **No high-pass filter.** Nothing exists below the fundamental in this signal; a high-pass for
  mixing belongs to the track; a steep one near the note softens the transient.
- **No transient layer.** A click or noise burst at the attack is the next most audible gap
  for techno, DnB and the hard styles, and the next epic; it is a separate voice component, not
  a property of the waveform.
- **No filter envelope of its own**, no LFO, no second oscillator: the pitch sweep is the only
  modulation source, by design.
