// Plugin-level tests: parameters, MIDI timing, snapshot semantics through host
// automation, state, presets/A-B, instances, sample rates, editor open/closed.
// Runs the real KickCrafterProcessor (same object the VST3 wrapper drives).
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestHarness.h"
#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <random>
#include <thread>
#include <chrono>

using namespace kcf;

namespace
{

struct MidiEv { int sample; juce::MidiMessage message; };

struct Host
{
    explicit Host (double sampleRate = 48000.0, int blockSize = 256) : sr (sampleRate), block (blockSize)
    {
        processor = std::make_unique<KickCrafterProcessor>();
        processor->setPlayConfigDetails (0, 2, sr, block);
        processor->prepareToPlay (sr, block);
    }

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        block = blockSize;
        processor->setPlayConfigDetails (0, 2, sr, block);
        processor->prepareToPlay (sr, block);
    }

    // Renders the left channel; `beforeBlock (startSample)` runs before each block (host automation).
    std::vector<float> render (int totalSamples, const std::vector<MidiEv>& events,
                               const std::function<void (int)>& beforeBlock = {}, std::vector<float>* right = nullptr)
    {
        std::vector<float> out ((size_t) totalSamples, 0.0f);
        if (right) right->assign ((size_t) totalSamples, 0.0f);
        juce::AudioBuffer<float> buffer (2, block);
        for (int start = 0; start < totalSamples; start += block)
        {
            const int count = std::min (block, totalSamples - start);
            if (beforeBlock) beforeBlock (start);
            juce::MidiBuffer midi;
            for (const auto& e : events)
                if (e.sample >= start && e.sample < start + count)
                    midi.addEvent (e.message, e.sample - start);
            juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, count);
            slice.clear();
            processor->processBlock (slice, midi);
            std::copy (slice.getReadPointer (0), slice.getReadPointer (0) + count, out.begin() + start);
            if (right) std::copy (slice.getReadPointer (1), slice.getReadPointer (1) + count, right->begin() + start);
        }
        return out;
    }

    juce::RangedAudioParameter& param (const char* id)
    {
        auto* p = processor->getState().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    void setDisplay (const char* id, float value)     // host-style: normalised via setValueNotifyingHost
    {
        auto& p = param (id);
        p.setValueNotifyingHost (p.convertTo0to1 (value));
    }

    float getDisplay (const char* id) { return processor->getState().getRawParameterValue (id)->load(); }

    std::unique_ptr<KickCrafterProcessor> processor;
    double sr;
    int block;
};

MidiEv noteOn (int sample, int note, int velocity, int channel = 1) { return { sample, juce::MidiMessage::noteOn (channel, note, (juce::uint8) velocity) }; }
MidiEv noteOff (int sample, int note, int channel = 1) { return { sample, juce::MidiMessage::noteOff (channel, note) }; }
MidiEv cc (int sample, int controller, int value, int channel = 1) { return { sample, juce::MidiMessage::controllerEvent (channel, controller, value) }; }

float peakAbs (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    float m = 0.0f;
    to = std::min (to, v.size());
    for (size_t i = from; i < to; ++i) m = std::max (m, std::fabs (v[i]));
    return m;
}

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0, size_t to = SIZE_MAX)
{
    double m = 0.0;
    to = std::min ({ to, a.size(), b.size() });
    for (size_t i = from; i < to; ++i) m = std::max (m, (double) std::fabs (a[i] - b[i]));
    return m;
}

bool allFinite (const std::vector<float>& v) { return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); }); }

double zeroCrossingHz (const std::vector<float>& v, size_t from, size_t to, double sr)
{
    to = std::min (to, v.size());
    std::vector<double> crossings;
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
            crossings.push_back ((double) (i - 1) + v[i - 1] / (v[i - 1] - v[i]));
    if (crossings.size() < 2) return 0.0;
    return sr * (double) (crossings.size() - 1) / (crossings.back() - crossings.front());
}

std::vector<float> engineHit (const KickParams& p, double sr, int note, float vel, int n)
{
    std::vector<float> buf ((size_t) n, 0.0f);
    renderSingleHit (p, sr, note, vel, buf.data(), n);
    return buf;
}

struct GestureSpy final : juce::AudioProcessorListener
{
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float value) override { changes.push_back ({ index, value }); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { begins.push_back (index); }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { ends.push_back (index); }
    std::vector<std::pair<int, float>> changes;
    std::vector<int> begins, ends;
};

} // namespace

// ------------------------------------------------------------- catalogue --

TEST_CASE ("published parameters: IDs, names, defaults, ranges, units and choices")
{
    Host host;
    auto& params = host.processor->getParameters();
    CHECK (params.size() == 12);
    struct Expect { const char* id; const char* name; float def; float lo; float hi; const char* label; const char* defText; };
    const Expect expected[] = {
        { "startFreq", "Start Frequency", 250.0f, 20.0f, 2000.0f, "Hz", "250.0 Hz" },
        { "endFreq", "End Frequency", 55.0f, 20.0f, 440.0f, "Hz", "55.0 Hz" },
        { "sweep", "Sweep Time", 47.0f, 0.0f, 250.0f, "ms", "47.0 ms" },
        { "hold", "Hold Time", 132.0f, 0.0f, 1000.0f, "ms", "132 ms" },
        { "fade", "Fade Out", 50.0f, 0.0f, 100.0f, "%", "50 %" },
        { "attack", "Attack", 0.4f, 0.4f, 50.0f, "ms", "0.40 ms" },
        { "curve", "Sweep Curve", 1.0f, 1.0f, 30.0f, "", "1.00" },
        { "shape", "Shape", 0.0f, 0.0f, 100.0f, "%", "0 %" },
        { "drive", "Drive", 1.0f, 0.0f, 4.0f, "x", "1.00 x" },
    };
    for (const auto& e : expected)
    {
        auto& p = host.param (e.id);
        CHECK_MSG (p.getName (64) == e.name, e.id);
        CHECK_MSG (p.getParameterID() == e.id, e.id);
        CHECK_NEAR (p.convertFrom0to1 (p.getDefaultValue()), e.def, 1e-4);
        CHECK_NEAR (p.getNormalisableRange().start, e.lo, 1e-6);
        CHECK_NEAR (p.getNormalisableRange().end, e.hi, 1e-6);
        CHECK_MSG (p.getLabel().isEmpty(), e.id);          // v1.1: no separate VST3 label (hosts appended it twice)
        CHECK_MSG (juce::String (e.label).isEmpty() || p.getText (p.getDefaultValue(), 32).endsWith (e.label), std::string (e.id) + " text carries its unit");
        CHECK_MSG (p.getText (p.getDefaultValue(), 32) == e.defText, std::string (e.id) + " -> " + p.getText (p.getDefaultValue(), 32).toStdString());
        CHECK (p.isAutomatable());
        CHECK (! p.isDiscrete());
    }
    auto& vel = host.param ("velocity");                    // v1.3: a switch; v1.5: default off
    CHECK (vel.getName (64) == "Velocity Sensitivity");
    CHECK (vel.isDiscrete() && vel.isBoolean() && vel.isAutomatable());
    CHECK (vel.getDefaultValue() == 0.0f);
    CHECK (vel.getText (1.0f, 32) == "On" && vel.getText (0.0f, 32) == "Off");
    CHECK (vel.getNormalisableRange().start == 0.0f && vel.getNormalisableRange().end == 1.0f);
    CHECK (vel.getLabel().isEmpty());
    auto& pitch = host.param ("pitchSource");
    CHECK (pitch.getName (64) == "Pitch Source");
    CHECK (pitch.isDiscrete());
    CHECK (pitch.getAllValueStrings().size() == 2);
    CHECK (pitch.getText (0.0f, 32) == "Fixed");          // v1.1: Fixed first and default
    CHECK (pitch.getText (1.0f, 32) == "MIDI Note");
    CHECK (pitch.getDefaultValue() == 0.0f);
    auto& channel = host.param ("midiChannel");
    CHECK (channel.getName (64) == "MIDI Channel");
    CHECK (channel.getAllValueStrings().size() == 17);
    CHECK (channel.getText (0.0f, 32) == "Omni");
    CHECK (channel.getText (1.0f, 32) == "16");
    // Logarithmic frequency mapping: the normalised midpoint is the geometric mean.
    auto& start = host.param ("startFreq");
    CHECK_NEAR (start.convertFrom0to1 (0.5f), std::sqrt (20.0 * 2000.0), 0.05);
    auto& curve = host.param ("curve");
    CHECK_NEAR (curve.convertFrom0to1 (0.5f), std::sqrt (30.0), 0.01);
    // Text entry accepts Hz and note names.
    CHECK_NEAR (start.convertFrom0to1 (start.getValueForText ("A1")), 55.0, 0.01);
    CHECK_NEAR (start.convertFrom0to1 (start.getValueForText ("440 Hz")), 440.0, 0.01);
    CHECK_NEAR (start.convertFrom0to1 (start.getValueForText ("C#2")), 69.2957, 0.01);
    CHECK_NEAR (start.convertFrom0to1 (start.getValueForText ("Bb1 +10c")), 58.6083, 0.01);
    CHECK_NEAR (start.convertFrom0to1 (start.getValueForText ("A1 -50c")), 53.4340, 0.01);
    CHECK_NEAR (start.convertFrom0to1 (start.getValueForText ("c-1")), 20.0, 0.01);             // 8.18 Hz clamps to the 20 Hz floor
    CHECK_NEAR (params::parseFrequencyText ("c-1", 0.0f), 8.1758, 0.001);
    CHECK (std::isnan (params::parseFrequencyText ("A1 x", std::numeric_limits<float>::quiet_NaN())));
    CHECK (std::isnan (params::parseFrequencyText ("A10", std::numeric_limits<float>::quiet_NaN())));
    CHECK (std::isnan (params::parseFrequencyText ("A1 +200c", std::numeric_limits<float>::quiet_NaN())));
    CHECK (std::isnan (params::parseFrequencyText ("banana", std::numeric_limits<float>::quiet_NaN())));
    CHECK (std::isnan (params::parseFrequencyText ("", std::numeric_limits<float>::quiet_NaN())));
    CHECK (std::isnan (params::parseFrequencyText ("1.2.3", std::numeric_limits<float>::quiet_NaN())));
    CHECK_NEAR (params::parseFrequencyText ("440hz", 0.0f), 440.0, 1e-4);
    CHECK_NEAR (params::parseNumberText ("12.5 ms", 0.0f), 12.5, 1e-5);
    CHECK_NEAR (params::parseNumberText ("-3", 0.0f), -3.0, 1e-5);
    CHECK (std::isnan (params::parseNumberText ("ten", std::numeric_limits<float>::quiet_NaN())));
    CHECK_NEAR (host.param ("sweep").convertFrom0to1 (host.param ("sweep").getValueForText ("12.5 ms")), 12.5, 1e-4);
    CHECK (host.processor->getNumPrograms() == 1);            // no host Program parameter (deliberate)
    CHECK (host.processor->getProgramName (0) == "KickCrafter");
    CHECK (host.processor->acceptsMidi());
    CHECK (! host.processor->producesMidi());
    CHECK (host.processor->getTailLengthSeconds() >= 1.25);
}

TEST_CASE ("automation value roundtrips: normalised in == normalised out, text roundtrips")
{
    Host host;
    for (auto* id : params::allIds)
    {
        auto& p = host.param (id);
        for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            p.setValueNotifyingHost (v);
            const float back = p.getValue();
            const float tolerance = p.isDiscrete() ? 0.5f / (float) (p.getNumSteps() - 1) : 1e-5f;
            CHECK_MSG (std::fabs (back - v) <= tolerance, std::string (id) + " " + std::to_string (v) + " -> " + std::to_string (back));
            if (! p.isDiscrete())
            {
                const float fromText = p.getValueForText (p.getText (back, 32));
                const float denormA = p.convertFrom0to1 (back), denormB = p.convertFrom0to1 (fromText);
                CHECK_MSG (std::fabs (denormA - denormB) <= std::max (0.02f * std::fabs (denormA), 0.06f),
                           std::string (id) + " text roundtrip " + std::to_string (denormA) + " vs " + std::to_string (denormB));
            }
        }
        p.setValueNotifyingHost (p.getDefaultValue());
    }
}

// ------------------------------------------------------------------ MIDI --

TEST_CASE ("default hit through processBlock equals the engine and is dual mono")
{
    Host host;
    std::vector<float> right;
    const auto left = host.render (12000, { noteOn (0, 33, 127) }, {}, &right);
    const auto reference = engineHit (KickParams {}, 48000.0, 33, 1.0f, 12000);
    CHECK (maxAbsDiff (left, reference) == 0.0);
    CHECK (maxAbsDiff (left, right) == 0.0);
    CHECK (peakAbs (left, 8593) == 0.0f);
    CHECK (host.processor->getNoteCounter() == 1);
    CHECK (host.processor->getLastNote() == 33);
    CHECK (host.processor->getLastVelocity() == 127);
}

TEST_CASE ("MIDI timing: sample offsets, several notes per block, velocity zero, Note Off ignored, channels")
{
    Host host (48000.0, 512);
    // Note at offset 100 of block 0, another at 300 of the same block, one in block 3 at offset 7.
    const auto out = host.render (20000, { noteOn (100, 33, 127), noteOn (300, 45, 100), noteOn (3 * 512 + 7, 40, 64) });
    CHECK (peakAbs (out, 0, 100) == 0.0f);
    CHECK (std::fabs (out[100]) < 0.05f && peakAbs (out, 101, 300) > 0.1f);
    // Reproduce with the engine event by event.
    KickEngine engine;
    engine.prepare (48000.0);
    std::vector<float> expect (20000, 0.0f);
    auto renderTo = [&] (int from, int to) { engine.render (expect.data() + from, to - from); };
    renderTo (0, 100); engine.noteOn (KickParams {}, 33, 1.0f);
    renderTo (100, 300); engine.noteOn (KickParams {}, 45, 100.0f / 127.0f);
    renderTo (300, 3 * 512 + 7); engine.noteOn (KickParams {}, 40, 64.0f / 127.0f);
    renderTo (3 * 512 + 7, 20000);
    CHECK (maxAbsDiff (out, expect) == 0.0);

    // Velocity zero must not trigger.
    Host quiet;
    const auto silent = quiet.render (4096, { noteOn (10, 33, 0) });
    CHECK (peakAbs (silent) == 0.0f);
    CHECK (quiet.processor->getNoteCounter() == 0);

    // Velocity sensitivity (switched On: the default is Off since v1.5): 64 is quieter than 127;
    // Note Off changes nothing.
    Host a, b, c;
    for (Host* h : { &a, &b, &c }) h->setDisplay (params::velocity, 1.0f);
    const auto loud = a.render (8192, { noteOn (0, 33, 127) });
    const auto soft = b.render (8192, { noteOn (0, 33, 64) });
    const auto withOff = c.render (8192, { noteOn (0, 33, 127), noteOff (1000, 33) });
    CHECK (peakAbs (soft, 2400, 4000) < peakAbs (loud, 2400, 4000) * 0.75f);
    CHECK (maxAbsDiff (loud, withOff) == 0.0);

    // Channel filter: Omni accepts channel 7; "5" rejects channel 1 and accepts channel 5.
    Host omni;
    CHECK (peakAbs (omni.render (2048, { noteOn (0, 33, 127, 7) })) > 0.1f);
    Host filtered;
    filtered.setDisplay (params::midiChannel, 5.0f);
    CHECK (peakAbs (filtered.render (2048, { noteOn (0, 33, 127, 1) })) == 0.0f);
    CHECK (peakAbs (filtered.render (2048, { noteOn (0, 33, 127, 5) })) > 0.1f);
}

