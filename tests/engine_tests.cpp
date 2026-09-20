// Engine tests: run without JUCE, also under ASan/UBSan (KCF_SANITIZE=ON).
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestHarness.h"
#include "engine/KickEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

using namespace kcf;

namespace
{

// ---------------------------------------------------------------------------
// Independent reference model: a direct port of the Daisy generate_sample /
// AudioCallback path using double precision, std::sin for the sine and a sign
// function for the square (no tables), radians phase. Landmark checks below do
// not use any production helper.
struct RefParams
{
    double start = 250.0, end = 55.0, sweep = 0.047, hold = 0.132, fadePercent = 0.5,
           attack = 0.0004, slope = 1.0, morph = 0.0, gain = 1.0;
};

double refFadeDuration (const RefParams& p)
{
    const double maxFade = p.sweep + p.hold;
    return 0.01 + p.fadePercent * (maxFade - 0.01);   // remap(percent, 0,1, 0.01, max)
}

double refLerp (double a, double b, double t) { t = std::clamp (t, 0.0, 1.0); return a + (b - a) * t; }

std::vector<double> renderReference (const RefParams& p, double sr)
{
    const double total = p.sweep + p.hold;
    const int n = (int) std::lround (total * sr);
    std::vector<double> out ((size_t) std::max (n, 0));
    const double decayStart = total - refFadeDuration (p);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double time = i / sr;
        double freq;
        if (time < p.sweep)
            freq = refLerp (p.start, p.end, 1.0 - std::pow (1.0 - std::clamp (time / p.sweep, 0.0, 1.0), p.slope));
        else
            freq = p.end;
        double amp;
        if (time < p.attack) amp = refLerp (0.0, 1.0, time / p.attack);
        else if (time < decayStart) amp = 1.0;
        else amp = refLerp (1.0, 0.0, (time - decayStart) / refFadeDuration (p));
        phase += 2.0 * M_PI * freq / sr;
        const double sine = std::sin (phase);
        double ph = std::fmod (phase, 2.0 * M_PI);
        const double square = ph < M_PI ? -1.0 : 1.0;
        double s = amp * refLerp (sine, square, p.morph);
        s *= p.gain;
        s = s * (27.0 + s * s) / (27.0 + 9.0 * s * s);
        out[(size_t) i] = s;
    }
    return out;
}

std::vector<float> renderHit (const KickParams& p, double sr, int note = 33, float vel = 1.0f, int maxSamples = 400000)
{
    std::vector<float> buf ((size_t) maxSamples, 0.0f);
    const int n = renderSingleHit (p, sr, note, vel, buf.data(), maxSamples);
    buf.resize ((size_t) std::max (n, 0));
    return buf;
}

bool allFinite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
}

float peakAbs (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    float m = 0.0f;
    to = std::min (to, v.size());
    for (size_t i = from; i < to; ++i) m = std::max (m, std::fabs (v[i]));
    return m;
}

// Frequency estimate from rising zero crossings inside [from, to) samples.
double zeroCrossingHz (const std::vector<float>& v, size_t from, size_t to, double sr)
{
    to = std::min (to, v.size());
    std::vector<double> crossings;
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
        {
            const double frac = v[i - 1] / (v[i - 1] - v[i]);   // linear interpolation
            crossings.push_back ((double) (i - 1) + frac);
        }
    if (crossings.size() < 2) return 0.0;
    return sr * (double) (crossings.size() - 1) / (crossings.back() - crossings.front());
}

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) m = std::max (m, (double) std::fabs (a[i] - b[i]));
    return m;
}

// Drives the engine with note events at sample offsets, in a given block size.
struct Event { int sample; int note; float vel; KickParams params; };
std::vector<float> renderEngine (double sr, const std::vector<Event>& events, int totalSamples, int blockSize,
                                 KickEngine* external = nullptr)
{
    KickEngine local;
    KickEngine& engine = external ? *external : local;
    if (! external) engine.prepare (sr);
    std::vector<float> out ((size_t) totalSamples, 0.0f);
    std::vector<float> block ((size_t) blockSize);
    size_t next = 0;
    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int count = std::min (blockSize, totalSamples - start);
        int cursor = 0;
        while (cursor < count)
        {
            int segmentEnd = count;
            while (next < events.size() && events[next].sample < start + cursor) ++next;   // stale
            if (next < events.size() && events[next].sample < start + count)
                segmentEnd = events[next].sample - start;
            if (segmentEnd > cursor)
            {
                engine.render (block.data(), segmentEnd - cursor);
                std::copy (block.begin(), block.begin() + (segmentEnd - cursor), out.begin() + start + cursor);
                cursor = segmentEnd;
            }
            while (next < events.size() && events[next].sample == start + cursor)
            {
                engine.noteOn (events[next].params, events[next].note, events[next].vel);
                ++next;
            }
        }
    }
    return out;
}

} // namespace

// ------------------------------------------------------------------ tests --

