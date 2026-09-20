// KickCrafter Fable - hardware independent kick synthesis engine.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Realtime rules for everything in this header: no allocation, no locks, no
// I/O once prepare() has been called. The engine owns a fixed voice pool.
#pragma once

#include "KickParams.h"
#include "SoftClip.h"

#include <array>
#include <cstddef>

namespace kcf
{

// Two 2048-point tables, generated exactly like the Daisy reference:
// sine = sin(2*pi*i/N); square = -1 for the first half, +1 for the second.
struct Wavetables
{
    Wavetables() noexcept;
    std::array<float, Limits::wavetableSize + 1> sine {};    // +1 guard point for interpolation
    std::array<float, Limits::wavetableSize + 1> square {};
};

// Everything one voice needs, frozen at Note On. Nothing here is read from the
// live parameters again, which implements the per-note snapshot rule.
struct VoiceSnapshot
{
    double sampleRate    = 48000.0;
    double startHz       = 250.0;
    double endHz         = 55.0;
    double sweepSec      = 0.047;
    double totalSec      = 0.179;
    double attackSec     = 0.0004;
    double fadeSec       = 0.0945;
    double fadeStartSec  = 0.0845;
    double slope         = 1.0;
    float  morph         = 0.0f;
    float  amplitude     = 1.0f;      // gain * velocity curve, applied before the bus limiter
    int    totalSamples  = 0;
    int    note          = 33;
    float  velocity      = 1.0f;

    // Builds the frozen snapshot from sanitized parameters, the host sample
    // rate, the MIDI note and the velocity in (0, 1].
    static VoiceSnapshot make (const KickParams& params, double sampleRate, int note, float velocity01) noexcept;

    // Instantaneous frequency (Hz) and amplitude envelope (0..1) at a time in seconds.
    double frequencyAt (double seconds) const noexcept;
    double envelopeAt (double seconds) const noexcept;
};

class KickVoice
{
public:
    void start (const VoiceSnapshot& snapshot) noexcept;   // steals with declick if active
    void kill() noexcept;                                    // immediate stop, declicked
    bool isActive() const noexcept { return active; }
    int remainingSamples() const noexcept { return snapshot.totalSamples - position; }
    int samplesPlayed() const noexcept { return position; }
    const VoiceSnapshot& getSnapshot() const noexcept { return snapshot; }

    // Adds this voice's output (already scaled by amplitude) into `out`.
    void renderAdd (const Wavetables& tables, float* out, int numSamples) noexcept;

    // Value the declick ramp will add at the next rendered sample (test/inspection).
    float pendingDeclickValue() const noexcept
    {
        return declickRemaining > 0 ? declickLevel * (float) declickRemaining / (float) Limits::stealDeclickSamples : 0.0f;
    }

private:
    void beginDeclick() noexcept;

    VoiceSnapshot snapshot;
    double phase = 0.0;           // normalised 0..1 instead of unbounded radians
    int position = 0;
    bool active = false;
    float lastOutput = 0.0f;
    float declickLevel = 0.0f;
    int declickRemaining = 0;
};

class KickEngine
{
public:
    KickEngine();

    void prepare (double sampleRate) noexcept;   // also resets all voices
    void reset() noexcept;                       // stops every voice, declicked
    double getSampleRate() const noexcept { return sampleRate; }

    // Starts a voice with a frozen snapshot of `params`. velocity01 must be > 0.
    // Returns the voice index used, or -1 when nothing was started.
    int noteOn (const KickParams& params, int note, float velocity01) noexcept;

    void allSoundOff() noexcept { reset(); }

    // Renders `numSamples` of mono output (overwrites), including the bus limiter.
    void render (float* out, int numSamples) noexcept;

    int activeVoiceCount() const noexcept;
    const KickVoice& getVoice (int index) const noexcept { return voices[(size_t) index]; }
    const Wavetables& getTables() const noexcept { return tables; }

private:
    Wavetables tables;
    std::array<KickVoice, Limits::maxVoices> voices;
    double sampleRate = 48000.0;
};

// Offline single-hit rendering shared by tests and the UI preview. Uses the
// same voice code as the realtime path. Returns the number of samples written
// (never more than maxSamples); the buffer is zero padded to the snapshot length.
int renderSingleHit (const KickParams& params, double sampleRate, int note, float velocity01,
                     float* out, int maxSamples) noexcept;

} // namespace kcf