TEST_CASE ("All Sound Off silences within the declick ramp; All Notes Off lets one-shots finish")
{
    Host host (48000.0, 64);
    host.setDisplay (params::hold, 1000.0f);
    const auto killed = host.render (12000, { noteOn (0, 33, 127), cc (6000, 120, 0) });
    CHECK (peakAbs (killed, 5000, 6000) > 0.2f);
    CHECK (peakAbs (killed, 6000 + Limits::stealDeclickSamples) == 0.0f);
    CHECK (peakAbs (killed, 6000, 6000 + Limits::stealDeclickSamples) <= 1.0f);
    Host host2 (48000.0, 64);
    host2.setDisplay (params::hold, 1000.0f);
    const auto notesOff = host2.render (12000, { noteOn (0, 33, 127), cc (6000, 123, 0) });
    CHECK (peakAbs (notesOff, 6100, 12000) > 0.2f);
    CHECK (allFinite (killed) && allFinite (notesOff));
}

TEST_CASE ("deterministic across block sizes through the plugin, including zero-length blocks")
{
    const std::vector<MidiEv> ev { noteOn (5, 33, 127), noteOn (777, 40, 90), noteOn (778, 45, 70), noteOn (4093, 33, 127), cc (9000, 120, 0), noteOn (9100, 36, 127) };
    Host reference (48000.0, 1);
    const auto ref = reference.render (16000, ev);
    for (int bs : { 7, 64, 256, 1024 })
    {
        Host h (48000.0, bs);
        CHECK_MSG (maxAbsDiff (h.render (16000, ev), ref) == 0.0, "block " + std::to_string (bs));
    }
    // Zero-sample block (host flush) and a block larger than the prepared size are
    // handled safely: rendering goes straight into the host buffer, so an oversized
    // block is rendered fully (no allocation, no truncation) and equals the engine.
    Host h (48000.0, 256);
    juce::AudioBuffer<float> empty (2, 0);
    juce::MidiBuffer midi;
    h.processor->processBlock (empty, midi);
    juce::AudioBuffer<float> big (2, 4096);
    big.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 127), 100);
    h.processor->processBlock (big, midi);
    std::vector<float> rendered (big.getReadPointer (0), big.getReadPointer (0) + 4096);
    std::vector<float> expect (4096, 0.0f);
    const auto hit = engineHit (KickParams {}, 48000.0, 33, 1.0f, 3996);
    std::copy (hit.begin(), hit.end(), expect.begin() + 100);
    CHECK (maxAbsDiff (rendered, expect) == 0.0);
    // A host with no output channels still advances the engine (scratch path):
    // the next audible block must be the exact continuation of the hit after
    // 3996 rendered + 300 channel-less samples.
    juce::AudioBuffer<float> none (0, 300);
    juce::MidiBuffer silentMidi;
    h.processor->processBlock (none, silentMidi);
    juce::AudioBuffer<float> after (2, 256);
    after.clear();
    h.processor->processBlock (after, silentMidi);
    std::vector<float> afterVec (after.getReadPointer (0), after.getReadPointer (0) + 256);
    const auto longHit = engineHit (KickParams {}, 48000.0, 33, 1.0f, 8592);   // full default hit
    const size_t continuation = 3996 + 300;
    REQUIRE (longHit.size() >= continuation + 256);
    std::vector<float> expectAfter (longHit.begin() + (long) continuation, longHit.begin() + (long) continuation + 256);
    CHECK (maxAbsDiff (afterVec, expectAfter) == 0.0);
    CHECK (peakAbs (afterVec) > 0.0f);
}

// -------------------------------------------------------------- snapshot --

TEST_CASE ("strict snapshot regression: each parameter changed by the host mid-voice leaves the voice untouched, next note changes")
{
    // Base voice: Fixed pitch (so End Frequency matters), 250 ms sweep, 50 ms attack,
    // 1 s hold, 30 % fade. The host change lands at block 4 (sample 1024 = 21 ms):
    // inside the attack, inside the sweep, long before the fade. A second note is
    // triggered at sample 30208 (block aligned) while the first still sounds.
    struct Change { const char* id; float value; };
    const Change changes[] = {
        { params::startFreq, 1200.0f }, { params::endFreq, 300.0f }, { params::sweep, 20.0f }, { params::hold, 300.0f },
        { params::fade, 100.0f }, { params::attack, 0.4f }, { params::curve, 20.0f }, { params::shape, 100.0f },
        { params::drive, 4.0f }, { params::velocity, 1.0f }, { params::pitchSource, 1.0f },
    };
    const int block = 256, total = 60000, changeAt = 1024, secondNote = 30208;
    REQUIRE (changeAt % block == 0 && secondNote % block == 0);
    auto configure = [] (Host& h)
    {
        h.setDisplay (params::pitchSource, 0.0f);     // Fixed
        h.setDisplay (params::endFreq, 55.0f);
        h.setDisplay (params::sweep, 250.0f);
        h.setDisplay (params::attack, 50.0f);
        h.setDisplay (params::hold, 1000.0f);
        h.setDisplay (params::fade, 30.0f);
    };
    const std::vector<MidiEv> events { noteOn (0, 45, 100), noteOn (secondNote, 45, 100) };
    Host control (48000.0, block);
    configure (control);
    const auto ref = control.render (total, events);
    for (const auto& change : changes)
    {
        Host mutated (48000.0, block);
        configure (mutated);
        int executed = 0;
        const auto out = mutated.render (total, events, [&] (int start)
        {
            if (start == changeAt) { mutated.setDisplay (change.id, change.value); ++executed; }
        });
        CHECK_MSG (executed == 1, std::string ("mutation must execute exactly once: ") + change.id);
        CHECK_MSG (std::fabs (mutated.getDisplay (change.id) - change.value) < 1e-3, std::string ("host value not applied: ") + change.id);
        CHECK_MSG (maxAbsDiff (out, ref, 0, (size_t) secondNote) == 0.0, std::string ("tail leaked for ") + change.id);
        CHECK_MSG (maxAbsDiff (out, ref, (size_t) secondNote, (size_t) total) > 1e-3, std::string ("next note ignored ") + change.id);
        CHECK (allFinite (out));

        // Sensitivity control: the same change applied BEFORE the note must alter the
        // compared window, otherwise the tail-invariance check would be vacuous.
        Host early (48000.0, block);
        configure (early);
        early.setDisplay (change.id, change.value);
        const auto leaky = early.render (total, events);
        CHECK_MSG (maxAbsDiff (leaky, ref, (size_t) changeAt, (size_t) secondNote) > 1e-3,
                   std::string ("window insensitive to ") + change.id);
    }

    // State restore during an active voice is another route into the parameter model.
    Host source (48000.0, block);
    configure (source);
    source.setDisplay (params::shape, 100.0f);
    source.setDisplay (params::drive, 3.0f);
    juce::MemoryBlock stateBlock;
    source.processor->getStateInformation (stateBlock);
    Host restored (48000.0, block);
    configure (restored);
    int executed = 0;
    const auto out = restored.render (total, events, [&] (int start)
    {
        if (start == changeAt) { restored.processor->setStateInformation (stateBlock.getData(), (int) stateBlock.getSize()); ++executed; }
    });
    CHECK (executed == 1);
    CHECK (maxAbsDiff (out, ref, 0, (size_t) secondNote) == 0.0);
    CHECK (maxAbsDiff (out, ref, (size_t) secondNote, (size_t) total) > 1e-3);
    CHECK_NEAR (restored.getDisplay (params::drive), 3.0f, 1e-4);
}

TEST_CASE ("snapshot rule through presets and A/B: switching mid-voice does not touch the voice")
{
    Host control (48000.0, 256), preset (48000.0, 256), ab (48000.0, 256);
    for (Host* h : { &control, &preset, &ab }) h->setDisplay (params::hold, 1000.0f);
    const std::vector<MidiEv> events { noteOn (0, 33, 127), noteOn (30000, 33, 127) };
    const auto ref = control.render (60000, events);
    const auto viaPreset = preset.render (60000, events, [&] (int start) { if (start == 20480) preset.processor->loadFactoryPreset (7); });
    CHECK (maxAbsDiff (viaPreset, ref, 0, 30000) == 0.0);
    CHECK (maxAbsDiff (viaPreset, ref, 30000) > 1e-3);
    CHECK (preset.processor->getPresetIndex() == 7);
    CHECK (preset.processor->currentValuesMatchPreset (7));
    // A/B: slot B holds the Gabber preset, slot A the long default.
    ab.processor->copyCurrentToOtherSlot();
    ab.processor->toggleAB();
    ab.processor->loadFactoryPreset (7);
    ab.processor->toggleAB();             // back to A (long default), B = Gabber
    CHECK (ab.processor->getABSlot() == 0);
    CHECK_NEAR (ab.getDisplay (params::hold), 1000.0f, 1e-4);
    const auto viaAB = ab.render (60000, events, [&] (int start) { if (start == 20480) ab.processor->toggleAB(); });
    CHECK (maxAbsDiff (viaAB, ref, 0, 30000) == 0.0);
    CHECK (maxAbsDiff (viaAB, ref, 30000) > 1e-3);
    CHECK (ab.processor->getABSlot() == 1);
    CHECK_NEAR (ab.getDisplay (params::drive), 4.0f, 1e-4);
}

TEST_CASE ("automation before a trigger is honoured at the trigger's block")
{
    Host host (48000.0, 256);
    const auto out = host.render (12000, { noteOn (512, 33, 127) }, [&] (int start)
    {
        if (start == 256) host.setDisplay (params::shape, 100.0f);   // one block before the note
    });
    KickParams square;
    square.morph = 1.0f;
    std::vector<float> expect (12000, 0.0f);
    const auto hit = engineHit (square, 48000.0, 33, 1.0f, 12000 - 512);
    std::copy (hit.begin(), hit.end(), expect.begin() + 512);
    CHECK (maxAbsDiff (out, expect) == 0.0);
}

TEST_CASE ("overlapping voices with different snapshots reproduce the engine sequence")
{
    Host host (48000.0, 128);
    host.setDisplay (params::hold, 400.0f);
    const auto out = host.render (40000, { noteOn (0, 33, 127), noteOn (4864, 45, 80) }, [&] (int start)
    {
        if (start == 2560) { host.setDisplay (params::shape, 70.0f); host.setDisplay (params::drive, 2.0f); host.setDisplay (params::startFreq, 900.0f); }
    });
    // Parameter-resolved reference inputs: the log-range normalisation makes 900 Hz
    // come back as 900.00006 Hz, so the engine must see the plugin's resolved values.
    KickEngine engine;
    engine.prepare (48000.0);
    Host resolver (48000.0, 128);
    resolver.setDisplay (params::hold, 400.0f);
    const KickParams a = resolver.processor->getCurrentParams();
    resolver.setDisplay (params::shape, 70.0f); resolver.setDisplay (params::drive, 2.0f); resolver.setDisplay (params::startFreq, 900.0f);
    const KickParams b = resolver.processor->getCurrentParams();
    CHECK_NEAR (b.startHz, 900.0f, 0.01f);
    CHECK (b.morph == 0.7f && b.gain == 2.0f);
    std::vector<float> expect (40000, 0.0f);
    engine.noteOn (a, 33, 1.0f);
    engine.render (expect.data(), 4864);
    engine.noteOn (b, 45, 80.0f / 127.0f);
    engine.render (expect.data() + 4864, 40000 - 4864);
    CHECK (maxAbsDiff (out, expect) == 0.0);
}

// ---------------------------------------------------------------- state --

TEST_CASE ("state save/restore covers every parameter, A/B slot, preset and UI scale; versioned")
{
    Host a;
    a.setDisplay (params::startFreq, 333.0f);
    a.setDisplay (params::endFreq, 77.0f);
    a.setDisplay (params::sweep, 123.0f);
    a.setDisplay (params::hold, 456.0f);
    a.setDisplay (params::fade, 33.0f);
    a.setDisplay (params::attack, 7.5f);
    a.setDisplay (params::curve, 4.2f);
    a.setDisplay (params::shape, 61.0f);
    a.setDisplay (params::drive, 2.75f);
    a.setDisplay (params::velocity, 0.0f);
    a.setDisplay (params::pitchSource, 1.0f);
    a.setDisplay (params::midiChannel, 10.0f);
    a.processor->copyCurrentToOtherSlot();
    a.processor->toggleAB();                                   // now in slot B with the same values
    a.setDisplay (params::drive, 0.5f);                        // B differs in drive
    a.processor->setUIScalePercent (125);
    juce::MemoryBlock block;
    a.processor->getStateInformation (block);
    CHECK (block.getSize() > 0);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    REQUIRE (xml != nullptr);
    CHECK (xml->hasTagName ("KickCrafter"));
    CHECK (xml->getIntAttribute ("version") == 4);

    Host b;
    b.processor->setStateInformation (block.getData(), (int) block.getSize());
    for (auto* id : params::allIds)
        CHECK_MSG (std::fabs (a.getDisplay (id) - b.getDisplay (id)) < 1e-4, id);
    CHECK (b.processor->getABSlot() == 1);
    CHECK (b.processor->getUIScalePercent() == 125);
    CHECK (b.processor->getOtherSlotValues() == a.processor->getOtherSlotValues());
    b.processor->toggleAB();
    CHECK_NEAR (b.getDisplay (params::drive), 2.75f, 1e-4);
    CHECK (b.processor->getABSlot() == 0);
    // The restored instance renders exactly like the original.
    Host c;
    c.processor->setStateInformation (block.getData(), (int) block.getSize());
    CHECK (maxAbsDiff (a.render (8000, { noteOn (0, 33, 127) }), c.render (8000, { noteOn (0, 33, 127) })) == 0.0);
}