TEST_CASE ("default hit matches the independent Daisy reference model at 48 kHz")
{
    KickParams p;                       // defaults = reference defaults, note 33 -> 55 Hz
    const auto hit = renderHit (p, 48000.0);
    const auto ref = renderReference (RefParams {}, 48000.0);
    REQUIRE (hit.size() == ref.size());
    CHECK (hit.size() == 8592);   // 179 ms at 48 kHz, rounded to the nearest sample
    double maxErr = 0.0;
    for (size_t i = 0; i < hit.size(); ++i)
        maxErr = std::max (maxErr, std::fabs ((double) hit[i] - ref[i]));
    // Table interpolation of a 2048-point sine and float arithmetic: well below -80 dB.
    CHECK_MSG (maxErr < 1e-4, "max sample error " + std::to_string (maxErr));
}

TEST_CASE ("reference landmarks: pitch, envelope, peak, tail silence")
{
    const double sr = 48000.0;
    const auto hit = renderHit (KickParams {}, sr);
    REQUIRE (! hit.empty());
    // Hold region after the sweep (47 ms) and before the fade (fade starts at 84.5 ms).
    const double holdHz = zeroCrossingHz (hit, (size_t) (0.050 * sr), (size_t) (0.084 * sr), sr);
    CHECK_NEAR (holdHz, 55.0, 0.6);
    // SoftLimit(1) = 28/36: the reference peak in the hold region.
    CHECK_NEAR (peakAbs (hit, (size_t) (0.050 * sr), (size_t) (0.084 * sr)), 28.0 / 36.0, 0.002);
    // Attack: 0.4 ms linear ramp, first sample almost silent.
    CHECK (std::fabs (hit[0]) < 0.05f);
    // Fade: the last 5 ms are below -20 dB of the peak.
    CHECK (peakAbs (hit, hit.size() - (size_t) (0.005 * sr)) < 0.08f);
    // Sweep start: the first half cycle (positive lobe) ends near 2 ms for 250 Hz;
    // the linear sweep has already moved a little, so ~245 Hz average is expected.
    size_t firstFall = 0;
    for (size_t i = 1; i < hit.size(); ++i) if (hit[i - 1] > 0.0f && hit[i] <= 0.0f) { firstFall = i; break; }
    REQUIRE (firstFall > 0);
    const double startHz = sr / (2.0 * (double) firstFall);
    CHECK_MSG (startHz > 225.0 && startHz < 255.0, "start region " + std::to_string (startHz));
}

TEST_CASE ("engine output is silent before and after a hit and finite everywhere")
{
    const double sr = 48000.0;
    KickParams p;
    std::vector<Event> ev { { 1000, 33, 1.0f, p } };
    const auto out = renderEngine (sr, ev, 20000, 256);
    CHECK (peakAbs (out, 0, 1000) == 0.0f);
    CHECK (std::fabs (out[1000]) < 0.05f);
    CHECK (peakAbs (out, 1001, 1500) > 0.1f);
    const size_t end = 1000 + (size_t) (0.179 * sr);
    CHECK (peakAbs (out, end + 1, out.size()) == 0.0f);
    CHECK (allFinite (out));
}

TEST_CASE ("sanitize: NaN/Inf fall back to defaults, out of range clamps")
{
    KickParams p;
    p.startHz = std::numeric_limits<float>::quiet_NaN();
    p.endHz = std::numeric_limits<float>::infinity();
    p.sweepSec = -1.0f;
    p.holdSec = 99.0f;
    p.fadeFraction = 2.0f;
    p.attackSec = 0.0f;
    p.slope = 1000.0f;
    p.morph = -5.0f;
    p.gain = std::numeric_limits<float>::infinity();
    p.velocitySensitive = false;
    const KickParams s = p.sanitized();
    CHECK (s.startHz == 250.0f);
    CHECK (s.endHz == 55.0f);
    CHECK (s.sweepSec == 0.0f);
    CHECK (s.holdSec == 1.0f);
    CHECK (s.fadeFraction == 1.0f);
    CHECK (s.attackSec == KickParams {}.attackSec);
    CHECK (s.slope == 30.0f);
    CHECK (s.morph == 0.0f);
    CHECK (s.gain == 1.0f);
    CHECK (! s.velocitySensitive);                       // a switch: nothing to sanitize, kept as set
    // Rendering the malformed set directly is safe and finite.
    const auto hit = renderHit (p, 48000.0);
    CHECK (allFinite (hit));
    CHECK (! hit.empty());
}

