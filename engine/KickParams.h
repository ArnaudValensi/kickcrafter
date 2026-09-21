// KickCrafter - hardware independent kick synthesis engine.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This header has no JUCE or host dependencies. It describes the musical
// parameters of one kick hit and the fixed bounds that keep synthesis safe.
#pragma once

#include <cmath>
#include <cstdint>

namespace kcf
{

enum class PitchSource : int
{
    midiNote = 0,   // end frequency follows the triggering MIDI note
    fixed = 1       // end frequency is the "End Frequency" parameter
};

// Hard bounds of the engine. The plugin parameter ranges use the same values;
// the engine re-clamps everything so that malformed input can never reach the
// voices.
struct Limits
{
    static constexpr float minHz            = 20.0f;
    static constexpr float maxStartHz       = 2000.0f;
    static constexpr float maxEndHz         = 440.0f;
    static constexpr float maxNoteHz        = 2000.0f;  // MIDI-driven end frequency bound
    static constexpr float maxSweepSec      = 0.250f;
    static constexpr float maxHoldSec       = 1.000f;
    static constexpr float minAttackSec     = 0.4f / 1000.0f;
    static constexpr float maxAttackSec     = 0.050f;
    static constexpr float minSlope         = 1.0f;
    static constexpr float maxSlope         = 30.0f;
    static constexpr float maxGain          = 4.0f;
    static constexpr float minFadeSec       = 0.010f;   // reference remap floor
    static constexpr int   maxVoices        = 16;
    static constexpr double minSampleRate   = 8000.0;
    static constexpr double maxSampleRate   = 384000.0;
    static constexpr int   wavetableSize    = 2048;
    static constexpr int   stealDeclickSamples = 64;
};

struct KickParams
{
    // Defaults are written as the same divisions the plugin uses to convert its
    // display units (ms, %), so engine defaults and host defaults are bit-identical.
    float startHz             = 250.0f;            // 20 .. 2000, log
    float endHz               = 55.0f;             // 20 .. 440, log (fixed pitch mode)
    float sweepSec            = 47.0f / 1000.0f;   // 0 .. 0.25
    float holdSec             = 132.0f / 1000.0f;  // 0 .. 1.0
    float fadeFraction        = 50.0f / 100.0f;    // 0 .. 1 of total duration (see fadeSeconds)
    float attackSec           = 0.4f / 1000.0f;    // 0.0004 .. 0.05
    float slope               = 1.0f;     // 1 .. 30, log
    float morph               = 0.0f;     // 0 = sine, 1 = square
    float gain                = 1.0f;     // 0 .. 4, applied before the bus limiter
    bool  velocitySensitive   = false;    // MIDI velocity scales the level (on/off since v1.3, Off by default since v1.5; was a 0..1 amount)
    PitchSource pitchSource   = PitchSource::fixed;     // v1.1: the End knob sets the note by default

    // Returns a copy where NaN/Inf are replaced by defaults and every value is
    // clamped into the engine bounds.
    KickParams sanitized() const noexcept;

    // Total hit duration (sweep + hold) in seconds.
    float totalSeconds() const noexcept { return sweepSec + holdSec; }

    // Reference mapping: fade fraction 0 -> 10 ms, 1 -> whole duration,
    // never longer than the hit itself.
    float fadeSeconds() const noexcept;

    bool operator== (const KickParams& o) const noexcept
    {
        return startHz == o.startHz && endHz == o.endHz && sweepSec == o.sweepSec
            && holdSec == o.holdSec && fadeFraction == o.fadeFraction && attackSec == o.attackSec
            && slope == o.slope && morph == o.morph && gain == o.gain
            && velocitySensitive == o.velocitySensitive && pitchSource == o.pitchSource;
    }
    bool operator!= (const KickParams& o) const noexcept { return ! (*this == o); }
};

inline float sanitizeValue (float value, float fallback, float lo, float hi) noexcept
{
    if (! std::isfinite (value))
        value = fallback;
    return value < lo ? lo : (value > hi ? hi : value);
}

inline KickParams KickParams::sanitized() const noexcept
{
    const KickParams d;
    KickParams p;
    p.startHz             = sanitizeValue (startHz, d.startHz, Limits::minHz, Limits::maxStartHz);
    p.endHz               = sanitizeValue (endHz, d.endHz, Limits::minHz, Limits::maxEndHz);
    p.sweepSec            = sanitizeValue (sweepSec, d.sweepSec, 0.0f, Limits::maxSweepSec);
    p.holdSec             = sanitizeValue (holdSec, d.holdSec, 0.0f, Limits::maxHoldSec);
    p.fadeFraction        = sanitizeValue (fadeFraction, d.fadeFraction, 0.0f, 1.0f);
    p.attackSec           = sanitizeValue (attackSec, d.attackSec, Limits::minAttackSec, Limits::maxAttackSec);
    p.slope               = sanitizeValue (slope, d.slope, Limits::minSlope, Limits::maxSlope);
    p.morph               = sanitizeValue (morph, d.morph, 0.0f, 1.0f);
    p.gain                = sanitizeValue (gain, d.gain, 0.0f, Limits::maxGain);
    p.velocitySensitive   = velocitySensitive;
    p.pitchSource         = pitchSource == PitchSource::midiNote ? PitchSource::midiNote : d.pitchSource;
    return p;
}

inline float KickParams::fadeSeconds() const noexcept
{
    const float total = totalSeconds();
    const float fade = Limits::minFadeSec + fadeFraction * (total - Limits::minFadeSec);
    return fade < 0.0f ? 0.0f : (fade > total ? total : fade);
}

// MIDI note number to Hz (DaisySP mtof, 12-TET, A4 = 440 Hz).
inline float midiNoteToHz (float note) noexcept
{
    return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
}

inline float hzToMidiNote (float hz) noexcept
{
    return 69.0f + 12.0f * std::log2 (hz / 440.0f);
}

} // namespace kcf