TEST_CASE ("malformed, empty, foreign and out-of-range state never crash and never leak bad values")
{
    Host host;
    host.setDisplay (params::startFreq, 300.0f);
    host.processor->setStateInformation (nullptr, 0);
    host.processor->setStateInformation ("", 0);
    const char garbage[] = "\x00\x01\x02not xml at all\xff\xfe";
    host.processor->setStateInformation (garbage, (int) sizeof (garbage));
    std::mt19937 rng (7);
    std::vector<unsigned char> random (4096);
    for (auto& byte : random) byte = (unsigned char) rng();
    host.processor->setStateInformation (random.data(), (int) random.size());
    juce::XmlElement foreign ("SomeOtherPlugin");
    foreign.setAttribute ("startFreq", 5.0);
    juce::MemoryBlock foreignBlock;
    juce::AudioProcessor::copyXmlToBinary (foreign, foreignBlock);
    host.processor->setStateInformation (foreignBlock.getData(), (int) foreignBlock.getSize());
    CHECK_NEAR (host.getDisplay (params::startFreq), 300.0f, 1e-3);   // unchanged by every bad input

    // Well-formed root with NaN, Inf, out-of-range and junk attributes, from a
    // "newer" schema version: known fields are read best-effort, junk is ignored.
    host.setDisplay (params::drive, 2.0f);
    juce::XmlElement bad ("KickCrafter");
    bad.setAttribute ("version", 999);
    auto* p = bad.createNewChildElement ("Params");
    p->setAttribute ("startFreq", "nan");
    p->setAttribute ("endFreq", 1e9);
    p->setAttribute ("sweep", -50.0);
    p->setAttribute ("hold", "inf");
    p->setAttribute ("drive", "banana");
    p->setAttribute ("pitchSource", 7);
    p->setAttribute ("midiChannel", -3);
    auto* ui = bad.createNewChildElement ("UI");
    ui->setAttribute ("scale", 100000);
    auto* preset = bad.createNewChildElement ("Preset");
    preset->setAttribute ("index", 99);
    juce::MemoryBlock badBlock;
    juce::AudioProcessor::copyXmlToBinary (bad, badBlock);
    host.processor->setStateInformation (badBlock.getData(), (int) badBlock.getSize());
    CHECK_NEAR (host.getDisplay (params::startFreq), 300.0f, 1e-3);   // NaN rejected
    CHECK_NEAR (host.getDisplay (params::endFreq), 440.0f, 1e-3);     // clamped
    CHECK_NEAR (host.getDisplay (params::sweep), 0.0f, 1e-3);         // clamped
    CHECK_NEAR (host.getDisplay (params::hold), 132.0f, 1e-3);        // Inf rejected
    CHECK_NEAR (host.getDisplay (params::pitchSource), 1.0f, 1e-3);   // clamped to the last choice (MIDI Note)
    CHECK_NEAR (host.getDisplay (params::midiChannel), 0.0f, 1e-3);   // clamped to Omni
    CHECK_NEAR (host.getDisplay (params::drive), 2.0f, 1e-3);         // "banana" ignored, not zero
    CHECK (host.processor->getUIScalePercent() == 100);
    CHECK (host.processor->getPresetIndex() == 0);
    const auto out = host.render (8000, { noteOn (0, 33, 127) });
    CHECK (allFinite (out) && peakAbs (out) > 0.1f);                  // still audible

    // Missing / invalid version: the whole document is rejected.
    for (const char* version : { "", "abc", "0", "-1", "1.5" })
    {
        juce::XmlElement doc ("KickCrafter");
        if (juce::String (version).isNotEmpty()) doc.setAttribute ("version", version);
        doc.createNewChildElement ("Params")->setAttribute ("startFreq", 777.0);
        juce::MemoryBlock b;
        juce::AudioProcessor::copyXmlToBinary (doc, b);
        host.processor->setStateInformation (b.getData(), (int) b.getSize());
        CHECK_MSG (std::fabs (host.getDisplay (params::startFreq) - 300.0f) < 1e-3, std::string ("version '") + version + "' accepted");
    }
    // Strict numeric grammar: partial tokens are rejected and keep
    // the previous value; valid scientific notation and endpoints are accepted.
    for (const char* text : { "--1", "1.2.3", "1e2e3", "1e", "e5", "+", "-", ".", "0x10", "1,5", "1 2" })
        CHECK_MSG (! params::isStrictNumberToken (text), std::string ("accepted junk token ") + text);
    for (const char* text : { "1e100", "-1e100", "1e39" })   // grammatical but not representable as float: rejected
    {
        float v = 0.0f;
        CHECK_MSG (params::isStrictNumberToken (text) && ! params::parseStrictFloat (text, v), std::string ("representability ") + text);
    }
    { float v = 0.0f; CHECK (params::parseStrictFloat ("3.4e38", v) && v > 3.0e38f); }
    for (const char* text : { "1", "-1", "+2.5", ".5", "5.", "1e3", "1E-2", "  7  ", "0" })
        CHECK_MSG (params::isStrictNumberToken (text), std::string ("rejected valid token ") + text);
    host.setDisplay (params::drive, 2.0f);
    host.setDisplay (params::sweep, 30.0f);
    {
        juce::XmlElement doc ("KickCrafter");
        doc.setAttribute ("version", 1);
        auto* pp = doc.createNewChildElement ("Params");
        pp->setAttribute ("drive", "--1");
        pp->setAttribute ("sweep", "1.2.3");
        pp->setAttribute ("hold", "1e2");        // valid scientific: 100 ms
        pp->setAttribute ("shape", "5e1");       // 50 %
        auto* abNode = doc.createNewChildElement ("AB");
        abNode->setAttribute ("slot", 0);
        auto* other = abNode->createNewChildElement ("Other");
        other->setAttribute ("drive", "1e2e3");  // junk: keep previous other-slot value
        other->setAttribute ("startFreq", "1e100"); // not representable: rejected
        other->setAttribute ("hold", "9e9");     // finite, representable, out of range: clamped to 1000
        other->setAttribute ("sweep", "2.5e1");  // 25 ms
        juce::MemoryBlock b;
        juce::AudioProcessor::copyXmlToBinary (doc, b);
        const auto before = host.processor->getOtherSlotValues();
        host.processor->setStateInformation (b.getData(), (int) b.getSize());
        CHECK_NEAR (host.getDisplay (params::drive), 2.0f, 1e-4);      // "--1" ignored
        CHECK_NEAR (host.getDisplay (params::sweep), 30.0f, 1e-4);     // "1.2.3" ignored
        CHECK_NEAR (host.getDisplay (params::hold), 100.0f, 1e-4);
        CHECK_NEAR (host.getDisplay (params::shape), 50.0f, 1e-4);
        const auto after = host.processor->getOtherSlotValues();
        auto slotIndex = [] (const char* id) { for (size_t i = 0; i < params::synthesisIds.size(); ++i) if (juce::String (params::synthesisIds[i]) == id) return i; return (size_t) 0; };
        CHECK_NEAR (after[slotIndex (params::drive)], before[slotIndex (params::drive)], 1e-6);
        CHECK_NEAR (after[slotIndex (params::startFreq)], before[slotIndex (params::startFreq)], 1e-6);
        CHECK_NEAR (after[slotIndex (params::hold)], 1000.0f, 1e-4);
        CHECK_NEAR (after[slotIndex (params::sweep)], 25.0f, 1e-4);
    }
    // Numeric overflow text and legitimate clamping.
    juce::XmlElement big ("KickCrafter");
    big.setAttribute ("version", 1);
    auto* bp = big.createNewChildElement ("Params");
    bp->setAttribute ("startFreq", "1e400");
    bp->setAttribute ("hold", 5000.0);
    juce::MemoryBlock bb;
    juce::AudioProcessor::copyXmlToBinary (big, bb);
    host.processor->setStateInformation (bb.getData(), (int) bb.getSize());
    CHECK_NEAR (host.getDisplay (params::startFreq), 300.0f, 1e-3);   // overflow rejected
    CHECK_NEAR (host.getDisplay (params::hold), 1000.0f, 1e-3);       // finite out-of-range clamps
}

namespace
{
    struct SavedState { float drive = 0, shape = 0, otherDrive = 0, otherShape = 0; int slot = -1, preset = -1; bool ok = false; };
    SavedState parseSaved (KickCrafterProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        SavedState st;
        const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        if (xml == nullptr) return st;
        const auto tree = juce::ValueTree::fromXml (*xml);
        const auto params = tree.getChildWithName ("Params");
        const auto ab = tree.getChildWithName ("AB");
        const auto other = ab.getChildWithName ("Other");
        if (! params.isValid() || ! other.isValid()) return st;
        st.drive = (float) (double) params.getProperty ("drive");
        st.shape = (float) (double) params.getProperty ("shape");
        st.otherDrive = (float) (double) other.getProperty ("drive");
        st.otherShape = (float) (double) other.getProperty ("shape");
        st.slot = (int) ab.getProperty ("slot");
        st.preset = (int) tree.getChildWithName ("Preset").getProperty ("index");
        st.ok = true;
        return st;
    }
    // A = drive 3.5 / shape 100 in slot 0; B = drive 0.5 / shape 0 in slot 1.
    bool isCoherentAB (const SavedState& st)
    {
        auto near = [] (float a, float b) { return std::fabs (a - b) < 1e-3f; };
        const bool aCurrent = near (st.drive, 3.5f) && near (st.shape, 100.0f) && near (st.otherDrive, 0.5f) && near (st.otherShape, 0.0f) && st.slot == 0;
        const bool bCurrent = near (st.drive, 0.5f) && near (st.shape, 0.0f) && near (st.otherDrive, 3.5f) && near (st.otherShape, 100.0f) && st.slot == 1;
        return st.ok && (aCurrent || bCurrent);
    }
}

TEST_CASE ("A/B/state transactions are coherent: concurrent saves/restores during toggles, and re-entrant saves from callbacks")
{
    // Distinguishable slots: A = drive 3.5 / shape 100 (slot 0), B = drive 0.5 / shape 0 (slot 1).
    Host host (48000.0, 256);
    host.setDisplay (params::hold, 400.0f);
    host.setDisplay (params::drive, 0.5f);
    host.setDisplay (params::shape, 0.0f);
    host.processor->copyCurrentToOtherSlot();     // B stored
    host.setDisplay (params::drive, 3.5f);
    host.setDisplay (params::shape, 100.0f);      // A current, slot 0
    REQUIRE (isCoherentAB (parseSaved (*host.processor)));
    juce::MemoryBlock coherentA;
    host.processor->getStateInformation (coherentA);

    std::atomic<bool> run { true };
    std::atomic<int> saves { 0 }, incoherent { 0 }, toggles { 0 }, restores { 0 }, audioBlocks { 0 };
    std::atomic<bool> audioFinite { true };
    std::atomic<double> maxBlockMicros { 0.0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, 256);
        int n = 0;
        while (run.load())
        {
            juce::MidiBuffer midi;
            if (n % 20 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 33 + (n / 20) % 12, (juce::uint8) 100), 3);
            buffer.clear();
            const auto t0 = juce::Time::getHighResolutionTicks();
            host.processor->processBlock (buffer, midi);
            const double micros = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1e6;
            double prev = maxBlockMicros.load();
            while (micros > prev && ! maxBlockMicros.compare_exchange_weak (prev, micros)) {}
            for (int i = 0; i < 256; ++i)
                if (! std::isfinite (buffer.getReadPointer (0)[i])) audioFinite = false;
            audioBlocks = ++n;
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });
    std::thread toggler ([&]
    {
        int i = 0;
        while (run.load())
        {
            if (i++ % 7 == 6) { host.processor->setStateInformation (coherentA.getData(), (int) coherentA.getSize()); ++restores; }
            else { host.processor->toggleAB(); ++toggles; }
            std::this_thread::sleep_for (std::chrono::microseconds (120));
        }
    });
    std::thread saver ([&]
    {
        while (run.load())
        {
            if (! isCoherentAB (parseSaved (*host.processor))) ++incoherent;
            ++saves;
            std::this_thread::sleep_for (std::chrono::microseconds (90));
        }
    });
    std::this_thread::sleep_for (std::chrono::milliseconds (1200));
    run = false;
    audio.join(); toggler.join(); saver.join();
    CHECK (audioFinite.load());
    CHECK (audioBlocks.load() > 200);
    CHECK (toggles.load() > 100 && restores.load() > 10 && saves.load() > 100);
    CHECK_MSG (incoherent.load() == 0, "incoherent saves: " + std::to_string (incoherent.load()));
    CHECK (isCoherentAB (parseSaved (*host.processor)));
    // Timing is wall-clock evidence on a shared machine (scheduler noise included); the
    // audio thread never takes the control lock by construction, see PluginProcessor.h.
    std::printf ("  concurrent coherence: %d audio blocks, %d toggles, %d restores, %d saves, %d incoherent, max processBlock %.0f us\n",
                 audioBlocks.load(), toggles.load(), restores.load(), saves.load(), incoherent.load(), maxBlockMicros.load());
    CHECK_MSG (maxBlockMicros.load() < 20000.0, "max processBlock time " + std::to_string (maxBlockMicros.load()) + " us");

    // Re-entrant saves: a synchronous parameter-changed callback,
    // i.e. after at least one real mutation inside toggleAB / loadFactoryPreset /
    // setStateInformation, saves state and must see the COMPLETE before state: every
    // parameter, the other slot, the slot index and the preset index. Exactly one
    // capture per operation. Diagnostics (UI scale, thread note) are excluded.
    struct FullState
    {
        std::map<juce::String, double> params, other;
        int slot = -1, preset = -1;
        bool ok = false;
        bool operator== (const FullState& o) const { return ok && o.ok && params == o.params && other == o.other && slot == o.slot && preset == o.preset; }
    };
    auto fullState = [] (KickCrafterProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        FullState st;
        const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        if (xml == nullptr) return st;
        const auto tree = juce::ValueTree::fromXml (*xml);
        const auto pr = tree.getChildWithName ("Params");
        const auto ab = tree.getChildWithName ("AB");
        const auto ot = ab.getChildWithName ("Other");
        for (auto* id : params::allIds) st.params[id] = std::round ((double) pr.getProperty (id) * 1e4) / 1e4;
        for (auto* id : params::synthesisIds) st.other[id] = std::round ((double) ot.getProperty (id) * 1e4) / 1e4;
        st.slot = (int) ab.getProperty ("slot");
        st.preset = (int) tree.getChildWithName ("Preset").getProperty ("index");
        st.ok = pr.isValid() && ot.isValid();
        return st;
    };
    struct ReentrantSaver final : juce::AudioProcessorListener
    {
        ReentrantSaver (KickCrafterProcessor& p, std::function<FullState (KickCrafterProcessor&)> f) : proc (p), full (std::move (f)) {}
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override
        {
            ++mutationCallbacks;
            if (armed && ! inside) { inside = true; seen = full (proc); ++captures; armed = false; inside = false; }
        }
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override {}   // deliberately not a capture point
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        KickCrafterProcessor& proc;
        std::function<FullState (KickCrafterProcessor&)> full;
        FullState seen;
        bool armed = false, inside = false;
        int captures = 0, mutationCallbacks = 0;
    };
    Host h2;
    h2.setDisplay (params::drive, 0.5f); h2.setDisplay (params::shape, 0.0f);
    h2.processor->copyCurrentToOtherSlot();
    h2.setDisplay (params::drive, 3.5f); h2.setDisplay (params::shape, 100.0f); h2.setDisplay (params::startFreq, 640.0f);
    ReentrantSaver saver2 (*h2.processor, fullState);
    h2.processor->addListener (&saver2);

    auto exercise = [&] (const char* what, std::function<void()> operation)
    {
        const FullState before = fullState (*h2.processor);
        saver2.armed = true;
        const int callbacksBefore = saver2.mutationCallbacks;
        const int capturesBefore = saver2.captures;
        operation();
        const FullState after = fullState (*h2.processor);
        CHECK_MSG (saver2.mutationCallbacks > callbacksBefore, std::string (what) + ": no parameter mutation callback executed");
        CHECK_MSG (saver2.captures == capturesBefore + 1, std::string (what) + ": expected exactly one re-entrant capture");
        CHECK_MSG (saver2.seen == before, std::string (what) + ": re-entrant save did not return the complete BEFORE state");
        CHECK_MSG (! (after == before), std::string (what) + ": operation changed nothing (vacuous)");
        return after;
    };
    // toggle: A (drive 3.5 / shape 100 / start 640) -> B (drive 0.5 / shape 0 / start 250)
    const auto afterToggle = exercise ("toggle", [&] { h2.processor->toggleAB(); });
    CHECK (afterToggle.slot == 1 && std::fabs (afterToggle.params.at ("drive") - 0.5) < 1e-3 && std::fabs (afterToggle.other.at ("drive") - 3.5) < 1e-3);
    CHECK (isCoherentAB (parseSaved (*h2.processor)));
    // preset: B -> Gabber (drive 4, shape 100, start 640)
    const auto afterPreset = exercise ("preset", [&] { h2.processor->loadFactoryPreset (7); });
    CHECK (afterPreset.preset == 7 && std::fabs (afterPreset.params.at ("drive") - 4.0) < 1e-3 && std::fabs (afterPreset.params.at ("startFreq") - 640.0) < 0.1);
    // restore: Gabber -> the saved coherent A state
    const auto afterRestore = exercise ("restore", [&] { h2.processor->setStateInformation (coherentA.getData(), (int) coherentA.getSize()); });
    CHECK (afterRestore.slot == 0 && std::fabs (afterRestore.params.at ("drive") - 3.5) < 1e-3 && std::fabs (afterRestore.other.at ("drive") - 0.5) < 1e-3);
    h2.processor->removeListener (&saver2);
    CHECK (isCoherentAB (parseSaved (*h2.processor)) && parseSaved (*h2.processor).slot == 0);

    // Deterministic transaction check: a state saved between toggles must hold BOTH
    // sounds (current + other) and the matching slot, never the same values twice.
    Host h3;
    h3.setDisplay (params::drive, 3.5f);
    h3.processor->copyCurrentToOtherSlot();
    h3.processor->toggleAB();
    h3.setDisplay (params::drive, 0.5f);      // B = 0.5, A = 3.5
    const auto st = parseSaved (*h3.processor);
    CHECK (st.slot == 1 && std::fabs (st.drive - 0.5f) < 1e-4f && std::fabs (st.otherDrive - 3.5f) < 1e-4f);
    juce::MemoryBlock saved;
    h3.processor->getStateInformation (saved);
    Host h4;
    h4.processor->setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (h4.processor->getABSlot() == 1);
    CHECK_NEAR (h4.getDisplay (params::drive), 0.5f, 1e-4);
    h4.processor->toggleAB();
    CHECK_NEAR (h4.getDisplay (params::drive), 3.5f, 1e-4);
}

