# Architecture

```mermaid
flowchart LR
    classDef audio fill:#fbe9d7,stroke:#b5651d,stroke-width:1.5px
    classDef mod fill:#e9eef9,stroke:#4a6fa5
    classDef trig fill:#eeeeee,stroke:#777777
    OSC["Wavetable oscillator<br/>2048-point sine and square tables"]:::audio --> VCA["Amplifier"]:::audio --> DRIVE["Drive<br/>0–4 ×"]:::audio --> LIM["Soft limiter<br/>x·(27+x²)/(27+9x²), clamped"]:::audio --> OUT(["Output"]):::audio
    GATE(["Note On"]):::trig -. "trigger" .-> PENV["Pitch envelope<br/>Start → End · Sweep time · Curve"]:::mod
    GATE -. "trigger" .-> AENV["Amplitude envelope<br/>Attack · Hold · Fade"]:::mod
    NOTE["MIDI note<br/>Pitch Source = MIDI Note"]:::mod -. "end frequency" .-> PENV
    PENV -. "frequency" .-> OSC
    SHAPE["Shape<br/>sine ↔ square morph"]:::mod -. "waveform" .-> OSC
    AENV -. "level" .-> VCA
    VEL["MIDI velocity<br/>Velocity switch On"]:::mod -. "level" .-> VCA
```

The audio path is the classic one-oscillator kick: an oscillator, an amplifier, a gain stage and a
limiter. Everything that shapes a hit is a modulation source, and every Note On freezes all of them
into the new voice: a sounding hit never changes, controls only shape the next one.

| Module | What it does | Parameters |
|---|---|---|
| Wavetable oscillator | Reads a 2048-point table by linear interpolation; the waveform is a morph between the sine table and the square table. | Shape |
| Pitch envelope | Sweeps the oscillator frequency from Start to End over the sweep time along `start + (end - start) · (1 - (1 - t)^curve)`, then holds End. In MIDI Note mode the played note sets End. | Start, End, Sweep, Curve, Pitch Source |
| Amplitude envelope | Linear attack, hold at full level, linear fade to silence; the hit lasts sweep + hold, the fade is a fraction of it. | Attack, Hold, Fade |
| Amplifier | Envelope level × MIDI velocity (127 = full) when the Velocity switch is on, envelope level alone when it is off. | Velocity |
| Drive | Per-hit gain, 0 to 4 ×, applied inside the voice so it is frozen with the rest. | Drive |
| Soft limiter | `x · (27 + x²) / (27 + 9x²)` on the sum of all voices, clamped to ±1 for |x| ≥ 3. The only stage shared by the voices. | none |

There is no filter, no LFO and no second oscillator: the character comes from the sweep curve, the
sine-to-square morph and the drive into the limiter. Sixteen voices; when all are busy the one
closest to its end is stolen with a short fade so a steal never clicks. The square table is not
band-limited, on purpose.