TEST_CASE ("zero, short and maximum durations")
{
    const double sr = 48000.0;
    KickParams zero;
    zero.sweepSec = 0.0f;
    zero.holdSec = 0.0f;
    CHECK (renderHit (zero, sr).empty());
    KickEngine engine;
    engine.prepare (sr);
    CHECK (engine.noteOn (zero, 36, 1.0f) == -1);
    CHECK (engine.activeVoiceCount() == 0);

    KickParams shortHit;            // 1 ms total, shorter than the 10 ms fade floor
    shortHit.sweepSec = 0.0f;
    shortHit.holdSec = 0.001f;
    CHECK_NEAR (shortHit.fadeSeconds(), 0.001, 1e-7);
    const auto s = renderHit (shortHit, sr);
    CHECK (s.size() == 48);
    CHECK (allFinite (s));
    CHECK (peakAbs (s) <= 1.0f);

    KickParams longest;
    longest.sweepSec = 0.25f;
    longest.holdSec = 1.0f;
    longest.fadeFraction = 0.0f;   // 10 ms fade at the end
    const auto l = renderHit (longest, sr);
    CHECK (l.size() == (size_t) (1.25 * sr));
    CHECK (allFinite (l));
    CHECK (peakAbs (l, l.size() - 4) < 0.1f);
    CHECK_NEAR (longest.fadeSeconds(), 0.010, 1e-6);
    longest.fadeFraction = 1.0f;
    CHECK_NEAR (longest.fadeSeconds(), 1.25, 1e-6);
}

TEST_CASE ("all parameter endpoints render finite bounded output at 44.1/48/96 kHz")
{
    const float startEnds[] = { 20.0f, 2000.0f };
    const float endEnds[] = { 20.0f, 440.0f };
    const float sweepEnds[] = { 0.0f, 0.25f };
    const float holdEnds[] = { 0.0f, 1.0f };
    const float fadeEnds[] = { 0.0f, 1.0f };
    const float attackEnds[] = { 0.0004f, 0.05f };
    const float slopeEnds[] = { 1.0f, 30.0f };
    const float morphEnds[] = { 0.0f, 1.0f };
    const float gainEnds[] = { 0.0f, 4.0f };
    int rendered = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int mask = 0; mask < 512; ++mask)
        {
            KickParams p;
            p.startHz = startEnds[(mask >> 0) & 1];
            p.endHz = endEnds[(mask >> 1) & 1];
            p.sweepSec = sweepEnds[(mask >> 2) & 1];
            p.holdSec = holdEnds[(mask >> 3) & 1];
            p.fadeFraction = fadeEnds[(mask >> 4) & 1];
            p.attackSec = attackEnds[(mask >> 5) & 1];
            p.slope = slopeEnds[(mask >> 6) & 1];
            p.morph = morphEnds[(mask >> 7) & 1];
            p.gain = gainEnds[(mask >> 8) & 1];
            p.pitchSource = PitchSource::fixed;
            const auto hit = renderHit (p, sr);
            if (! allFinite (hit) || peakAbs (hit) > 1.0f)
            {
                CHECK_MSG (false, "mask " + std::to_string (mask) + " sr " + std::to_string (sr));
                return;
            }
            ++rendered;
        }
    CHECK (rendered == 3 * 512);
}

TEST_CASE ("sample rate independence: same pitch and duration at 44.1/48/96 kHz and extreme rates")
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 8000.0, 384000.0 })
    {
        const auto hit = renderHit (KickParams {}, sr);
        CHECK_NEAR ((double) hit.size() / sr, 0.179, 0.51 / sr + 1e-9);
        const double holdHz = zeroCrossingHz (hit, (size_t) (0.050 * sr), (size_t) (0.084 * sr), sr);
        CHECK_MSG (std::fabs (holdHz - 55.0) < 0.8, "sr " + std::to_string (sr) + " hold " + std::to_string (holdHz));
        CHECK (allFinite (hit));
    }
    // Unsupported sample rates are clamped instead of trusted.
    KickEngine e;
    e.prepare (std::numeric_limits<double>::quiet_NaN());
    CHECK (e.getSampleRate() == Limits::minSampleRate);
    e.prepare (1e9);
    CHECK (e.getSampleRate() == Limits::maxSampleRate);
}

TEST_CASE ("MIDI pitch source follows the note, fixed mode ignores it")
{
    const double sr = 48000.0;
    KickParams p;
    CHECK (p.pitchSource == PitchSource::fixed);       // v1.1 default: the End value is the note
    p.pitchSource = PitchSource::midiNote;
    p.sweepSec = 0.0f;
    p.holdSec = 0.5f;
    p.fadeFraction = 0.0f;
    const auto a1 = renderHit (p, sr, 33);   // A1 = 55 Hz
    const auto a2 = renderHit (p, sr, 45);   // A2 = 110 Hz
    CHECK_NEAR (zeroCrossingHz (a1, 4800, 24000, sr), 55.0, 0.3);
    CHECK_NEAR (zeroCrossingHz (a2, 4800, 24000, sr), 110.0, 0.6);
    const auto snapHigh = VoiceSnapshot::make (p, sr, 127, 1.0f);
    CHECK (snapHigh.endHz == (double) Limits::maxNoteHz);           // 12.5 kHz clamped to bound
    const auto snapLow = VoiceSnapshot::make (p, sr, 0, 1.0f);
    CHECK (snapLow.endHz == (double) Limits::minHz);

    p.pitchSource = PitchSource::fixed;
    p.endHz = 80.0f;
    const auto f1 = renderHit (p, sr, 33);
    const auto f2 = renderHit (p, sr, 45);
    CHECK_NEAR (zeroCrossingHz (f1, 4800, 24000, sr), 80.0, 0.4);
    CHECK (maxAbsDiff (f1, f2) == 0.0);
    CHECK_NEAR (midiNoteToHz (69.0f), 440.0, 1e-3);
    CHECK_NEAR (hzToMidiNote (55.0f), 33.0, 1e-4);
}