TEST_CASE ("presets and copies under concurrent audio: finite output, bounded callback, no crash")
{
    Host host (48000.0, 256);
    host.setDisplay (params::hold, 400.0f);
    std::atomic<bool> run { true };
    std::atomic<bool> audioFinite { true };
    std::atomic<int> audioBlocks { 0 }, ops { 0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, 256);
        int n = 0;
        while (run.load())
        {
            juce::MidiBuffer midi;
            if (n % 20 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            buffer.clear();
            host.processor->processBlock (buffer, midi);
            for (int i = 0; i < 256; ++i) if (! std::isfinite (buffer.getReadPointer (0)[i])) audioFinite = false;
            audioBlocks = ++n;
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });
    std::thread control ([&]
    {
        int i = 0;
        while (run.load())
        {
            switch (i++ % 4)
            {
                case 0: host.processor->loadFactoryPreset (i % presets::count()); break;
                case 1: host.processor->copyCurrentToOtherSlot(); break;
                case 2: host.setDisplay (params::drive, 3.0f); break;
                default: host.processor->toggleAB(); break;
            }
            ++ops;
            std::this_thread::sleep_for (std::chrono::microseconds (150));
        }
    });
    std::this_thread::sleep_for (std::chrono::milliseconds (600));
    run = false;
    audio.join(); control.join();
    CHECK (audioFinite.load());
    CHECK (audioBlocks.load() > 100 && ops.load() > 100);
    CHECK (parseSaved (*host.processor).ok);
}

TEST_CASE ("note names use one octave convention everywhere (C4 = MIDI 60)")
{
    CHECK (params::noteNameForMidi (33) == "A1");
    CHECK (params::noteNameForMidi (60) == "C4");
    CHECK (params::noteNameForMidi (0) == "C-1");
    CHECK (params::noteNameForHz (55.0f) == "A1");
    CHECK (params::noteNameForHz (midiNoteToHz (33.0f)) == params::noteNameForMidi (33));
    for (int n = 0; n < 128; ++n)
        CHECK_MSG (params::noteNameForHz (midiNoteToHz ((float) n)) == params::noteNameForMidi (n), "note " + std::to_string (n));
}

// ------------------------------------------------------------ instances --

TEST_CASE ("two instances are independent: parameters, presets, A/B and audio")
{
    Host one, two;
    one.setDisplay (params::shape, 100.0f);
    one.processor->loadFactoryPreset (3);
    one.processor->toggleAB();                    // slot B: still the defaults
    one.setDisplay (params::drive, 3.0f);         // make B distinct too
    CHECK_NEAR (two.getDisplay (params::shape), 0.0f, 1e-6);
    CHECK_NEAR (two.getDisplay (params::drive), 1.0f, 1e-6);
    CHECK (two.processor->getPresetIndex() == 0);
    CHECK (two.processor->getABSlot() == 0);
    CHECK (one.processor->getABSlot() == 1);
    const auto a = one.render (8000, { noteOn (0, 33, 127) });
    const auto b = two.render (8000, { noteOn (0, 33, 127) });
    CHECK (maxAbsDiff (b, engineHit (KickParams {}, 48000.0, 33, 1.0f, 8000)) == 0.0);
    CHECK (maxAbsDiff (a, b) > 1e-3);
    // Interleaved processing does not cross-talk.
    Host three, four;
    three.setDisplay (params::hold, 500.0f);
    juce::AudioBuffer<float> b3 (2, 256), b4 (2, 256);
    juce::MidiBuffer m3, m4;
    m3.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 127), 0);
    three.processor->processBlock (b3, m3);
    four.processor->processBlock (b4, m4);
    CHECK (b4.getMagnitude (0, 256) == 0.0f);
    CHECK (b3.getMagnitude (0, 256) > 0.1f);
    CHECK (four.processor->getActiveVoiceCount() == 0 && three.processor->getActiveVoiceCount() == 1);
}

TEST_CASE ("sample-rate changes: 44.1/48/96 kHz hits keep their pitch and length, active voices reset")
{
    Host host;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        host.prepare (sr, 512);
        CHECK (host.processor->getCurrentSampleRate() == sr);
        const auto out = host.render ((int) (0.25 * sr), { noteOn (0, 33, 127) });
        CHECK_NEAR (zeroCrossingHz (out, (size_t) (0.05 * sr), (size_t) (0.084 * sr), sr), 55.0, 0.8);
        CHECK (peakAbs (out, (size_t) (0.1795 * sr) + 1) == 0.0f);
        CHECK (peakAbs (out, (size_t) (0.05 * sr), (size_t) (0.08 * sr)) > 0.7f);
    }
    host.setDisplay (params::hold, 1000.0f);
    host.render (4096, { noteOn (0, 33, 127) });
    CHECK (host.processor->getActiveVoiceCount() == 1);
    host.prepare (48000.0, 256);
    const auto silent = host.render (4096, {});
    CHECK (peakAbs (silent) == 0.0f);
}

TEST_CASE ("audition button path: realtime flag triggers A1 at full velocity on the next block")
{
    Host host;
    host.processor->triggerAudition();
    const auto out = host.render (9000, {});
    CHECK (maxAbsDiff (out, engineHit (KickParams {}, 48000.0, KickCrafterProcessor::auditionNote, 1.0f, 9000)) == 0.0);
    CHECK (host.processor->getLastNote() == 33);
    CHECK (host.processor->getNoteCounter() == 1);
}

TEST_CASE ("presets: curated set loads, marks modification, only affects the parameter model")
{
    Host host;
    CHECK (presets::count() == 8);
    for (int i = 0; i < presets::count(); ++i)
    {
        host.processor->loadFactoryPreset (i);
        CHECK (host.processor->getPresetIndex() == i);
        CHECK (host.processor->currentValuesMatchPreset (i));
        // Log-mapped values (frequencies, curve) roundtrip through the host's normalised
        // value with ~1e-5 relative error; everything else is exact.
        const KickParams got = host.processor->getCurrentParams();
        const KickParams want = presets::factory()[(size_t) i].params.sanitized();
        CHECK_NEAR (got.startHz, want.startHz, want.startHz * 2e-5f);
        CHECK_NEAR (got.endHz, want.endHz, want.endHz * 2e-5f);
        CHECK_NEAR (got.slope, want.slope, want.slope * 2e-5f);
        CHECK_NEAR (got.attackSec, want.attackSec, want.attackSec * 2e-5f);   // skewed range: same ~1e-5 roundtrip
        CHECK (got.sweepSec == want.sweepSec && got.holdSec == want.holdSec && got.fadeFraction == want.fadeFraction
               && got.morph == want.morph && got.gain == want.gain
               && got.velocitySensitive == want.velocitySensitive && got.pitchSource == want.pitchSource);
        const auto out = host.render (4096, { noteOn (0, 33, 127) });
        CHECK (allFinite (out) && peakAbs (out) > 0.05f);
    }
    host.setDisplay (params::drive, 0.1234f);
    CHECK (! host.processor->currentValuesMatchPreset (host.processor->getPresetIndex()));

    // No host programs (review 022): after a non-default native preset (+ A/B state), host
    // setCurrentProgram calls (0, a former valid index, out of range) change nothing.
    host.processor->loadFactoryPreset (5);              // Square Growl
    host.processor->copyCurrentToOtherSlot();
    host.processor->toggleAB();                         // slot 1
    host.setDisplay (params::drive, 0.75f);
    const KickParams frozen = host.processor->getCurrentParams();
    const auto other = host.processor->getOtherSlotValues();
    for (int program : { 0, 5, 7, 99, -1 })
    {
        host.processor->setCurrentProgram (program);
        CHECK (host.processor->getCurrentProgram() == 0);
        CHECK (host.processor->getNumPrograms() == 1);
        CHECK (host.processor->getCurrentParams() == frozen);
        CHECK (host.processor->getPresetIndex() == 5);
        CHECK (host.processor->getABSlot() == 1);
        CHECK (host.processor->getOtherSlotValues() == other);
    }
    // Native preset name/index roundtrip through the saved state.
    juce::MemoryBlock block;
    host.processor->getStateInformation (block);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    REQUIRE (xml != nullptr);
    const auto tree = juce::ValueTree::fromXml (*xml);
    CHECK ((int) tree.getChildWithName ("Preset").getProperty ("index") == 5);
    CHECK (tree.getChildWithName ("Preset").getProperty ("name").toString() == "Square Growl");
    Host again;
    again.processor->setStateInformation (block.getData(), (int) block.getSize());
    CHECK (again.processor->getPresetIndex() == 5 && again.processor->getABSlot() == 1);
    CHECK (again.processor->getCurrentParams() == frozen);
}

// --------------------------------------------------------------- editor --

TEST_CASE ("v1.0 state (schema 1) migrates the pitch-source index; schema 2 is stored as is")
{
    // Schema 1 stored 0 = MIDI Note, 1 = Fixed. Schema 2 (v1.1) stores 0 = Fixed, 1 = MIDI Note.
    for (int oldValue : { 0, 1 })
    {
        Host host;
        juce::XmlElement doc ("KickCrafter");
        doc.setAttribute ("version", 1);
        auto* pp = doc.createNewChildElement ("Params");
        pp->setAttribute ("pitchSource", oldValue);
        pp->setAttribute ("startFreq", 300.0);
        auto* ab = doc.createNewChildElement ("AB");
        ab->setAttribute ("slot", 0);
        auto* other = ab->createNewChildElement ("Other");
        other->setAttribute ("pitchSource", oldValue);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (doc, block);
        host.processor->setStateInformation (block.getData(), (int) block.getSize());
        const float expected = oldValue == 1 ? 0.0f : 1.0f;    // Fixed -> index 0, MIDI Note -> index 1
        CHECK_NEAR (host.getDisplay (params::pitchSource), expected, 1e-6);
        CHECK (host.processor->getCurrentParams().pitchSource == (oldValue == 1 ? PitchSource::fixed : PitchSource::midiNote));
        const auto otherValues = host.processor->getOtherSlotValues();
        CHECK_NEAR (otherValues[params::synthesisIds.size() - 1], expected, 1e-6);   // pitchSource is the last synthesis id
        CHECK_NEAR (host.getDisplay (params::startFreq), 300.0f, 1e-3);
    }
    // Schema 2 documents are not migrated.
    Host host;
    juce::XmlElement doc ("KickCrafter");
    doc.setAttribute ("version", 2);
    doc.createNewChildElement ("Params")->setAttribute ("pitchSource", 1);
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary (doc, block);
    host.processor->setStateInformation (block.getData(), (int) block.getSize());
    CHECK_NEAR (host.getDisplay (params::pitchSource), 1.0f, 1e-6);
    CHECK (host.processor->getCurrentParams().pitchSource == PitchSource::midiNote);
    // v1.3: `velocity` was a 0-100 % amount up to schema 3 (any amount > 0 -> on); schema 4 stores 0/1
    struct Legacy { int version; double stored; bool on; };
    for (const Legacy l : { Legacy { 1, 100.0, true }, Legacy { 2, 60.0, true }, Legacy { 3, 0.0, false }, Legacy { 3, 0.4, true },
                            Legacy { 4, 1.0, true }, Legacy { 4, 0.0, false }, Legacy { 4, 0.6, true } })
    {
        Host h;
        juce::XmlElement d ("KickCrafter");
        d.setAttribute ("version", l.version);
        d.createNewChildElement ("Params")->setAttribute ("velocity", l.stored);
        auto* abNode = d.createNewChildElement ("AB");
        abNode->setAttribute ("slot", 0);
        abNode->createNewChildElement ("Other")->setAttribute ("velocity", l.stored);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (d, mb);
        h.processor->setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK_MSG (h.getDisplay (params::velocity) == (l.on ? 1.0f : 0.0f), "velocity schema " + std::to_string (l.version) + " value " + std::to_string (l.stored));
        CHECK (h.processor->getCurrentParams().velocitySensitive == l.on);
        CHECK (h.processor->getOtherSlotValues()[params::synthesisIds.size() - 2] == (l.on ? 1.0f : 0.0f));   // velocity precedes pitchSource
    }
}

