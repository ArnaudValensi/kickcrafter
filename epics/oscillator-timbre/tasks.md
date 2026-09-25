# Tasks: oscillator timbre

Four milestones. Each ends with the validation `requirements.md` names for it (decision 10), a
journal entry in this epic's `journal/` (dated from `date +%F`) and a commit. Mark a milestone
**Done** here, with a short record of what is in place, in the commit that closes it.

## M0: The reference

Before touching the engine: render the eight factory presets with Shape forced to 0 (and once as
they are) at 48 kHz with the current engine, hash the outputs, and write the hashes into the new
bit-identity test so it fails the moment the default path changes. Commit the test alone, green.

## M1: The engine

Band-limited table sets (decision 1), the Waveform target (2), Tilt with its band limit (3), the
DC compensation (4), Tone (5), the three fields in `KickParams` with `sanitized()` and
`operator==`, `VoiceSnapshot` and `KickVoice::renderAdd`, `renderSingleHit` unchanged in shape.
Engine tests (a) to (f). Acceptance: `./run check` green, the bit-identity test green, the aliasing
test green at 44.1, 48 and 96 kHz, `./run asan` green. Listen to the five factory presets with
Shape above 0 and record the verdict in the journal.

## M2: Parameters, state, presets

The three parameters in `Parameters.cpp` with their texts, the id arrays, the A/B slot array,
state schema 5 and preset schema 5 with their migrations (decision 6), the eight factory files
with the three attributes, the three new presets (9) tuned by ear, the plugin tests of decision 10.
Acceptance: `./run validate` green (the REAPER `setup` stage discovers fifteen parameters; check
the formatted texts it logs), the factory-bank test with eleven rows, the preset and state
round-trips.

## M3: The editor, the documents, the release

The 3 × 4 grid, the two knobs, the selector caption in the Shape cell, Pitch source and MIDI
channel in the top bar (decision 7), the editor tests, the documents and the diagram (11), the
CHANGELOG entry and version 1.6.0 (12). Acceptance: `./run validate` green, captures read by eye
(the grid, the top bar at 100 % and 125 %, the selector caption), `./run memory` both passes green,
the journal entry, then the owner's listening pass on the three new presets and the tag.