TEST_CASE ("velocity scales loudness once when sensitivity is on; off plays every note at full level")
{
    const double sr = 48000.0;
    KickParams p;
    REQUIRE (p.velocitySensitive);                                 // default: on
    const auto full = renderHit (p, sr, 33, 1.0f);
    const auto half = renderHit (p, sr, 33, 0.5f);
    const auto quarter = renderHit (p, sr, 33, 0.25f);
    const size_t a = (size_t) (0.050 * sr), b = (size_t) (0.084 * sr);
    CHECK_NEAR (peakAbs (full, a, b), softLimit (1.0f), 0.003);
    CHECK_NEAR (peakAbs (half, a, b), softLimit (0.5f), 0.003);   // 0.4658: exactly one velocity stage
    CHECK_NEAR (peakAbs (quarter, a, b), softLimit (0.25f), 0.003);
    p.velocitySensitive = false;
    const auto insensitive = renderHit (p, sr, 33, 0.5f);
    CHECK (maxAbsDiff (insensitive, full) == 0.0);                // off: bit-identical to a full-velocity hit
    CHECK (maxAbsDiff (renderHit (p, sr, 33, 0.01f), full) == 0.0);
    KickEngine e;
    e.prepare (sr);
    CHECK (e.noteOn (p, 33, 0.0f) == -1);      // velocity zero never triggers
    CHECK (e.activeVoiceCount() == 0);
}

TEST_CASE ("gain precedes the limiter and is frozen per hit; limiter is bounded")
{
    const double sr = 48000.0;
    KickParams p;
    p.gain = 4.0f;
    const auto hot = renderHit (p, sr);
    const size_t a = (size_t) (0.050 * sr), b = (size_t) (0.084 * sr);
    CHECK_NEAR (peakAbs (hot, a, b), 1.0, 1e-6);   // softLimit(4) = 1.0058 unbounded; softClip bounds it at 1
    CHECK (peakAbs (hot) <= 1.0f);
    p.gain = 2.0f;
    const auto warm = renderHit (p, sr);
    CHECK_NEAR (peakAbs (warm, a, b), softLimit (2.0f), 0.003);   // 2*31/63 = 0.984, below the bound
    CHECK (softLimit (64.0f) > 1.5f);        // documents why a bound is needed
    CHECK (softClip (64.0f) == 1.0f);
    CHECK (softClip (-64.0f) == -1.0f);
    CHECK_NEAR (softClip (3.0f), 1.0f, 1e-6);   // continuous at the clamp point
    CHECK_NEAR (softClip (2.999f), softLimit (2.999f), 1e-6);

}

TEST_CASE ("sixteen coherent maximum-gain square voices stay bounded at 44.1/48/96 kHz")
{
    // the stress set-up is asserted so earlier edits cannot weaken it.
    KickParams p;
    p.gain = Limits::maxGain;
    p.morph = 1.0f;
    p.startHz = Limits::maxStartHz;
    p.sweepSec = Limits::maxSweepSec;
    p.holdSec = Limits::maxHoldSec;
    p.velocitySensitive = true;
    REQUIRE (p.sanitized() == p);
    REQUIRE (p.gain == 4.0f && p.morph == 1.0f);
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<Event> ev;
        for (int i = 0; i < Limits::maxVoices; ++i) ev.push_back ({ 0, 33, 1.0f, p });
        REQUIRE (ev.size() == 16);
        const int total = (int) (1.3 * sr);
        const auto out = renderEngine (sr, ev, total, 64);
        CHECK_MSG (allFinite (out), "sr " + std::to_string (sr));
        CHECK_MSG (peakAbs (out) <= 1.0f, "sr " + std::to_string (sr) + " peak " + std::to_string (peakAbs (out)));
        CHECK (peakAbs (out, 100, (size_t) (1.2 * sr)) > 0.99f);         // sum of 64 hits the bound
        CHECK (peakAbs (out, (size_t) (1.26 * sr)) == 0.0f);             // and ends silent
    }
}

TEST_CASE ("voice pool: 16 voices, overflow steals the voice closest to its end without disturbing others")
{
    const double sr = 48000.0;
    KickEngine engine;
    engine.prepare (sr);
    KickParams p;
    p.holdSec = 0.5f;
    std::vector<float> scratch (64);
    for (int i = 0; i < 16; ++i)
    {
        CHECK (engine.noteOn (p, 30 + i, 1.0f) == i);
        engine.render (scratch.data(), 64);     // stagger the ages by 64 samples
    }
    CHECK (engine.activeVoiceCount() == 16);
    // Voice 0 is the oldest, hence closest to its end -> stolen.
    const int stolen = engine.noteOn (p, 60, 1.0f);
    CHECK (stolen == 0);
    CHECK (engine.activeVoiceCount() == 16);
    CHECK (engine.getVoice (0).getSnapshot().note == 60);
    for (int i = 1; i < 16; ++i)
    {
        CHECK (engine.getVoice (i).getSnapshot().note == 30 + i);
        CHECK (engine.getVoice (i).samplesPlayed() == 64 * (16 - i));
    }
}