TEST_CASE ("editor graphs (v1.1): knee only edits the sweep, curve handle spans 1..30, time handles can pass the visible axis, preset re-pick reloads")
{
    REQUIRE (juce::Desktop::getInstance().getDisplays().displays.size() > 0);
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (100); };
    auto makeEvent = [] (juce::Component& target, juce::Point<float> pos, bool down)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos,
                                 down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, juce::Time::getCurrentTime(), pos,
                                 juce::Time::getCurrentTime(), 1, false);
    };
    Host host;
    std::unique_ptr<juce::AudioProcessorEditor> editor (host.processor->createEditor());
    auto* kc = dynamic_cast<KickCrafterEditor*> (editor.get());
    REQUIRE (kc != nullptr);
    editor->setVisible (true);
    editor->addToDesktop (0);
    pump();

    // 1. Fixed mode (default): dragging the knee vertically leaves the End frequency alone.
    auto& pitch = kc->getPitchGraph();
    kc->pollNow(); pump();
    REQUIRE (pitch.getHandles().size() == 3);
    const float endBefore = host.getDisplay (params::endFreq);
    const auto knee = pitch.getHandles()[1];
    CHECK (knee.lockedVertical && knee.secondaryId.isEmpty());
    pitch.mouseDown (makeEvent (pitch, knee.position, true));
    pitch.mouseDrag (makeEvent (pitch, knee.position.translated (25.0f, -80.0f), true));
    pitch.mouseUp (makeEvent (pitch, knee.position.translated (25.0f, -80.0f), false));
    CHECK_NEAR (host.getDisplay (params::endFreq), endBefore, 1e-4);
    CHECK (host.getDisplay (params::sweep) > 47.0f);
    host.setDisplay (params::sweep, 47.0f);
    kc->pollNow(); pump();

    // 2. Curve handle follows the mouse (v1.5): on the falling default sweep (250 -> 55 Hz) a
    //    steeper exponent pulls the mid-sweep pitch DOWN, so dragging down over the whole plot
    //    height reaches the knob's maximum (30) and dragging up over it reaches the minimum (1).
    const auto plot = pitch.plotArea();
    auto curveHandle = pitch.getHandles()[2];
    CHECK (curveHandle.id == juce::String (params::curve));
    const float midBefore = curveHandle.position.y;
    pitch.mouseDown (makeEvent (pitch, curveHandle.position, true));
    pitch.mouseDrag (makeEvent (pitch, curveHandle.position.translated (0.0f, plot.getHeight()), true));
    pitch.mouseUp (makeEvent (pitch, curveHandle.position.translated (0.0f, plot.getHeight()), false));
    CHECK_NEAR (host.getDisplay (params::curve), 30.0f, 0.05f);
    kc->pollNow(); pump();
    curveHandle = pitch.getHandles()[2];
    CHECK (curveHandle.position.y > midBefore + 10.0f);          // the handle moved the way the mouse went
    pitch.mouseDown (makeEvent (pitch, curveHandle.position, true));
    pitch.mouseDrag (makeEvent (pitch, curveHandle.position.translated (0.0f, -plot.getHeight()), true));
    pitch.mouseUp (makeEvent (pitch, curveHandle.position.translated (0.0f, -plot.getHeight()), false));
    CHECK_NEAR (host.getDisplay (params::curve), 1.0f, 0.05f);
    host.setDisplay (params::curve, 1.0f);
    kc->pollNow(); pump();
    // Rising sweep (start below end): the same rule, mirrored. Dragging UP steepens (20 -> 440 Hz:
    // the mid-sweep pitch goes from 230 Hz at exponent 1 to about 440 Hz at 30, a visible climb).
    host.setDisplay (params::startFreq, 20.0f);
    host.setDisplay (params::endFreq, 440.0f);
    kc->pollNow(); pump();
    curveHandle = pitch.getHandles()[2];
    const float risingBefore = curveHandle.position.y;
    pitch.mouseDown (makeEvent (pitch, curveHandle.position, true));
    pitch.mouseDrag (makeEvent (pitch, curveHandle.position.translated (0.0f, -plot.getHeight()), true));
    pitch.mouseUp (makeEvent (pitch, curveHandle.position.translated (0.0f, -plot.getHeight()), false));
    CHECK_NEAR (host.getDisplay (params::curve), 30.0f, 0.05f);
    kc->pollNow(); pump();
    CHECK (pitch.getHandles()[2].position.y < risingBefore - 8.0f);
    host.setDisplay (params::curve, 1.0f);
    host.setDisplay (params::startFreq, 250.0f);
    host.setDisplay (params::endFreq, 55.0f);
    kc->pollNow(); pump();

    // 3. Hold handle dragged past the plot's right edge keeps growing beyond the visible axis.
    auto& amp = kc->getAmplitudeGraph();
    REQUIRE (amp.getHandles().size() == 3);
    const auto end = amp.getHandles()[2];
    CHECK (end.id == juce::String (params::hold));
    const auto ampPlot = amp.plotArea();
    const float holdBefore = host.getDisplay (params::hold);
    amp.mouseDown (makeEvent (amp, end.position, true));
    amp.mouseDrag (makeEvent (amp, { ampPlot.getRight() + ampPlot.getWidth(), end.position.y }, true));   // one width beyond
    const float holdDuringDrag = host.getDisplay (params::hold);
    amp.mouseUp (makeEvent (amp, { ampPlot.getRight() + ampPlot.getWidth(), end.position.y }, false));
    CHECK (holdDuringDrag > holdBefore * 4.0f);                 // x6 axis growth minus the sweep
    CHECK (holdDuringDrag <= 1000.0f);
    host.setDisplay (params::hold, 132.0f);
    kc->pollNow(); pump();

    // 3b. Release after the axis grew (1.5.1 fix): the preview rebuilt during the drag laid the
    //     handles out on the frozen axis; on release they are laid out again on the rescaled one,
    //     so the end handle sits at the hit end without waiting for the next parameter change.
    {
        const auto handle = amp.getHandles()[2];
        const auto target = handle.position.translated (ampPlot.getWidth() * 0.3f, 0.0f);
        amp.mouseDown (makeEvent (amp, handle.position, true));
        amp.mouseDrag (makeEvent (amp, target, true));
        kc->pollNow(); pump();                                   // the preview (and its axis) rebuilds mid-drag
        amp.mouseUp (makeEvent (amp, target, false));
        CHECK (host.getDisplay (params::hold) > 132.0f);
        const double total = (double) (host.getDisplay (params::sweep) + host.getDisplay (params::hold)) / 1000.0;
        CHECK_NEAR (amp.getHandles()[2].position.x, amp.xForSeconds (total), 0.5f);
        host.setDisplay (params::hold, 132.0f);
        kc->pollNow(); pump();
    }

    // 4. Preset popup: picking the item that is already selected reloads the preset.
    host.processor->loadFactoryPreset (2);
    host.setDisplay (params::drive, 3.3f);                       // "edited"
    CHECK (! host.processor->currentValuesMatchPreset (2));
    auto* box = dynamic_cast<kcf::ui::PresetBox*> (&kc->getTopBar().getPresetBox());
    REQUIRE (box != nullptr && box->onPick);
    box->onPick (3);                                             // item id 3 = factory index 2
    kc->pollNow(); pump();
    CHECK (host.processor->currentValuesMatchPreset (2));
    CHECK (host.processor->getPresetIndex() == 2);

    editor.reset();
    pump();
}

// ---------------------------------------------------------- presets (v1.2) ----

TEST_CASE ("factory bank is embedded XML and equals the v1.1 C++ table exactly")
{
    // Expected values: the v1.1 table (start, end, sweep ms, hold ms, fade %, attack ms, curve, shape %, drive).
    struct Row { const char* name; float st, en, sw, ho, fa, at, cu, sh, dr; };
    const Row rows[] = {
        { "Reference",    250, 55, 47, 132, 50, 0.4f, 1,    0,   1 },
        { "Deep Sub",     140, 55, 38, 820, 88, 0.4f, 2.5f, 0,   1.15f },
        { "Techno Punch", 420, 55, 24, 240, 62, 0.4f, 4,    12,  1.6f },
        { "Tight Click",  950, 55, 11, 95,  55, 0.4f, 9,    28,  1 },
        { "Soft Thump",   150, 55, 62, 320, 82, 7,    1.6f, 0,   0.8f },
        { "Square Growl", 300, 55, 92, 420, 72, 0.4f, 2.2f, 70,  2.4f },
        { "Long Boom",    170, 55, 42, 720, 86, 0.4f, 2,    6,   1.3f },
        { "Gabber",       640, 55, 34, 260, 42, 0.4f, 1.6f, 100, 4 },
    };
    const auto& bank = presets::factory();
    REQUIRE (bank.size() == 8 && presets::count() == 8);
    for (size_t i = 0; i < 8; ++i)
    {
        const auto& r = rows[i];
        const auto& p = bank[i];
        CHECK_MSG (p.name == r.name, std::string ("bank order: ") + p.name.toStdString());
        CHECK (p.kind == presets::Kind::factory && p.file == juce::File());
        // identical to what the old make() produced: the same divisions, so exact equality holds
        CHECK (p.params.startHz == r.st && p.params.endHz == r.en);
        CHECK (p.params.sweepSec == r.sw / 1000.0f && p.params.holdSec == r.ho / 1000.0f);
        CHECK (p.params.fadeFraction == r.fa / 100.0f && p.params.attackSec == r.at / 1000.0f);
        CHECK (p.params.slope == r.cu && p.params.morph == r.sh / 100.0f && p.params.gain == r.dr);
        CHECK (! p.params.velocitySensitive && p.params.pitchSource == PitchSource::fixed);   // v1.5: Off in every factory preset
    }
    CHECK (bank[0].params == KickParams {});                     // Reference is the engine default
    CHECK (presets::findFactory ("Gabber") == &bank[7] && presets::findFactory ("nope") == nullptr);
}

TEST_CASE ("preset XML: exact roundtrip, strict rejection of malformed documents")
{
    for (const auto& p : presets::factory())
    {
        presets::Preset back; juce::String error;
        REQUIRE (presets::fromXml (presets::toXml (p.name, p.params), back, error));
        CHECK (back.name == p.name && back.params == p.params);
    }
    KickParams odd;
    odd.startHz = 1234.567f; odd.endHz = 61.7f; odd.sweepSec = 0.1234f; odd.holdSec = 0.9876f; odd.fadeFraction = 0.333f;
    odd.attackSec = 0.0123f; odd.slope = 17.25f; odd.morph = 0.42f; odd.gain = 3.3f; odd.velocitySensitive = false;
    odd.pitchSource = PitchSource::midiNote;
    presets::Preset back; juce::String error;
    REQUIRE (presets::fromXml (presets::toXml ("Odd one", odd), back, error));
    CHECK (back.name == "Odd one" && back.params == odd.sanitized());
    // out-of-range values are clamped by the engine, not rejected
    for (const auto& legacy : { std::pair<const char*, bool> { "100", true }, { "60", true }, { "0.4", true }, { "0", false } })
    {
        presets::Preset lp; juce::String le;
        const juce::String doc = juce::String (R"(<KickCrafterPreset version="3" name="Old"><Params startFreq="250" endFreq="55" sweep="47" hold="132" fade="50" attack="0.4" curve="1" shape="0" drive="1" velocity=")")
                                 + legacy.first + R"(" pitchSource="0"/></KickCrafterPreset>)";
        REQUIRE (presets::fromXml (doc, lp, le));
        CHECK (lp.params.velocitySensitive == legacy.second);           // schema <= 3: 0-100 % amount, > 0 -> on
    }
    {
        presets::Preset np; juce::String ne;                             // schema 4: 0/1 (0.6 -> on: >= 0.5)
        REQUIRE (presets::fromXml (R"(<KickCrafterPreset version="4" name="New"><Params startFreq="250" endFreq="55" sweep="47" hold="132" fade="50" attack="0.4" curve="1" shape="0" drive="1" velocity="0" pitchSource="0"/></KickCrafterPreset>)", np, ne));
        CHECK (! np.params.velocitySensitive);
        CHECK (presets::toXml ("New", np.params).contains ("velocity=\"0\"") && presets::toXml ("New", np.params).contains ("version=\"4\""));
    }
    const juce::String big = R"(<KickCrafterPreset version="3" name="Big"><Params startFreq="9999" endFreq="55" sweep="47" hold="5000" fade="50" attack="0.4" curve="1" shape="0" drive="1" velocity="100" pitchSource="0"/></KickCrafterPreset>)";
    REQUIRE (presets::fromXml (big, back, error));
    CHECK (back.params.startHz == Limits::maxStartHz && back.params.holdSec == Limits::maxHoldSec);
    // rejected documents
    const char* bad[] = {
        "",
        "<Other/>",
        R"(<KickCrafterPreset name="x"><Params startFreq="1" endFreq="55" sweep="47" hold="132" fade="50" attack="0.4" curve="1" shape="0" drive="1" velocity="100" pitchSource="0"/></KickCrafterPreset>)",   // no version
        R"(<KickCrafterPreset version="0" name="x"><Params/></KickCrafterPreset>)",
        R"(<KickCrafterPreset version="3" name=""><Params/></KickCrafterPreset>)",
        R"(<KickCrafterPreset version="3" name="x"/>)",                                                                  // no Params
        R"(<KickCrafterPreset version="3" name="x"><Params startFreq="250"/></KickCrafterPreset>)",                     // missing ids
        R"(<KickCrafterPreset version="3" name="x"><Params startFreq="1e2e3" endFreq="55" sweep="47" hold="132" fade="50" attack="0.4" curve="1" shape="0" drive="1" velocity="100" pitchSource="0"/></KickCrafterPreset>)",
        R"(<KickCrafterPreset version="3" name="x"><Params startFreq="nan" endFreq="55" sweep="47" hold="132" fade="50" attack="0.4" curve="1" shape="0" drive="1" velocity="100" pitchSource="0"/></KickCrafterPreset>)",
    };
    for (const char* doc : bad)
    {
        CHECK_MSG (! presets::fromXml (doc, back, error), std::string ("accepted: ") + doc);
        CHECK (error.isNotEmpty());
    }
    // names
    CHECK (presets::isValidName ("My Kick") && ! presets::isValidName ("") && ! presets::isValidName (" padded")
           && ! presets::isValidName ("a/b") && ! presets::isValidName (juce::String::repeatedString ("x", 65)));
    CHECK (presets::fileNameFor ("My Kick / 2 <3>") == "My Kick _ 2 _3_.xml" && presets::fileNameFor ("...") == "preset....xml");
}

