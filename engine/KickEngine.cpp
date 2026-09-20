// SPDX-License-Identifier: AGPL-3.0-or-later
#include "KickEngine.h"

#include <algorithm>
#include <cmath>

namespace kcf
{

namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;

    inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

    inline double clamp01 (double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    inline double clampSampleRate (double sr) noexcept
    {
        if (! std::isfinite (sr) || sr < Limits::minSampleRate) return Limits::minSampleRate;
        return sr > Limits::maxSampleRate ? Limits::maxSampleRate : sr;
    }
}

Wavetables::Wavetables() noexcept
{
    constexpr int n = Limits::wavetableSize;
    for (int i = 0; i < n; ++i)
    {
        sine[(size_t) i]   = (float) std::sin (twoPi * (double) i / (double) n);
        square[(size_t) i] = i < n / 2 ? -1.0f : 1.0f;
    }
    sine[(size_t) n]   = sine[0];
    square[(size_t) n] = square[0];
}

VoiceSnapshot VoiceSnapshot::make (const KickParams& raw, double sr, int note, float velocity01) noexcept
{
    const KickParams p = raw.sanitized();
    VoiceSnapshot s;
    s.sampleRate = clampSampleRate (sr);
    s.note = std::clamp (note, 0, 127);
    s.velocity = sanitizeValue (velocity01, 1.0f, 0.0f, 1.0f);

    s.startHz = p.startHz;
    if (p.pitchSource == PitchSource::midiNote)
        s.endHz = sanitizeValue (midiNoteToHz ((float) s.note), 55.0f, Limits::minHz, Limits::maxNoteHz);
    else
        s.endHz = p.endHz;

    s.sweepSec = p.sweepSec;
    s.totalSec = p.totalSeconds();
    s.totalSamples = (int) std::lround (s.totalSec * s.sampleRate);   // nearest sample (reference truncated in float)
    s.attackSec = std::min ((double) p.attackSec, s.totalSec);
    s.fadeSec = p.fadeSeconds();
    s.fadeStartSec = s.totalSec - s.fadeSec;
    s.slope = p.slope;
    s.morph = p.morph;

    // Velocity: linear loudness scaling when velocity sensitivity is on (v1.3: a
    // switch; off = every note at full level, as the Daisy original), then the
    // per-hit gain. Both are frozen here; the bus limiter comes after.
    const float velocityGain = p.velocitySensitive ? s.velocity : 1.0f;
    s.amplitude = p.gain * velocityGain;
    return s;
}

double VoiceSnapshot::frequencyAt (double t) const noexcept
{
    if (t < sweepSec && sweepSec > 0.0)
    {
        const double x = clamp01 (t / sweepSec);
        const double shaped = 1.0 - std::pow (1.0 - x, slope);   // reference expo_decay
        return startHz + (endHz - startHz) * shaped;
    }
    return endHz;
}

double VoiceSnapshot::envelopeAt (double t) const noexcept
{
    if (t < 0.0 || t >= totalSec)
        return 0.0;
    double a = 1.0;
    if (attackSec > 0.0 && t < attackSec)
        a *= t / attackSec;
    if (fadeSec > 0.0 && t >= fadeStartSec)
        a *= clamp01 (1.0 - (t - fadeStartSec) / fadeSec);
    // Product of the two ramps keeps the envelope continuous even when the
    // attack is longer than the pre-fade section (reference jumped instead).
    return a;
}

void KickVoice::beginDeclick() noexcept
{
    // Continue the level the slot would have produced next: the voice's last
    // sample plus whatever is left of an earlier, still running ramp. Restarting
    // the ramp from that sum keeps repeated steals/kills continuous.
    const float carried = declickRemaining > 0
                              ? declickLevel * (float) declickRemaining / (float) Limits::stealDeclickSamples
                              : 0.0f;
    const float level = (active ? lastOutput : 0.0f) + carried;
    // Always replace the ramp: an exact cancellation (level == 0) must clear the
    // old ramp rather than leave it running.
    declickLevel = level;
    declickRemaining = level != 0.0f ? Limits::stealDeclickSamples : 0;
}

void KickVoice::start (const VoiceSnapshot& s) noexcept
{
    beginDeclick();
    snapshot = s;
    phase = 0.0;
    position = 0;
    lastOutput = 0.0f;
    active = s.totalSamples > 0;
}

void KickVoice::kill() noexcept
{
    beginDeclick();
    active = false;
    position = 0;
    lastOutput = 0.0f;
}

void KickVoice::renderAdd (const Wavetables& tables, float* out, int numSamples) noexcept
{
    if (declickRemaining > 0)
    {
        const float step = declickLevel / (float) Limits::stealDeclickSamples;
        for (int i = 0; i < numSamples && declickRemaining > 0; ++i, --declickRemaining)
            out[i] += step * (float) declickRemaining;
        if (declickRemaining <= 0)
            declickLevel = 0.0f;
    }

    if (! active)
        return;

    const VoiceSnapshot& s = snapshot;
    const double invSr = 1.0 / s.sampleRate;
    const int count = std::min (numSamples, s.totalSamples - position);
    constexpr int n = Limits::wavetableSize;

    for (int i = 0; i < count; ++i)
    {
        const double t = (double) position * invSr;
        const double freq = s.frequencyAt (t);
        const double amp = s.envelopeAt (t);

        phase += freq * invSr;             // reference advances before sampling
        phase -= std::floor (phase);       // keep in [0, 1)

        const double pos = phase * n;
        const int index = (int) pos;       // 0 .. n-1 (guard point covers n)
        const float frac = (float) (pos - (double) index);
        const float sine = lerp (tables.sine[(size_t) index], tables.sine[(size_t) index + 1], frac);
        const float square = lerp (tables.square[(size_t) index], tables.square[(size_t) index + 1], frac);
        const float sample = (float) amp * lerp (sine, square, s.morph) * s.amplitude;

        out[i] += sample;
        lastOutput = sample;
        ++position;
    }

    if (position >= s.totalSamples)
    {
        active = false;
        lastOutput = 0.0f;
    }
}

KickEngine::KickEngine() = default;

void KickEngine::prepare (double sr) noexcept
{
    sampleRate = clampSampleRate (sr);
    for (auto& v : voices)
        v = KickVoice();
}

void KickEngine::reset() noexcept
{
    for (auto& v : voices)
        v.kill();
}

int KickEngine::noteOn (const KickParams& params, int note, float velocity01) noexcept
{
    if (! (velocity01 > 0.0f))
        return -1;

    const VoiceSnapshot snapshot = VoiceSnapshot::make (params, sampleRate, note, velocity01);
    if (snapshot.totalSamples <= 0)
        return -1;   // zero-length hit: nothing to play, nothing to allocate

    int chosen = -1;
    for (int i = 0; i < Limits::maxVoices; ++i)
    {
        if (! voices[(size_t) i].isActive())
        {
            chosen = i;
            break;
        }
    }

    if (chosen < 0)
    {
        // Pool saturated: steal the voice closest to its natural end.
        int best = 0;
        int bestRemaining = voices[0].remainingSamples();
        for (int i = 1; i < Limits::maxVoices; ++i)
        {
            const int r = voices[(size_t) i].remainingSamples();
            if (r < bestRemaining)
            {
                best = i;
                bestRemaining = r;
            }
        }
        chosen = best;
    }

    voices[(size_t) chosen].start (snapshot);
    return chosen;
}

void KickEngine::render (float* out, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    std::fill (out, out + numSamples, 0.0f);
    for (auto& v : voices)
        v.renderAdd (tables, out, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        float x = out[i];
        if (! std::isfinite (x))
            x = 0.0f;
        out[i] = softClip (x);
    }
}

int KickEngine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.isActive() ? 1 : 0;
    return n;
}

int renderSingleHit (const KickParams& params, double sampleRate, int note, float velocity01,
                     float* out, int maxSamples) noexcept
{
    if (out == nullptr || maxSamples <= 0)
        return 0;

    const VoiceSnapshot snapshot = VoiceSnapshot::make (params, sampleRate, note, velocity01);
    const int count = std::min (maxSamples, snapshot.totalSamples);
    std::fill (out, out + maxSamples, 0.0f);

    if (count <= 0 || ! (velocity01 > 0.0f))
        return 0;

    Wavetables tables;   // stack copy: preview/test path only, never the audio thread
    KickVoice voice;
    voice.start (snapshot);
    voice.renderAdd (tables, out, count);
    for (int i = 0; i < count; ++i)
        out[i] = softClip (std::isfinite (out[i]) ? out[i] : 0.0f);
    return count;
}

} // namespace kcf