TEST_CASE ("voice steal is declicked: no step larger than the ramp bound")
{
    const double sr = 48000.0;
    KickParams p;
    p.holdSec = 0.6f;
    p.fadeFraction = 0.0f;
    p.attackSec = 0.05f;
    p.sweepSec = 0.0f;
    std::vector<Event> ev;
    for (int i = 0; i < 16; ++i) ev.push_back ({ i * 10, 40, 0.06f, p });   // low level: limiter ~ linear
    ev.push_back ({ 6000, 40, 0.06f, p });                                   // steals voice 0 mid-cycle
    const auto out = renderEngine (sr, ev, 8000, 128);
    double maxStep = 0.0;
    for (size_t i = 5900; i < 6200; ++i) maxStep = std::max (maxStep, (double) std::fabs (out[i] - out[i - 1]));
    // Continuous 55 Hz sine at ~0.06*16 sum moves < 0.008 per sample; the ramp adds at most level/64.
    CHECK_MSG (maxStep < 0.02, "max step " + std::to_string (maxStep));
    CHECK (allFinite (out));
}

TEST_CASE ("overlapping voices keep independent snapshots")
{
    const double sr = 48000.0;
    KickParams a;
    a.holdSec = 0.4f;
    KickParams b = a;
    b.startHz = 900.0f;
    b.morph = 1.0f;
    b.gain = 2.0f;
    b.pitchSource = PitchSource::fixed;
    b.endHz = 200.0f;
    // Control: A alone, B alone (B triggered at 4800 samples), then both.
    const auto aAlone = renderEngine (sr, { { 0, 33, 1.0f, a } }, 30000, 256);
    const auto bAlone = renderEngine (sr, { { 4800, 33, 1.0f, b } }, 30000, 256);
    const auto both = renderEngine (sr, { { 0, 33, 1.0f, a }, { 4800, 33, 1.0f, b } }, 30000, 256);
    // Before B starts the mix equals A alone; after A ends it equals B alone.
    CHECK (maxAbsDiff (std::vector<float> (both.begin(), both.begin() + 4800), std::vector<float> (aAlone.begin(), aAlone.begin() + 4800)) == 0.0);
    const size_t aEnd = (size_t) ((a.totalSeconds()) * sr) + 1;
    CHECK (maxAbsDiff (std::vector<float> (both.begin() + (long) aEnd, both.end()), std::vector<float> (bAlone.begin() + (long) aEnd, bAlone.end())) == 0.0);
    // During the overlap the sum passes through the shared limiter: undo it and compare linear sums.
    double maxErr = 0.0;
    for (size_t i = 4800; i < aEnd; ++i)
    {
        // Invert softLimit numerically (monotonic on [-3, 3]).
        auto inv = [] (float y) { double lo = -3.0, hi = 3.0; for (int k = 0; k < 60; ++k) { const double mid = 0.5 * (lo + hi); if (softLimit ((float) mid) < y) lo = mid; else hi = mid; } return 0.5 * (lo + hi); };
        const double expected = inv (aAlone[i]) + inv (bAlone[i]);
        const double actual = inv (both[i]);
        if (std::fabs (expected) < 2.9)
            maxErr = std::max (maxErr, std::fabs (expected - actual));
    }
    CHECK_MSG (maxErr < 2e-3, "overlap linear-sum error " + std::to_string (maxErr));
}

TEST_CASE ("deterministic across block sizes and sample-accurate event offsets")
{
    const double sr = 48000.0;
    KickParams p;
    KickParams q = p;
    q.morph = 0.7f;
    q.gain = 1.5f;
    std::vector<Event> ev { { 5, 33, 1.0f, p }, { 777, 40, 0.8f, q }, { 778, 45, 0.6f, p }, { 4093, 33, 1.0f, q }, { 4094, 33, 1.0f, q } };
    const auto ref = renderEngine (sr, ev, 20000, 1);
    for (int bs : { 7, 64, 256, 512, 1024, 4096 })
    {
        const auto out = renderEngine (sr, ev, 20000, bs);
        CHECK_MSG (maxAbsDiff (out, ref) == 0.0, "block size " + std::to_string (bs));
    }
    CHECK (peakAbs (ref, 0, 5) == 0.0f);
    CHECK (std::fabs (ref[5]) < 0.05f);
}