TEST_CASE ("user preset library: save, scan, load, rename, delete, collisions, junk files (temporary directory)")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kcf-presets-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    struct Cleanup { juce::File d; ~Cleanup() { d.deleteRecursively(); } } cleanup { dir };
    presets::Library lib (dir);
    CHECK (lib.presets().empty() && lib.skippedFiles() == 0);          // missing directory = empty library
    juce::String error;
    KickParams a; a.startHz = 333.0f; a.sweepSec = 0.09f;
    REQUIRE (lib.save ("Zed", a, error));
    KickParams b; b.startHz = 444.0f;
    REQUIRE (lib.save ("alpha", b, error));
    REQUIRE (lib.presets().size() == 2);
    CHECK (lib.presets()[0].name == "alpha" && lib.presets()[1].name == "Zed");   // case-insensitive order
    CHECK (lib.find ("Zed")->params == a.sanitized() && lib.find ("Zed")->file.existsAsFile());
    CHECK (lib.find ("Zed")->kind == presets::Kind::user);
    // overwrite keeps one file
    a.gain = 2.0f;
    REQUIRE (lib.save ("Zed", a, error));
    CHECK (lib.presets().size() == 2 && lib.find ("Zed")->params.gain == 2.0f);
    CHECK (dir.findChildFiles (juce::File::findFiles, false, "*.xml").size() == 2);
    // a fresh scan of the same directory sees the same presets (persistence)
    presets::Library again (dir);
    CHECK (again.presets().size() == 2 && again.find ("alpha")->params == b.sanitized());
    // rename: collision refused, then applied with the file renamed
    CHECK (! lib.rename ("alpha", "Zed", error) && error.isNotEmpty());
    REQUIRE (lib.rename ("alpha", "Beta", error));
    CHECK (lib.find ("alpha") == nullptr && lib.find ("Beta") != nullptr && lib.find ("Beta")->params == b.sanitized());
    CHECK (lib.find ("Beta")->file.getFileName() == "Beta.xml");
    CHECK (! lib.rename ("nope", "x", error) && ! lib.rename ("Beta", "", error));
    // file-name collision between two distinct names that sanitise identically ("Beta<" / "Beta>" -> Beta_.xml)
    KickParams c; c.startHz = 555.0f;
    REQUIRE (lib.save ("Beta<", c, error));
    REQUIRE (lib.save ("Beta>", c, error));
    CHECK (lib.presets().size() == 4 && lib.find ("Beta<")->file != lib.find ("Beta>")->file);
    CHECK (lib.find ("Beta<")->file.getFileName() == "Beta_.xml" && lib.find ("Beta>")->file.getFileName() != "Beta_.xml");
    // files created behind the library's back are seen before save/rename decide (rescan first)
    const auto n = lib.presets().size();
    dir.getChildFile ("Ghost.xml").replaceWithText (presets::toXml ("Ghost", c));
    REQUIRE (lib.save ("Ghost", b, error));                           // overwrites that file in place: no shadow "Ghost (2).xml"
    CHECK (lib.presets().size() == n + 1 && lib.find ("Ghost")->file.getFileName() == "Ghost.xml");
    CHECK (lib.find ("Ghost")->params == b.sanitized());
    CHECK (dir.findChildFiles (juce::File::findFiles, false, "Ghost*.xml").size() == 1);
    dir.getChildFile ("Ghost2.xml").replaceWithText (presets::toXml ("Ghost2", c));
    CHECK (! lib.rename ("Ghost", "Ghost2", error) && error.contains ("already exists"));   // the hand-made name collides
    CHECK (lib.find ("Ghost2") != nullptr && lib.find ("Ghost") != nullptr);
    dir.getChildFile ("Ghost2.xml").deleteFile();
    CHECK (! lib.remove ("Ghost2", error));                            // gone behind our back: reported, not asserted
    REQUIRE (lib.remove ("Ghost", error));                             // back to the state the checks below expect
    CHECK (lib.presets().size() == n);
    REQUIRE (lib.remove ("Beta>", error));
    CHECK (lib.presets().size() == 3);
    // junk files are skipped with a reason, valid ones still load; duplicate names are skipped
    dir.getChildFile ("junk.xml").replaceWithText ("<KickCrafterPreset version=\"3\" name=\"J\"><Params startFreq=\"x\"/></KickCrafterPreset>");
    dir.getChildFile ("notes.txt").replaceWithText ("ignored");
    dir.getChildFile ("dup.xml").replaceWithText (presets::toXml ("Beta", c));
    lib.rescan();
    CHECK (lib.presets().size() == 3 && lib.skippedFiles() == 2);
    CHECK (lib.skippedReasons().size() == 2);
    CHECK (lib.find ("Beta")->params == b.sanitized());                // the original wins, the duplicate is skipped
    // delete
    REQUIRE (lib.remove ("Beta<", error));
    CHECK (lib.presets().size() == 2 && ! lib.find ("Beta<"));
    CHECK (! lib.remove ("Beta<", error));
    // invalid names never touch the disk
    CHECK (! lib.save ("", a, error) && ! lib.save ("a/b", a, error));
    CHECK (dir.findChildFiles (juce::File::findFiles, false, "*.xml").size() == 4);   // Zed, Beta, junk, dup
}

TEST_CASE ("loaded preset identity: factory/user loads, adopt after save, edited marker, state schema 3 and migration")
{
    Host host;
    auto loaded = host.processor->getLoadedPreset();
    CHECK (loaded.kind == presets::Kind::factory && loaded.name == "Reference" && loaded.factoryIndex == 0);
    CHECK (host.processor->currentValuesMatchLoadedPreset());
    host.setDisplay (params::drive, 2.0f);
    CHECK (! host.processor->currentValuesMatchLoadedPreset());

    // user preset load: identity by name, index -1, values applied, unedited
    presets::Preset user;
    user.kind = presets::Kind::user; user.name = "My Kick";
    user.params.startHz = 333.0f; user.params.sweepSec = 0.09f; user.params.gain = 1.5f;
    host.processor->loadPreset (user);
    loaded = host.processor->getLoadedPreset();
    CHECK (loaded.kind == presets::Kind::user && loaded.name == "My Kick" && loaded.factoryIndex == -1);
    CHECK (host.processor->getPresetIndex() == -1);
    CHECK_NEAR (host.getDisplay (params::startFreq), 333.0f, 0.05f);
    CHECK_NEAR (host.getDisplay (params::sweep), 90.0f, 1e-3f);
    CHECK (host.processor->currentValuesMatchLoadedPreset());
    host.setDisplay (params::hold, 500.0f);
    CHECK (! host.processor->currentValuesMatchLoadedPreset());

    // adopt (Save as "Other"): identity changes, parameters untouched, reference = current values
    presets::Preset other; other.kind = presets::Kind::user; other.name = "Other";
    host.processor->adoptPresetIdentity (other);
    CHECK (host.processor->getLoadedPreset().name == "Other" && host.processor->currentValuesMatchLoadedPreset());
    CHECK_NEAR (host.getDisplay (params::hold), 500.0f, 1e-3f);

    // state schema 3: user identity by name survives save/restore, values restored, shown unedited
    juce::MemoryBlock state;
    host.processor->getStateInformation (state);
    {
        std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()));
        REQUIRE (xml != nullptr);
        CHECK (xml->getIntAttribute ("version") == 4);
        auto* preset = xml->getChildByName ("Preset");
        REQUIRE (preset != nullptr);
        CHECK (preset->getStringAttribute ("kind") == "user" && preset->getStringAttribute ("name") == "Other" && preset->getIntAttribute ("index") == -1);
    }
    Host restored;
    restored.processor->setStateInformation (state.getData(), (int) state.getSize());
    loaded = restored.processor->getLoadedPreset();
    CHECK (loaded.kind == presets::Kind::user && loaded.name == "Other" && loaded.factoryIndex == -1);
    CHECK_NEAR (restored.getDisplay (params::hold), 500.0f, 1e-3f);
    CHECK (restored.processor->currentValuesMatchLoadedPreset());

    // factory identity by name
    host.processor->loadFactoryPreset (7);
    host.processor->getStateInformation (state);
    Host gab;
    gab.processor->setStateInformation (state.getData(), (int) state.getSize());
    loaded = gab.processor->getLoadedPreset();
    CHECK (loaded.kind == presets::Kind::factory && loaded.name == "Gabber" && loaded.factoryIndex == 7);
    CHECK (gab.processor->currentValuesMatchPreset (7) && gab.processor->currentValuesMatchLoadedPreset());

    // schema 2 document (index only) -> factory by index; unknown factory name -> index fallback
    for (int version : { 2, 3 })
    {
        Host old;
        juce::XmlElement doc ("KickCrafter");
        doc.setAttribute ("version", version);
        auto* p = doc.createNewChildElement ("Preset");
        p->setAttribute ("index", 5);
        if (version == 3) { p->setAttribute ("kind", "factory"); p->setAttribute ("name", "No Such Preset"); }
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (doc, block);
        old.processor->setStateInformation (block.getData(), (int) block.getSize());
        loaded = old.processor->getLoadedPreset();
        CHECK_MSG (loaded.kind == presets::Kind::factory && loaded.factoryIndex == 5 && loaded.name == "Square Growl", "version " + std::to_string (version));
    }
    // user kind with an invalid name is ignored (falls back to factory 0)
    {
        Host bad;
        juce::XmlElement doc ("KickCrafter");
        doc.setAttribute ("version", 3);
        auto* p = doc.createNewChildElement ("Preset");
        p->setAttribute ("index", -1); p->setAttribute ("kind", "user"); p->setAttribute ("name", "");
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (doc, block);
        bad.processor->setStateInformation (block.getData(), (int) block.getSize());
        CHECK (bad.processor->getLoadedPreset().kind == presets::Kind::factory && bad.processor->getPresetIndex() == 0);
    }
}

TEST_CASE ("top bar preset actions: save as / save / rename / delete / prev-next across sections / menu content (temporary library)")
{
    REQUIRE (juce::Desktop::getInstance().getDisplays().displays.size() > 0);
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); };
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kcf-topbar-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    const char* previousDir = std::getenv ("KCF_PRESET_DIR");                  // the process-wide isolation dir (main)
    struct Cleanup { juce::File d; juce::String previous; ~Cleanup() { d.deleteRecursively();
        if (previous.isNotEmpty()) setenv ("KCF_PRESET_DIR", previous.toRawUTF8(), 1); else unsetenv ("KCF_PRESET_DIR"); } }
        cleanup { dir, previousDir != nullptr ? juce::String (previousDir) : juce::String() };
    setenv ("KCF_PRESET_DIR", dir.getFullPathName().toRawUTF8(), 1);

    Host host;
    std::unique_ptr<juce::AudioProcessorEditor> editor (host.processor->createEditor());
    auto* kc = dynamic_cast<KickCrafterEditor*> (editor.get());
    REQUIRE (kc != nullptr);
    editor->setVisible (true); editor->addToDesktop (0); pump();
    auto& bar = kc->getTopBar();
    auto text = [&] { kc->pollNow(); return bar.getPresetBox().getText(); };
    const juce::String edited = juce::String::fromUTF8 ("  \xe2\x80\xa2 edited");
    CHECK (bar.getLibrary().directory() == dir);
    CHECK (text() == "Reference");
    { juce::PopupMenu m; bar.buildPresetMenu (m); CHECK (m.getNumItems() == 9); }          // "Factory" header + 8

    // Save as: file created, identity = user preset, unedited
    host.setDisplay (params::sweep, 90.0f);
    CHECK (text() == "Reference" + edited);
    REQUIRE (bar.saveCurrentAs ("Test A"));
    REQUIRE (bar.getLibrary().find ("Test A") != nullptr);
    CHECK (bar.getLibrary().find ("Test A")->file.existsAsFile());
    CHECK (host.processor->getLoadedPreset().kind == presets::Kind::user && host.processor->getPresetIndex() == -1);
    CHECK (text() == "Test A");
    { juce::PopupMenu m; bar.buildPresetMenu (m); CHECK (m.getNumItems() == 11); }         // + "User" header + 1
    // Save: overwrites the loaded user preset with the current values
    host.setDisplay (params::hold, 400.0f);
    CHECK (text() == "Test A" + edited);
    REQUIRE (bar.saveCurrent());
    CHECK (text() == "Test A");
    CHECK_NEAR (bar.getLibrary().find ("Test A")->params.holdSec, 0.4f, 1e-6f);
    CHECK (! bar.saveCurrentAs ("") && bar.getLastError().isNotEmpty());
    CHECK (! bar.saveCurrentAs ("a/b"));

    // Prev/next walk the combined list: factory bank, then user presets by name, wrapping
    REQUIRE (bar.saveCurrentAs ("Test B"));
    REQUIRE (bar.selectPresetByName (presets::Kind::factory, "Gabber"));
    CHECK (host.processor->getPresetIndex() == 7);
    bar.selectNextPreset();     CHECK (host.processor->getLoadedPreset().name == "Test A");
    bar.selectNextPreset();     CHECK (host.processor->getLoadedPreset().name == "Test B");
    bar.selectNextPreset();     CHECK (host.processor->getPresetIndex() == 0);
    bar.selectPreviousPreset(); CHECK (host.processor->getLoadedPreset().name == "Test B");
    CHECK (! bar.selectPresetByName (presets::Kind::user, "Nope"));

    // Rename keeps the values and the edited state, refuses collisions
    host.setDisplay (params::drive, 3.0f);
    CHECK (text() == "Test B" + edited);
    CHECK (! bar.renameCurrent ("Test A") && ! bar.renameCurrent (""));
    REQUIRE (bar.renameCurrent ("Test C"));
    CHECK (bar.getLibrary().find ("Test B") == nullptr && bar.getLibrary().find ("Test C") != nullptr);
    CHECK (host.processor->getLoadedPreset().name == "Test C");
    CHECK (text() == "Test C" + edited);
    CHECK_NEAR (host.getDisplay (params::drive), 3.0f, 1e-4f);

    // Delete: file removed, identity stays and shows "(missing)", values untouched
    REQUIRE (bar.deleteCurrent());
    CHECK (bar.getLibrary().find ("Test C") == nullptr);
    CHECK (text() == "Test C (missing)" + edited);
    CHECK_NEAR (host.getDisplay (params::drive), 3.0f, 1e-4f);
    CHECK (! bar.deleteCurrent() && ! bar.saveCurrent() && ! bar.renameCurrent ("x"));
    // next from a missing user preset goes to the first user preset, prev to the last factory one
    bar.selectNextPreset();     CHECK (host.processor->getLoadedPreset().name == "Test A");
    REQUIRE (bar.deleteCurrent());
    bar.selectPreviousPreset(); CHECK (host.processor->getPresetIndex() == 7);

    // Factory presets are read-only for the actions
    CHECK (! bar.saveCurrent() && ! bar.renameCurrent ("x") && ! bar.deleteCurrent());
    CHECK (bar.getLastError().isNotEmpty());

    // Picking the already selected user item reloads it (popup id path)
    REQUIRE (bar.saveCurrentAs ("Test D"));
    host.setDisplay (params::drive, 2.5f);
    CHECK (text() == "Test D" + edited);
    auto* box = dynamic_cast<kcf::ui::PresetBox*> (&bar.getPresetBox());
    REQUIRE (box != nullptr && box->onPick);
    box->onPick (kcf::ui::TopBar::userItemBase);                 // the first (only) user item
    CHECK (text() == "Test D");
    // Rescan sees files added by hand
    dir.getChildFile ("Hand.xml").replaceWithText (presets::toXml ("Hand made", KickParams {}));
    bar.rescanLibrary();
    CHECK (bar.getLibrary().find ("Hand made") != nullptr);
    { juce::PopupMenu m; bar.buildPresetMenu (m); CHECK (m.getNumItems() == 12); }         // header + 8 + header + 2

    editor.reset();
    pump();
}

// Finds the (single) AlertWindow with that title among the desktop windows, or nullptr.
static juce::AlertWindow* findAlertWindow (const juce::String& title)
{
    auto& desktop = juce::Desktop::getInstance();
    juce::AlertWindow* found = nullptr;
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* w = dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)))
            if (w->getName() == title) { CHECK (found == nullptr); found = w; }
    return found;
}