TEST_CASE ("all sound off silences within the declick ramp and prepare resets voices")
{
    const double sr = 48000.0;
    KickEngine engine;
    engine.prepare (sr);
    KickParams p;
    p.holdSec = 1.0f;
    for (int i = 0; i < 5; ++i) engine.noteOn (p, 33 + i, 1.0f);
    std::vector<float> out (4096);
    engine.render (out.data(), 4096);
    CHECK (peakAbs (out, 2000) > 0.2f);
    engine.allSoundOff();
    CHECK (engine.activeVoiceCount() == 0);
    engine.render (out.data(), 4096);
    CHECK (allFinite (out));
    CHECK (peakAbs (out, (size_t) Limits::stealDeclickSamples) == 0.0f);
    CHECK (peakAbs (out, 0, (size_t) Limits::stealDeclickSamples) <= 1.0f);
    for (int i = 0; i < 5; ++i) engine.noteOn (p, 33 + i, 1.0f);
    engine.prepare (96000.0);
    CHECK (engine.activeVoiceCount() == 0);
    engine.render (out.data(), 4096);
    CHECK (peakAbs (out) == 0.0f);
}

TEST_CASE ("envelope stays continuous when the attack is longer than the pre-fade section")
{
    KickParams p;
    p.sweepSec = 0.0f;
    p.holdSec = 0.02f;
    p.attackSec = 0.05f;      // longer than the whole hit
    p.fadeFraction = 1.0f;    // fade over the whole hit
    const auto s = VoiceSnapshot::make (p, 48000.0, 33, 1.0f);
    CHECK (s.attackSec == s.totalSec);
    double previous = s.envelopeAt (0.0);
    double maxStep = 0.0;
    for (int i = 1; i < 960; ++i)
    {
        const double e = s.envelopeAt (i / 48000.0);
        CHECK (e >= 0.0 && e <= 1.0);
        maxStep = std::max (maxStep, std::fabs (e - previous));
        previous = e;
    }
    CHECK_MSG (maxStep < 0.01, "envelope step " + std::to_string (maxStep));
    CHECK (s.envelopeAt (-1.0) == 0.0);
    CHECK (s.envelopeAt (s.totalSec) == 0.0);
}

TEST_CASE ("aliasing assessment of the pure square (reference table preserved, measured for VALIDATION.md)")
{
    // Steady 440 Hz pure square (fixed pitch, no sweep) through the reference
    // 2048-point table with linear interpolation. Its spectrum should contain
    // only odd harmonics of 440 Hz; every other bin is folded alias energy or
    // interpolation error. The ratio (alias / harmonic energy) is reported per
    // sample rate. The bound below is deliberately loose: it documents the
    // reference behaviour rather than imposing a new anti-aliasing design.
    KickParams p;
    p.morph = 1.0f;
    p.pitchSource = PitchSource::fixed;
    p.endHz = 440.0f;
    p.startHz = 440.0f;
    p.sweepSec = 0.0f;
    p.holdSec = 1.0f;
    p.fadeFraction = 0.0f;
    p.gain = 0.5f;   // stay well inside the limiter's linear-ish region
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto hit = renderHit (p, sr);
        const int n = 8192;
        REQUIRE (hit.size() > (size_t) n + 4800);
        const size_t start = 2400;   // 50 ms in: after the attack, long before the fade
        double harmonic = 0.0, alias = 0.0;
        const double binHz = sr / n;
        for (int k = 1; k < n / 2; ++k)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / n);
                const double v = hit[start + (size_t) i] * w;
                re += v * std::cos (2.0 * M_PI * k * i / n);
                im -= v * std::sin (2.0 * M_PI * k * i / n);
            }
            const double e = re * re + im * im;
            const double f = k * binHz;
            const double ratio = f / 440.0;
            const int nearestOdd = 2 * (int) std::lround ((ratio - 1.0) / 2.0) + 1;
            const bool isHarmonicBin = std::fabs (f - nearestOdd * 440.0) <= 3.0 * binHz;
            (isHarmonicBin ? harmonic : alias) += e;
        }
        const double ratioDb = 10.0 * std::log10 (alias / harmonic);
        std::printf ("  pure-square 440 Hz at %.0f Hz: non-harmonic (alias + limiter intermod) energy %.1f dB below the harmonics\n", sr, ratioDb);
        CHECK (std::isfinite (ratioDb));
        CHECK_MSG (ratioDb < -20.0, "sr " + std::to_string (sr) + " alias ratio " + std::to_string (ratioDb));
        CHECK (allFinite (hit));
    }
    // Discontinuity check of the raw table path: consecutive-sample steps of a
    // 2 kHz pure square at 44.1 kHz never exceed the full swing (no overshoot).
    p.startHz = 2000.0f;
    p.endHz = 440.0f;
    p.sweepSec = 0.25f;
    p.holdSec = 0.0f;
    const auto sweep = renderHit (p, 44100.0);
    double maxStep = 0.0;
    for (size_t i = 1; i < sweep.size(); ++i) maxStep = std::max (maxStep, (double) std::fabs (sweep[i] - sweep[i - 1]));
    CHECK (maxStep <= 2.0 * softLimit (0.5f) + 1e-4);
    CHECK (allFinite (sweep));
}

TEST_CASE ("repeated steals and panic during a steal keep the slot continuous")
{
    const double sr = 48000.0;
    KickEngine engine;
    engine.prepare (sr);
    KickParams loud;                     // audible steady 55 Hz voice
    loud.pitchSource = PitchSource::fixed;
    loud.sweepSec = 0.0f;
    loud.holdSec = 1.0f;
    loud.fadeFraction = 0.0f;
    KickParams silent = loud;            // same timing, gain 0: contributes nothing
    silent.gain = 0.0f;
    KickParams shortSilent = silent;     // shortest remaining -> stolen first
    shortSilent.holdSec = 0.1f;

    engine.noteOn (loud, 33, 1.0f);
    for (int i = 1; i < 16; ++i) engine.noteOn (silent, 33, 1.0f);
    std::vector<float> out (1000);
    engine.render (out.data(), 1000);
    const float previous = out.back();
    CHECK (std::fabs (previous) > 0.3f);

    // First steal of slot 0 (loud voice) by a short silent hit: ramp starts.
    CHECK (engine.noteOn (shortSilent, 33, 1.0f) == 0);
    engine.render (out.data(), 1);
    const float first = out[0];
    const float expectedNext = engine.getVoice (0).pendingDeclickValue();
    // Second steal of the same slot one sample later must carry the ramp on.
    CHECK (engine.noteOn (shortSilent, 33, 1.0f) == 0);
    engine.render (out.data(), 1);
    const float second = out[0];
    // The pending ramp value is pre-limiter; the rendered output passes the bus limiter.
    CHECK_NEAR (second, softClip (expectedNext), 1e-6);
    CHECK_NEAR (first, previous, 1e-6);   // ramp begins at the interrupted level
    CHECK (std::fabs (second - first) < 0.05f);

    // Panic while the ramp is still running: also continuous.
    engine.render (out.data(), 10);
    const float beforePanic = out[9];
    const float pending = engine.getVoice (0).pendingDeclickValue();
    engine.allSoundOff();
    engine.render (out.data(), 200);
    CHECK_NEAR (out[0], softClip (pending), 1e-6);
    CHECK (std::fabs (out[0] - beforePanic) < 0.05f);
    double maxStep = 0.0;
    for (int i = 1; i < 200; ++i) maxStep = std::max (maxStep, (double) std::fabs (out[(size_t) i] - out[(size_t) i - 1]));
    CHECK (maxStep < 0.02);
    CHECK (peakAbs (out, 64, 200) == 0.0f);
    CHECK (engine.activeVoiceCount() == 0);

    // Repeated interruptions across different render partitions: the rendered
    // stream must not contain any step above the ramp bound.
    engine.prepare (sr);
    engine.noteOn (loud, 33, 1.0f);
    for (int i = 1; i < 16; ++i) engine.noteOn (silent, 33, 1.0f);
    std::vector<float> stream;
    std::vector<float> block (37);
    for (int round = 0; round < 40; ++round)
    {
        const int len = 1 + (round * 7) % 37;
        engine.render (block.data(), len);
        stream.insert (stream.end(), block.begin(), block.begin() + len);
        engine.noteOn (shortSilent, 33, 1.0f);    // steals slot 0 every few samples
    }
    maxStep = 0.0;
    for (size_t i = 1; i < stream.size(); ++i) maxStep = std::max (maxStep, (double) std::fabs (stream[i] - stream[i - 1]));
    CHECK_MSG (maxStep < 0.02, "max step across partitions " + std::to_string (maxStep));
    CHECK (allFinite (stream));
}

TEST_CASE ("exact cancellation of ramp and voice clears the old ramp on steal and on panic")
{
    const double sr = 48000.0;
    KickEngine engine;
    engine.prepare (sr);
    KickParams old;                        // steady fixed 55 Hz square, gain 1
    old.pitchSource = PitchSource::fixed;
    old.sweepSec = 0.0f; old.holdSec = 1.0f; old.fadeFraction = 0.0f; old.morph = 1.0f;
    engine.noteOn (old, 33, 1.0f);
    KickParams silent = old;
    silent.gain = 0.0f;
    for (int i = 1; i < 16; ++i) engine.noteOn (silent, 33, 1.0f);
    std::vector<float> out (1000);
    engine.render (out.data(), 1000);
    // Replacement square at gain 9/64: after its attack it outputs exactly +9/64,
    // and 55 samples into the 64-sample ramp the carried old level is exactly -9/64.
    KickParams replacement = old;
    replacement.endHz = 440.0f; replacement.holdSec = 0.1f; replacement.gain = 9.0f / 64.0f;
    CHECK (engine.noteOn (replacement, 33, 1.0f) == 0);
    engine.render (out.data(), 55);
    const float carried = engine.getVoice (0).pendingDeclickValue();
    CHECK_NEAR (carried + replacement.gain, 0.0, 1e-7);   // the set-up really cancels
    KickEngine panic = engine;
    silent.holdSec = 0.1f;
    CHECK (engine.noteOn (silent, 33, 1.0f) == 0);
    CHECK (engine.getVoice (0).pendingDeclickValue() == 0.0f);
    engine.render (out.data(), 8);
    CHECK (peakAbs (out, 0, 8) == 0.0f);                   // no stale ramp after the steal
    panic.allSoundOff();
    CHECK (panic.getVoice (0).pendingDeclickValue() == 0.0f);
    panic.render (out.data(), 8);
    CHECK (peakAbs (out, 0, 8) == 0.0f);                   // nor after the panic
    CHECK (panic.activeVoiceCount() == 0);
    // Ordinary (non-cancelling) carry still works: level != 0 restarts a full ramp.
    KickEngine plain;
    plain.prepare (sr);
    KickParams silentLong = old;          // same length as `old`, so slot 0 is the first equal-remaining candidate
    silentLong.gain = 0.0f;
    plain.noteOn (old, 33, 1.0f);
    for (int i = 1; i < 16; ++i) plain.noteOn (silentLong, 33, 1.0f);   // full pool
    plain.render (out.data(), 1000);
    CHECK (plain.noteOn (silent, 33, 1.0f) == 0);                      // steals the loud voice in slot 0
    CHECK (std::fabs (plain.getVoice (0).pendingDeclickValue()) > 0.5f);
    plain.render (out.data(), 64);
    CHECK (std::fabs (out[0]) > 0.5f && out[63] != 0.0f);
    plain.render (out.data(), 8);
    CHECK (peakAbs (out, 0, 8) == 0.0f);                   // ramp terminates exactly after 64 samples
}