TEST_CASE ("top bar actions menu and dialogs: Save on a factory preset prompts (prefilled), Save as on an existing name confirms, Rename/Delete dialogs, Cancel/No, skipped-file feedback, editor closed while a dialog is open")
{
    REQUIRE (juce::Desktop::getInstance().getDisplays().displays.size() > 0);
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (80); };
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kcf-dialogs-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    const char* previousDir = std::getenv ("KCF_PRESET_DIR");
    struct Cleanup { juce::File d; juce::String previous; ~Cleanup() { d.deleteRecursively();
        if (previous.isNotEmpty()) setenv ("KCF_PRESET_DIR", previous.toRawUTF8(), 1); else unsetenv ("KCF_PRESET_DIR"); } }
        cleanup { dir, previousDir != nullptr ? juce::String (previousDir) : juce::String() };
    setenv ("KCF_PRESET_DIR", dir.getFullPathName().toRawUTF8(), 1);
    using TB = kcf::ui::TopBar;

    Host host;
    std::unique_ptr<juce::AudioProcessorEditor> editor (host.processor->createEditor());
    auto* kc = dynamic_cast<KickCrafterEditor*> (editor.get());
    REQUIRE (kc != nullptr);
    editor->setVisible (true); editor->addToDesktop (0); pump();
    auto* bar = &kc->getTopBar();
    auto text = [&] { kc->pollNow(); return bar->getPresetBox().getText(); };
    const juce::String edited = juce::String::fromUTF8 ("  \xe2\x80\xa2 edited");
    REQUIRE (bar->getLibrary().directory() == dir);
    REQUIRE (findAlertWindow ("Save preset as") == nullptr && bar->getOpenDialog() == nullptr);

    // Save on a factory preset = Save as, prefilled with the factory name; Cancel does nothing
    host.setDisplay (params::sweep, 90.0f);
    bar->performPresetAction (TB::actionSave);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    CHECK (bar->getOpenDialog()->getName() == "Save preset as");
    CHECK (bar->getOpenDialog()->getTextEditorContents ("name") == "Reference");
    CHECK (bar->getOpenDialog()->isCurrentlyModal());
    if (auto* ed = bar->getOpenDialog()->getTextEditor ("name"))
        CHECK (ed->getHighlightedRegion().getLength() == juce::String ("Reference").length());   // typing replaces the name
    bar->getOpenDialog()->triggerButtonClick ("Cancel");
    pump();
    CHECK (bar->getOpenDialog() == nullptr);
    CHECK (bar->getLibrary().presets().empty());
    CHECK (text() == "Reference" + edited);
    // ... and OK saves under the typed name: identity becomes that user preset
    bar->performPresetAction (TB::actionSave);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    bar->getOpenDialog()->getTextEditor ("name")->setText ("Dlg A", false);
    bar->getOpenDialog()->triggerButtonClick ("OK");
    pump();
    CHECK (bar->getOpenDialog() == nullptr);
    REQUIRE (bar->getLibrary().find ("Dlg A") != nullptr);
    CHECK_NEAR (bar->getLibrary().find ("Dlg A")->params.sweepSec, 0.09f, 1e-6f);
    CHECK (host.processor->getLoadedPreset().kind == presets::Kind::user && host.processor->getLoadedPreset().name == "Dlg A");
    CHECK (text() == "Dlg A");
    // Save on a user preset overwrites silently (no dialog)
    host.setDisplay (params::hold, 400.0f);
    bar->performPresetAction (TB::actionSave);
    pump();
    CHECK (bar->getOpenDialog() == nullptr && findAlertWindow ("Overwrite preset") == nullptr);
    CHECK_NEAR (bar->getLibrary().find ("Dlg A")->params.holdSec, 0.4f, 1e-6f);
    CHECK (text() == "Dlg A");

    // Save as with an existing name asks for confirmation: No keeps the file, Yes overwrites
    host.setDisplay (params::hold, 500.0f);
    bar->performPresetAction (TB::actionSaveAs);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    CHECK (bar->getOpenDialog()->getTextEditorContents ("name") == "Dlg A");                    // prefilled with the loaded name
    bar->getOpenDialog()->triggerButtonClick ("OK");
    pump();
    auto* box = findAlertWindow ("Overwrite preset");
    REQUIRE (box != nullptr);
    box->triggerButtonClick ("No");
    pump();
    CHECK (findAlertWindow ("Overwrite preset") == nullptr);
    CHECK_NEAR (bar->getLibrary().find ("Dlg A")->params.holdSec, 0.4f, 1e-6f);                  // unchanged
    CHECK (text() == "Dlg A" + edited);
    bar->performPresetAction (TB::actionSaveAs);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    bar->getOpenDialog()->triggerButtonClick ("OK");
    pump();
    box = findAlertWindow ("Overwrite preset");
    REQUIRE (box != nullptr);
    box->triggerButtonClick ("Yes");
    pump();
    CHECK (findAlertWindow ("Overwrite preset") == nullptr);
    CHECK_NEAR (bar->getLibrary().find ("Dlg A")->params.holdSec, 0.5f, 1e-6f);
    CHECK (text() == "Dlg A");
    // Save as with a new name creates it without confirmation
    bar->performPresetAction (TB::actionSaveAs);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    bar->getOpenDialog()->getTextEditor ("name")->setText ("Dlg B", false);
    bar->getOpenDialog()->triggerButtonClick ("OK");
    pump();
    CHECK (findAlertWindow ("Overwrite preset") == nullptr);
    CHECK (bar->getLibrary().find ("Dlg B") != nullptr && host.processor->getLoadedPreset().name == "Dlg B");
    // An invalid name reports an error box
    bar->performPresetAction (TB::actionSaveAs);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    bar->getOpenDialog()->getTextEditor ("name")->setText ("bad/name", false);
    bar->getOpenDialog()->triggerButtonClick ("OK");
    pump();
    auto* err = findAlertWindow ("Preset");
    REQUIRE (err != nullptr);
    err->triggerButtonClick ("OK");
    pump();
    CHECK (findAlertWindow ("Preset") == nullptr);
    CHECK (bar->getLibrary().presets().size() == 2);

    // Rename: prompt prefilled with the current name; collision -> error box; new name applied
    bar->performPresetAction (TB::actionRename);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    CHECK (bar->getOpenDialog()->getName() == "Rename preset");
    CHECK (bar->getOpenDialog()->getTextEditorContents ("name") == "Dlg B");
    bar->getOpenDialog()->getTextEditor ("name")->setText ("Dlg A", false);
    bar->getOpenDialog()->triggerButtonClick ("OK");
    pump();
    err = findAlertWindow ("Preset");
    REQUIRE (err != nullptr);
    err->triggerButtonClick ("OK");
    pump();
    CHECK (host.processor->getLoadedPreset().name == "Dlg B");
    bar->performPresetAction (TB::actionRename);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    bar->getOpenDialog()->getTextEditor ("name")->setText ("Dlg C", false);
    bar->getOpenDialog()->triggerButtonClick ("OK");
    pump();
    CHECK (bar->getLibrary().find ("Dlg B") == nullptr && bar->getLibrary().find ("Dlg C") != nullptr);
    CHECK (text() == "Dlg C");
    // A second prompt replaces the first: the stale callback of the destroyed prompt (which
    // may be re-allocated at the same address) must not close the new one
    bar->performPresetAction (TB::actionRename);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr && bar->getOpenDialog()->getName() == "Rename preset");
    bar->performPresetAction (TB::actionSaveAs);
    pump(); pump();
    REQUIRE (bar->getOpenDialog() != nullptr);
    CHECK (bar->getOpenDialog()->getName() == "Save preset as");
    CHECK (findAlertWindow ("Rename preset") == nullptr);
    bar->getOpenDialog()->triggerButtonClick ("Cancel");
    pump();
    CHECK (bar->getOpenDialog() == nullptr);

    // Delete: No keeps the file, Yes removes it and the name shows "(missing)"
    bar->performPresetAction (TB::actionDelete);
    pump();
    box = findAlertWindow ("Delete preset");
    REQUIRE (box != nullptr);
    box->triggerButtonClick ("No");
    pump();
    CHECK (bar->getLibrary().find ("Dlg C") != nullptr);
    bar->performPresetAction (TB::actionDelete);
    pump();
    box = findAlertWindow ("Delete preset");
    REQUIRE (box != nullptr);
    box->triggerButtonClick ("Yes");
    pump();
    CHECK (bar->getLibrary().find ("Dlg C") == nullptr);
    CHECK (text() == "Dlg C (missing)");
    // Rename / Delete on a factory (or missing) preset only report
    bar->performPresetAction (TB::actionDelete);
    pump();
    err = findAlertWindow ("Preset");
    REQUIRE (err != nullptr && bar->getOpenDialog() == nullptr);
    err->triggerButtonClick ("OK");
    pump();

    // Skipped files: the popup shows a disabled row, Rescan reports the reasons
    dir.getChildFile ("Broken.xml").replaceWithText ("<KickCrafterPreset version=\"3\" name=\"Broken\"><Params startFreq=\"abc\"/></KickCrafterPreset>");
    bar->performPresetAction (TB::actionRescan);
    pump();
    CHECK (bar->getLibrary().skippedFiles() == 1);
    err = findAlertWindow ("Preset");
    REQUIRE (err != nullptr);
    err->triggerButtonClick ("OK");
    pump();
    {
        juce::PopupMenu m; bar->buildPresetMenu (m);
        CHECK (m.getNumItems() == 12);                                  // header + 8 + "User" header + Dlg A + skipped row
        juce::PopupMenu::MenuItemIterator it (m);
        bool sawSkipped = false;
        while (it.next()) if (it.getItem().itemID == TB::skippedInfoItemId) { sawSkipped = true; CHECK (! it.getItem().isEnabled); }
        CHECK (sawSkipped);
    }
    dir.getChildFile ("Broken.xml").deleteFile();
    bar->rescanLibrary();
    { juce::PopupMenu m; bar->buildPresetMenu (m); CHECK (m.getNumItems() == 11); }

    // Closing the editor while dialogs are open: the prompt and the confirmation vanish and
    // nothing runs afterwards (the P1 of review 027). Both kinds, in turn.
    REQUIRE (bar->selectPresetByName (presets::Kind::user, "Dlg A"));
    bar->performPresetAction (TB::actionDelete);
    pump();
    REQUIRE (findAlertWindow ("Delete preset") != nullptr);
    editor.reset(); bar = nullptr;
    pump(); pump();
    CHECK (findAlertWindow ("Delete preset") == nullptr);
    CHECK (juce::File (dir).getChildFile ("Dlg A.xml").existsAsFile());       // the pending Yes never ran
    editor.reset (host.processor->createEditor());
    kc = dynamic_cast<KickCrafterEditor*> (editor.get());
    REQUIRE (kc != nullptr);
    editor->setVisible (true); editor->addToDesktop (0); pump();
    bar = &kc->getTopBar();
    bar->performPresetAction (TB::actionSaveAs);
    pump();
    REQUIRE (bar->getOpenDialog() != nullptr && findAlertWindow ("Save preset as") != nullptr);
    editor.reset(); bar = nullptr;
    pump(); pump();
    CHECK (findAlertWindow ("Save preset as") == nullptr);
    CHECK (juce::File (dir).findChildFiles (juce::File::findFiles, false, "*.xml").size() == 1);
}