TEST_CASE ("engine copies the caller's parameters: mutating them mid-hit changes nothing, the next hit sees them")
{
    const double sr = 48000.0;
    KickEngine control;
    control.prepare (sr);
    KickEngine mutated;
    mutated.prepare (sr);
    KickParams p;
    p.holdSec = 0.6f;
    KickParams live = p;
    control.noteOn (p, 33, 1.0f);
    mutated.noteOn (live, 33, 1.0f);
    std::vector<float> a (8192), b (8192);
    control.render (a.data(), 8192);
    mutated.render (b.data(), 8192);
    // Every field changes while the voice is active.
    live.startHz = 900.0f; live.endHz = 300.0f; live.sweepSec = 0.2f; live.holdSec = 0.05f; live.fadeFraction = 0.0f;
    live.attackSec = 0.05f; live.slope = 20.0f; live.morph = 1.0f; live.gain = 4.0f; live.velocitySensitive = false;
    live.pitchSource = PitchSource::fixed;
    control.render (a.data(), 8192);
    mutated.render (b.data(), 8192);
    CHECK (maxAbsDiff (a, b) == 0.0);
    // The next hit reflects the new values.
    mutated.noteOn (live, 33, 1.0f);
    const auto& snap = mutated.getVoice (1).getSnapshot();
    CHECK (snap.morph == 1.0f && snap.amplitude == 4.0f && snap.endHz == 300.0 && snap.startHz == 900.0 && snap.slope == 20.0);
}

TEST_CASE ("wavetables match the reference generation")
{
    Wavetables t;
    CHECK (t.sine[0] == 0.0f);
    CHECK_NEAR (t.sine[512], 1.0, 1e-6);
    CHECK_NEAR (t.sine[1536], -1.0, 1e-6);
    CHECK (t.square[0] == -1.0f && t.square[1023] == -1.0f && t.square[1024] == 1.0f && t.square[2047] == 1.0f);
    CHECK (t.sine[2048] == t.sine[0] && t.square[2048] == t.square[0]);
}

TEST_CASE ("negative controls: the checks really fail for wrong behaviour")
{
    // A hit with a different end frequency must not pass the 55 Hz landmark.
    KickParams p;
    p.pitchSource = PitchSource::fixed;
    p.endHz = 70.0f;
    const auto wrong = renderHit (p, 48000.0);
    const double hz = zeroCrossingHz (wrong, 2400, 4032, 48000.0);
    CHECK (std::fabs (hz - 55.0) > 5.0);
    const auto ref = renderReference (RefParams {}, 48000.0);
    double maxErr = 0.0;
    for (size_t i = 0; i < std::min (wrong.size(), ref.size()); ++i)
        maxErr = std::max (maxErr, std::fabs ((double) wrong[i] - ref[i]));
    CHECK (maxErr > 0.1);
}

int main (int argc, char** argv)
{
    // `kcf_engine_tests --dump-default-hit <file>` writes the default A1 hit at 48 kHz as raw
    // little-endian float32: the reference the REAPER render analysis compares against.
    if (argc == 3 && std::string (argv[1]) == "--dump-default-hit")
    {
        const auto hit = renderHit (KickParams {}, 48000.0);
        FILE* f = std::fopen (argv[2], "wb");
        if (f == nullptr) return 2;
        std::fwrite (hit.data(), sizeof (float), hit.size(), f);
        std::fclose (f);
        std::printf ("wrote %zu samples\n", hit.size());
        return 0;
    }
    return kcftest::runAll (argc, argv);
}