TEST_CASE ("editor: default and restored sizes, first-open handle geometry, host refresh without gestures, drags with gestures, resize, closed == open audio")
{
    // Requires a display (run under Xvfb). Deliberately fails instead of skipping.
    REQUIRE (juce::Desktop::getInstance().getDisplays().displays.size() > 0);
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (150); };
    auto makeEvent = [] (juce::Component& target, juce::Point<float> pos, bool down)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos,
                                 down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, juce::Time::getCurrentTime(), pos,
                                 juce::Time::getCurrentTime(), 1, false);
    };

    // 1. Fresh instance opens at 100 % = 1000 x 640.
    Host closed, open;
    std::unique_ptr<juce::AudioProcessorEditor> editor (open.processor->createEditor());
    auto* kc = dynamic_cast<KickCrafterEditor*> (editor.get());
    REQUIRE (kc != nullptr);
    CHECK (editor->getWidth() == KickCrafterEditor::logicalWidth && editor->getHeight() == KickCrafterEditor::logicalHeight);
    CHECK (open.processor->getUIScalePercent() == 100);
    editor->setVisible (true);
    editor->addToDesktop (0);
    pump();
    CHECK (editor->getWidth() == KickCrafterEditor::logicalWidth && editor->getHeight() == KickCrafterEditor::logicalHeight);

    // 2. First open, before any parameter/MIDI event: handles sit on their curves and are hittable.
    {
        auto& graph = kc->getPitchGraph();
        const auto handles = graph.getHandles();
        REQUIRE (handles.size() == 3);
        const auto plot = graph.plotArea();
        CHECK (plot.getWidth() > 100.0f && plot.getHeight() > 50.0f);
        for (const auto& h : handles)
            CHECK_MSG (plot.expanded (2.0f).contains (h.position), "handle " + h.id.toStdString() + " outside the plot");
        CHECK_NEAR (handles[0].position.x, plot.getX(), 1.0f);                                 // start handle at t = 0
        CHECK_NEAR (handles[1].position.x, graph.xForSeconds (0.047), 1.0f);                   // knee at the sweep time
        CHECK (handles[0].position.y < handles[1].position.y);                                  // 250 Hz above 55 Hz
        auto& amp = kc->getAmplitudeGraph();
        const auto ah = amp.getHandles();
        REQUIRE (ah.size() == 3);
        CHECK_NEAR (ah[2].position.x, amp.xForSeconds (0.179), 1.0f);                         // hit end
        CHECK_NEAR (ah[1].position.x, amp.xForSeconds (0.179 - 0.0945), 1.0f);                // fade start
        // Pointer hit: pressing exactly on the knee handle and dragging changes the sweep time.
        const float sweepBefore = open.getDisplay (params::sweep);
        graph.mouseDown (makeEvent (graph, handles[1].position, true));
        graph.mouseDrag (makeEvent (graph, handles[1].position.translated (60.0f, 0.0f), true));
        graph.mouseUp (makeEvent (graph, handles[1].position.translated (60.0f, 0.0f), false));
        CHECK (open.getDisplay (params::sweep) > sweepBefore + 10.0f);
        open.setDisplay (params::sweep, 47.0f);
        kc->pollNow();
    }

    // 3. Editor open vs closed: identical audio.
    const std::vector<MidiEv> events { noteOn (0, 33, 127), noteOn (6000, 45, 90) };
    const auto a = closed.render (16000, events);
    const auto b = open.render (16000, events);
    CHECK (maxAbsDiff (a, b) == 0.0);
    pump();

    // 4. Host-driven refresh: UI follows, no gestures, preview follows.
    GestureSpy spy;
    open.processor->addListener (&spy);
    open.setDisplay (params::startFreq, 400.0f);
    kc->pollNow();
    pump();
    CHECK (spy.begins.empty() && spy.ends.empty());
    CHECK_NEAR (kc->findKnob (params::startFreq)->getSlider().getValue(), 400.0, 0.5);
    CHECK_NEAR (kc->getPreviewModel().params.startHz, 400.0f, 0.5f);
    CHECK (kc->findKnob (params::startFreq)->getValueLabel().getText() == "400.0 Hz");

    // 5. Native knob edit: drag start / value / drag end -> begin / change / end.
    auto& slider = kc->findKnob (params::sweep)->getSlider();
    const auto centre = slider.getLocalBounds().getCentre().toFloat();
    slider.mouseDown (makeEvent (slider, centre, true));
    slider.mouseDrag (makeEvent (slider, centre.translated (0.0f, -50.0f), true));
    slider.mouseUp (makeEvent (slider, centre.translated (0.0f, -50.0f), false));
    pump();
    const int sweepIndex = open.param (params::sweep).getParameterIndex();
    CHECK (std::count (spy.begins.begin(), spy.begins.end(), sweepIndex) == 1);
    CHECK (std::count (spy.ends.begin(), spy.ends.end(), sweepIndex) == 1);
    CHECK (open.getDisplay (params::sweep) > 47.5f);
    CHECK (std::any_of (spy.changes.begin(), spy.changes.end(), [&] (auto& c) { return c.first == sweepIndex; }));
    // Text entry: valid and invalid.
    kc->findKnob (params::sweep)->getValueLabel().setText ("120 ms", juce::sendNotificationSync);
    CHECK_NEAR (open.getDisplay (params::sweep), 120.0f, 1e-3);
    kc->findKnob (params::sweep)->getValueLabel().setText ("nonsense", juce::sendNotificationSync);
    CHECK_NEAR (open.getDisplay (params::sweep), 120.0f, 1e-3);
    kc->findKnob (params::startFreq)->getValueLabel().setText ("A1", juce::sendNotificationSync);
    CHECK_NEAR (open.getDisplay (params::startFreq), 55.0f, 0.01f);

    // 6. Graph handle drag: start-frequency handle upwards -> gesture + higher value.
    spy.begins.clear(); spy.ends.clear();
    kc->pollNow();
    auto& graph = kc->getPitchGraph();
    const auto startHandle = graph.getHandles()[0];
    CHECK (startHandle.id == params::startFreq);
    const float before = open.getDisplay (params::startFreq);
    graph.mouseDown (makeEvent (graph, startHandle.position, true));
    graph.mouseDrag (makeEvent (graph, startHandle.position.translated (0.0f, -40.0f), true));
    graph.mouseUp (makeEvent (graph, startHandle.position.translated (0.0f, -40.0f), false));
    pump();
    const int startIndex = open.param (params::startFreq).getParameterIndex();
    CHECK (std::count (spy.begins.begin(), spy.begins.end(), startIndex) == 1);
    CHECK (std::count (spy.ends.begin(), spy.ends.end(), startIndex) == 1);
    CHECK (open.getDisplay (params::startFreq) > before * 1.2f);
    // Host refresh during a LIVE drag: begin + value first, refresh of another
    // parameter while the gesture is active, then exactly one matching end and no
    // extra begin/value caused by the refresh.
    spy.begins.clear(); spy.ends.clear(); spy.changes.clear();
    kc->pollNow();                                                  // handles relaid after the previous drag
    const auto liveHandle = graph.getHandles()[0].position;
    graph.mouseDown (makeEvent (graph, liveHandle, true));
    graph.mouseDrag (makeEvent (graph, liveHandle.translated (0.0f, 8.0f), true));   // gesture begins on the first drag
    CHECK (spy.begins.size() == 1 && spy.begins[0] == startIndex);
    const size_t changesBeforeRefresh = spy.changes.size();
    CHECK (changesBeforeRefresh >= 1 && spy.changes.back().first == startIndex);
    open.setDisplay (params::shape, 40.0f);                          // host refresh while the mouse is down
    kc->pollNow();
    CHECK (spy.ends.empty());
    CHECK (spy.begins.size() == 1);
    CHECK (std::count_if (spy.changes.begin(), spy.changes.end(), [&] (auto& c) { return c.first == startIndex; }) == (long) changesBeforeRefresh);
    CHECK_NEAR (kc->getPreviewModel().params.morph, 0.4f, 1e-4f);    // refresh applied, drag untouched
    graph.mouseUp (makeEvent (graph, liveHandle.translated (0.0f, 8.0f), false));
    CHECK (spy.ends.size() == 1 && spy.ends[0] == startIndex);
    open.processor->removeListener (&spy);

    // 6b. Click without drag: hit-test only, no parameter gesture. The spy is
    // registered (positive control: a real drag right after is observed).
    open.processor->addListener (&spy);
    spy.begins.clear(); spy.ends.clear(); spy.changes.clear();
    kc->pollNow();
    graph.mouseDown (makeEvent (graph, graph.getHandles()[0].position, true));
    graph.mouseUp (makeEvent (graph, graph.getHandles()[0].position, false));
    CHECK (spy.begins.empty() && spy.ends.empty() && spy.changes.empty());
    graph.mouseDown (makeEvent (graph, graph.getHandles()[0].position, true));
    graph.mouseDrag (makeEvent (graph, graph.getHandles()[0].position.translated (0.0f, -6.0f), true));
    graph.mouseUp (makeEvent (graph, graph.getHandles()[0].position.translated (0.0f, -6.0f), false));
    CHECK (spy.begins.size() == 1 && spy.ends.size() == 1);   // the spy is live

    // 6b'. Velocity switch (v1.3): one button, click toggles the parameter as a complete gesture,
    // host changes refresh its state and text without gestures; every knob dial has one size.
    {
        auto* velocityButton = dynamic_cast<juce::Button*> (kc->findChildWithID ("velocityButton"));
        if (velocityButton == nullptr)
            for (auto* child : kc->getChildren())
                for (auto* grandChild : child->getChildren())
                    if (grandChild->getComponentID() == "velocityButton") velocityButton = dynamic_cast<juce::Button*> (grandChild);
        REQUIRE (velocityButton != nullptr);
        CHECK (kc->findKnob (params::velocity) == nullptr);                        // no knob any more
        {   // geometry: last grid row, under the first knob column, on the same row as the pitch-source buttons
            auto* fixedBtn = dynamic_cast<juce::Button*> (kc->findChildWithID ("pitchFixed"));
            if (fixedBtn == nullptr)
                for (auto* child : kc->getChildren())
                    for (auto* grandChild : child->getChildren())
                        if (grandChild->getComponentID() == "pitchFixed") fixedBtn = dynamic_cast<juce::Button*> (grandChild);
            auto* curveKnob = kc->findKnob (params::curve);
            REQUIRE (fixedBtn != nullptr && curveKnob != nullptr);
            CHECK (velocityButton->getY() >= curveKnob->getBottom());
            CHECK (velocityButton->getRight() <= fixedBtn->getX());
            CHECK (velocityButton->getY() == fixedBtn->getY() && velocityButton->getHeight() == fixedBtn->getHeight());
            CHECK (velocityButton->getX() >= curveKnob->getX() && velocityButton->getRight() <= curveKnob->getRight());
            CHECK (velocityButton->getTitle() == "Velocity sensitivity");
        }
        const int velocityIndex = open.param (params::velocity).getParameterIndex();
        CHECK (! velocityButton->getToggleState() && velocityButton->getButtonText() == "Off");   // v1.5: Off by default
        spy.begins.clear(); spy.ends.clear();
        velocityButton->triggerClick();
        pump();
        kc->pollNow();
        CHECK (open.getDisplay (params::velocity) == 1.0f);
        CHECK (velocityButton->getToggleState() && velocityButton->getButtonText() == "On");
        CHECK (std::count (spy.begins.begin(), spy.begins.end(), velocityIndex) == 1 && std::count (spy.ends.begin(), spy.ends.end(), velocityIndex) == 1);
        velocityButton->triggerClick();
        pump();
        kc->pollNow();
        CHECK (open.getDisplay (params::velocity) == 0.0f && ! velocityButton->getToggleState());
        spy.begins.clear(); spy.ends.clear();
        open.setDisplay (params::velocity, 1.0f);                                  // host refresh: no gesture
        kc->pollNow();
        CHECK (velocityButton->getToggleState() && velocityButton->getButtonText() == "On");
        CHECK (spy.begins.empty() && spy.ends.empty());
        open.setDisplay (params::velocity, 0.0f);
        kc->pollNow();
        CHECK (! velocityButton->getToggleState());
        // uniform knob size: every dial is the same square, frequency knobs included (v1.3, user request)
        int dial = -1;
        for (auto* id : { params::startFreq, params::endFreq, params::sweep, params::hold, params::fade, params::attack, params::curve, params::shape, params::drive })
        {
            auto* knob = kc->findKnob (id);
            REQUIRE (knob != nullptr);
            const auto b = knob->getSlider().getBounds();
            const int d = std::min (b.getWidth(), b.getHeight());                 // the dial itself is min(w,h)
            if (dial < 0) dial = d;
            CHECK_MSG (d == dial, std::string (id) + " dial size differs");
        }
    }

    // 6c. Pitch-source buttons: a native click commits the value and the buttons show it.
    const int pitchIndex = open.param (params::pitchSource).getParameterIndex();
    auto* fixedButton = dynamic_cast<juce::Button*> (kc->findChildWithID ("pitchFixed"));
    auto* midiButton = dynamic_cast<juce::Button*> (kc->findChildWithID ("pitchMidi"));
    if (fixedButton == nullptr || midiButton == nullptr)
        for (auto* child : kc->getChildren())
            for (auto* grandChild : child->getChildren())
            {
                if (grandChild->getComponentID() == "pitchFixed") fixedButton = dynamic_cast<juce::Button*> (grandChild);
                if (grandChild->getComponentID() == "pitchMidi") midiButton = dynamic_cast<juce::Button*> (grandChild);
            }
    REQUIRE (fixedButton != nullptr && midiButton != nullptr);
    CHECK (fixedButton->getToggleState() && ! midiButton->getToggleState());   // v1.1 default: Fixed
    CHECK (fixedButton->getX() < midiButton->getX());                          // and listed first
    spy.begins.clear(); spy.ends.clear();
    midiButton->triggerClick();
    pump();
    kc->pollNow();
    CHECK_NEAR (open.getDisplay (params::pitchSource), 1.0f, 1e-6);
    CHECK (midiButton->getToggleState() && ! fixedButton->getToggleState());
    CHECK (std::count (spy.begins.begin(), spy.begins.end(), pitchIndex) == 1 && std::count (spy.ends.begin(), spy.ends.end(), pitchIndex) == 1);
    fixedButton->triggerClick();
    pump();
    kc->pollNow();
    CHECK_NEAR (open.getDisplay (params::pitchSource), 0.0f, 1e-6);
    CHECK (fixedButton->getToggleState() && ! midiButton->getToggleState());
    CHECK (std::count (spy.begins.begin(), spy.begins.end(), pitchIndex) == 2 && std::count (spy.ends.begin(), spy.ends.end(), pitchIndex) == 2);
    // Host refresh of the same parameter: buttons follow, no gesture.
    open.setDisplay (params::pitchSource, 1.0f);
    kc->pollNow();
    CHECK (midiButton->getToggleState() && ! fixedButton->getToggleState());
    CHECK (std::count (spy.begins.begin(), spy.begins.end(), pitchIndex) == 2);
    open.setDisplay (params::pitchSource, 0.0f);
    kc->pollNow();
    open.processor->removeListener (&spy);

    // 7. Resize to a second scale: state records it; handles follow the new geometry.
    editor->setSize (1250, 800);
    pump();
    CHECK (open.processor->getUIScalePercent() == 125);
    CHECK_NEAR (kc->getScale(), 1.25f, 1e-3);
    CHECK_NEAR (graph.getHandles()[0].position.x, graph.plotArea().getX(), 1.0f);

    // 8. Close and reopen: the restored scale is honoured, not the minimum.
    editor->removeFromDesktop();
    editor.reset();
    pump();
    for (int wanted : { 80, 100, 125, 150 })
    {
        open.processor->setUIScalePercent (wanted);
        std::unique_ptr<juce::AudioProcessorEditor> again (open.processor->createEditor());
        CHECK_MSG (again->getWidth() == KickCrafterEditor::logicalWidth * wanted / 100, "reopen at " + std::to_string (wanted));
        CHECK (open.processor->getUIScalePercent() == wanted);
        again.reset();
    }
    // Restored through state as well.
    open.processor->setUIScalePercent (125);
    juce::MemoryBlock block;
    open.processor->getStateInformation (block);
    Host restored;
    restored.processor->setStateInformation (block.getData(), (int) block.getSize());
    std::unique_ptr<juce::AudioProcessorEditor> restoredEditor (restored.processor->createEditor());
    CHECK (restoredEditor->getWidth() == 1250 && restoredEditor->getHeight() == 800);
    restoredEditor.reset();
    // Audio after the editor is gone is still the same engine.
    const auto c = open.render (8000, { noteOn (0, 33, 127) });
    CHECK (allFinite (c) && peakAbs (c) > 0.1f);
}

// `kcf_plugin_tests --layout [percent]` prints the editor size and the positions (editor
// pixels) of every knob and graph handle: used by the REAPER GUI driver.
static int printLayout (int percent)
{
    KickCrafterProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    processor.setUIScalePercent (percent);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* kc = dynamic_cast<KickCrafterEditor*> (editor.get());
    if (kc == nullptr) return 2;
    std::printf ("editor %d %d scale %.3f\n", editor->getWidth(), editor->getHeight(), kc->getScale());
    editor->setVisible (true);
    editor->addToDesktop (0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    std::printf ("editor-after-desktop %d %d scale %.3f\n", editor->getWidth(), editor->getHeight(), kc->getScale());
    auto toEditor = [&] (juce::Component& c, juce::Point<float> local) { return editor->getLocalPoint (&c, local); };
    for (auto* id : params::allIds)
        if (auto* knob = kc->findKnob (id))
        {
            const auto p = toEditor (knob->getSlider(), knob->getSlider().getLocalBounds().getCentre().toFloat());
            std::printf ("knob %s %.1f %.1f\n", id, p.x, p.y);
        }
    for (auto& h : kc->getPitchGraph().getHandles())
    {
        const auto p = toEditor (kc->getPitchGraph(), h.position);
        std::printf ("handle pitch %s %.1f %.1f\n", h.id.toRawUTF8(), p.x, p.y);
    }
    for (auto& h : kc->getAmplitudeGraph().getHandles())
    {
        const auto p = toEditor (kc->getAmplitudeGraph(), h.position);
        std::printf ("handle amp %s %.1f %.1f\n", h.id.toRawUTF8(), p.x, p.y);
    }
    {
        // Top-bar controls (editor coordinates) for the host harness: preset combo, actions button, scale button.
        auto& bar = kc->getTopBar();
        auto centreOf = [&] (juce::Component& c) { return editor->getLocalPoint (&c, c.getLocalBounds().getCentre().toFloat()); };   // scaled editor pixels, like the knobs
        const auto pb = centreOf (bar.getPresetBox()), pm = centreOf (bar.getPresetMenuButton());
        std::printf ("topbar presetBox %.1f %.1f\n", pb.x, pb.y);
        std::printf ("topbar presetMenu %.1f %.1f\n", pm.x, pm.y);
        for (auto* child : bar.getChildren())
            if (child->getComponentID() == "scaleButton")
            {
                const auto sb = centreOf (*child);
                std::printf ("topbar scaleButton %.1f %.1f\n", sb.x, sb.y);
            }
    }
    editor->removeFromDesktop();
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // Every editor created by this process (tests, layout dump) reads the user preset
    // folder: point it at a private temporary directory unless the caller chose one, so the
    // real ~/.config is never even read.
    juce::File isolatedPresetDir;
    if (std::getenv ("KCF_PRESET_DIR") == nullptr)
    {
        isolatedPresetDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("kcf-plugin-tests-presets-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        setenv ("KCF_PRESET_DIR", isolatedPresetDir.getFullPathName().toRawUTF8(), 1);
    }
    struct Cleanup { juce::File d; ~Cleanup() { if (d != juce::File()) d.deleteRecursively(); } } cleanup { isolatedPresetDir };
    if (argc >= 2 && juce::String (argv[1]) == "--layout")
        return printLayout (argc >= 3 ? std::atoi (argv[2]) : 100);
    return kcftest::runAll (argc, argv);
}
