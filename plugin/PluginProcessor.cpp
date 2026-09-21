// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <limits>

using namespace kcf;

// RAII control transaction: holds controlLock and, for the outermost transaction
// on this thread, captures the coherent "before" snapshot.
struct KickCrafterProcessor::Transaction
{
    explicit Transaction (KickCrafterProcessor& p) : owner (p), lock (p.controlLock)
    {
        if (owner.transactionDepth++ == 0)
            owner.beforeTransaction = owner.captureControlSnapshot();
    }
    ~Transaction() { --owner.transactionDepth; }
    KickCrafterProcessor& owner;
    const juce::ScopedLock lock;
};

KickCrafterProcessor::ControlSnapshot KickCrafterProcessor::captureControlSnapshot() const
{
    ControlSnapshot s;
    for (size_t i = 0; i < params::allIds.size(); ++i)
        s.parameters[i] = apvts.getRawParameterValue (params::allIds[i])->load();
    s.other = getOtherSlotValues();
    s.slot = getABSlot();
    s.preset = getPresetIndex();
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        s.presetKind = presetKind;
        s.presetName = presetName;
        s.presetValues = loadedPresetValues;
    }
    return s;
}

KickCrafterProcessor::KickCrafterProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KickCrafterParams", params::createLayout())
{
    refs.attach (apvts);
    (void) presets::count();                 // initialise the preset catalogue before any audio
    engine.prepare (48000.0);
    monoScratch.resize (256, 0.0f);
    otherSlot = captureSynthesisValues();   // constructor: no concurrent access yet
    setLoadedPreset (presets::factory().front());   // "Reference": identity + reference values
}

KickCrafterProcessor::~KickCrafterProcessor() = default;

// ------------------------------------------------------------------ audio --

void KickCrafterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    currentSampleRate.store (engine.getSampleRate(), std::memory_order_relaxed);
    monoScratch.assign (256, 0.0f);   // only used when the host gives us no output channel
    activeVoices.store (0, std::memory_order_relaxed);
}

void KickCrafterProcessor::releaseResources() {}

void KickCrafterProcessor::reset()
{
    engine.reset();
}

bool KickCrafterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void KickCrafterProcessor::handleMidiBytes (const juce::uint8* data, int numBytes, const KickParams& p, int channelFilter) noexcept
{
    if (numBytes < 2) return;
    const int status = data[0] & 0xF0;
    const int channel = (data[0] & 0x0F) + 1;
    if (channelFilter != 0 && channel != channelFilter)
        return;

    if (status == 0x90 && numBytes >= 3)
    {
        const int note = data[1] & 0x7F;
        const int velocity = data[2] & 0x7F;
        if (velocity == 0)
            return;                                        // running-status note off: never a trigger
        if (engine.noteOn (p, note, (float) velocity / 127.0f) >= 0)
        {
            lastNote.store (note, std::memory_order_relaxed);
            lastVelocity.store (velocity, std::memory_order_relaxed);
            noteCounter.fetch_add (1, std::memory_order_acq_rel);
        }
    }
    else if (status == 0xB0 && numBytes >= 3)
    {
        const int controller = data[1] & 0x7F;
        if (controller == 120)                             // All Sound Off: stop immediately (declicked)
            engine.allSoundOff();
        // 123 All Notes Off: one-shot voices have no sustain to release, so they
        // complete naturally. Documented in README.
    }
    // Note Off (0x80) is intentionally ignored: hits are one-shot.
}

void KickCrafterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Block-level parameter snapshot: the JUCE VST3 wrapper applies host
    // automation at block boundaries, so this is the finest granularity a
    // note can observe. Every Note On in this block freezes these values.
    const KickParams p = refs.read();
    const int channelFilter = refs.midiChannelFilter();

    if (auditionRequested.exchange (false, std::memory_order_acq_rel))
    {
        if (engine.noteOn (p, auditionNote, (float) auditionVelocity / 127.0f) >= 0)
        {
            lastNote.store (auditionNote, std::memory_order_relaxed);
            lastVelocity.store (auditionVelocity, std::memory_order_relaxed);
            noteCounter.fetch_add (1, std::memory_order_acq_rel);
        }
    }

    // Render straight into channel 0 (no intermediate buffer, no size limit);
    // a channel-less host still advances the engine through the small scratch.
    float* mono = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
    auto renderRange = [&] (int from, int to)
    {
        if (mono != nullptr)
        {
            engine.render (mono + from, to - from);
            return;
        }
        for (int at = from; at < to; )
        {
            const int count = juce::jmin ((int) monoScratch.size(), to - at);
            engine.render (monoScratch.data(), count);
            at += count;
        }
    };

    int cursor = 0;
    for (const auto metadata : midi)
    {
        const int position = juce::jlimit (0, numSamples, metadata.samplePosition);
        if (position > cursor)
        {
            renderRange (cursor, position);
            cursor = position;
        }
        handleMidiBytes (metadata.data, metadata.numBytes, p, channelFilter);
    }
    if (numSamples > cursor)
        renderRange (cursor, numSamples);

    for (int ch = 1; ch < numChannels; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    activeVoices.store (engine.activeVoiceCount(), std::memory_order_relaxed);
    midi.clear();
}

// --------------------------------------------------------------- programs --

// Host programs are deliberately NOT exposed (one nominal program, so the JUCE
// wrapper publishes no "Program" parameter). REAPER applies a Program envelope
// from its processing side, which would force preset loads into a realtime
// context where the coherent state transaction cannot be taken. Factory presets
// live in the editor's top bar and in the saved state instead.
int KickCrafterProcessor::getNumPrograms() { return 1; }
int KickCrafterProcessor::getCurrentProgram() { return 0; }
void KickCrafterProcessor::setCurrentProgram (int) {}
const juce::String KickCrafterProcessor::getProgramName (int) { return "KickCrafter"; }

void KickCrafterProcessor::setLoadedPreset (const presets::Preset& preset)
{
    const int index = preset.kind == presets::Kind::factory
                        ? (int) (presets::findFactory (preset.name) != nullptr ? presets::findFactory (preset.name) - presets::factory().data() : 0)
                        : -1;
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        presetKind = preset.kind;
        presetName = preset.name;
        loadedPresetValues = params::displayArrayFromParams (preset.params);
    }
    presetIndex.store (index, std::memory_order_release);
}

KickCrafterProcessor::LoadedPreset KickCrafterProcessor::getLoadedPreset() const
{
    LoadedPreset p;
    const juce::SpinLock::ScopedLockType lock (slotLock);
    p.kind = presetKind;
    p.name = presetName;
    p.factoryIndex = presetIndex.load (std::memory_order_acquire);
    return p;
}

void KickCrafterProcessor::loadFactoryPreset (int index)
{
    if (index < 0 || index >= presets::count()) return;
    loadPreset (presets::factory()[(size_t) index]);
}

void KickCrafterProcessor::loadPreset (const presets::Preset& preset)
{
    // Control transaction (editor / state / test control threads; never the audio thread).
    const bool onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
    programChangedOnMessageThread.store (onMessageThread ? 1 : 0, std::memory_order_relaxed);
    const Transaction transaction (*this);
    params::setDisplayValuesFromParams (apvts, preset.params, true, onMessageThread);
    setLoadedPreset (preset);
    if (onMessageThread)
        updateHostDisplay();
}

void KickCrafterProcessor::adoptPresetIdentity (const presets::Preset& preset)
{
    const Transaction transaction (*this);
    presets::Preset current = preset;
    current.params = getCurrentParams();            // the reference values are what is in the parameters now
    setLoadedPreset (current);
}

void KickCrafterProcessor::relabelLoadedPreset (const presets::Preset& preset)
{
    const Transaction transaction (*this);
    setLoadedPreset (preset);
}

namespace
{
    bool paramsClose (const KickParams& a, const KickParams& b)
    {
        auto close = [] (float x, float y, float tol) { return std::fabs (x - y) <= tol; };
        return close (a.startHz, b.startHz, 0.05f) && close (a.endHz, b.endHz, 0.05f) && close (a.sweepSec, b.sweepSec, 1e-5f)
            && close (a.holdSec, b.holdSec, 1e-5f) && close (a.fadeFraction, b.fadeFraction, 1e-4f)
            && close (a.attackSec, b.attackSec, 1e-6f) && close (a.slope, b.slope, 1e-3f) && close (a.morph, b.morph, 1e-4f)
            && close (a.gain, b.gain, 1e-4f) && a.velocitySensitive == b.velocitySensitive
            && a.pitchSource == b.pitchSource;
    }
}

bool KickCrafterProcessor::currentValuesMatchPreset (int index) const
{
    if (index < 0 || index >= presets::count()) return false;
    return paramsClose (getCurrentParams(), presets::factory()[(size_t) index].params.sanitized());
}

bool KickCrafterProcessor::currentValuesMatchLoadedPreset() const
{
    params::DisplayValues reference;
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        reference = loadedPresetValues;
    }
    return paramsClose (getCurrentParams(), params::paramsFromDisplayArray (reference));
}

// -------------------------------------------------------------------- A/B --

std::array<float, params::synthesisIds.size()> KickCrafterProcessor::captureSynthesisValues() const
{
    std::array<float, params::synthesisIds.size()> values {};
    for (size_t i = 0; i < params::synthesisIds.size(); ++i)
        values[i] = apvts.getRawParameterValue (params::synthesisIds[i])->load();
    return values;
}

void KickCrafterProcessor::applySynthesisValues (const std::array<float, params::synthesisIds.size()>& values)
{
    const bool gestures = juce::MessageManager::existsAndIsCurrentThread();
    for (size_t i = 0; i < params::synthesisIds.size(); ++i)
    {
        if (auto* param = apvts.getParameter (params::synthesisIds[i]))
        {
            if (gestures) param->beginChangeGesture();
            params::applyToParameter (*param, values[i], true);
            if (gestures) param->endChangeGesture();
        }
    }
}

std::array<float, params::synthesisIds.size()> KickCrafterProcessor::getOtherSlotValues() const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);
    return otherSlot;
}

void KickCrafterProcessor::toggleAB()
{
    const Transaction transaction (*this);
    const auto current = captureSynthesisValues();
    std::array<float, params::synthesisIds.size()> incoming {};
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        incoming = otherSlot;
        otherSlot = current;
    }
    abSlot.store (getABSlot() == 0 ? 1 : 0, std::memory_order_release);
    applySynthesisValues (incoming);
    if (juce::MessageManager::existsAndIsCurrentThread())
        updateHostDisplay();
}

void KickCrafterProcessor::copyCurrentToOtherSlot()
{
    const Transaction transaction (*this);
    const auto current = captureSynthesisValues();
    const juce::SpinLock::ScopedLockType lock (slotLock);
    otherSlot = current;
}

// ------------------------------------------------------------------ state --

void KickCrafterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock (controlLock);
    // Re-entered from a callback inside a transaction: serialise the coherent
    // "before" snapshot instead of the half-applied state.
    const ControlSnapshot snap = transactionDepth > 0 ? beforeTransaction : captureControlSnapshot();

    juce::ValueTree root (stateRootTag);
    root.setProperty ("version", stateVersion, nullptr);
    root.setProperty ("plugin", KCF_VERSION_STRING, nullptr);
    // Diagnostic only: which thread the last native preset load ran on ("message" or "other").
    root.setProperty ("lastProgramChangeThread", programChangedOnMessageThread.load() == 1 ? "message"
                                                  : (programChangedOnMessageThread.load() == 0 ? "other" : "none"), nullptr);

    juce::ValueTree parameters ("Params");
    for (size_t i = 0; i < params::allIds.size(); ++i)
        parameters.setProperty (params::allIds[i], (double) snap.parameters[i], nullptr);
    root.addChild (parameters, -1, nullptr);

    juce::ValueTree ab ("AB");
    ab.setProperty ("slot", snap.slot, nullptr);
    juce::ValueTree other ("Other");
    for (size_t i = 0; i < params::synthesisIds.size(); ++i)
        other.setProperty (params::synthesisIds[i], (double) snap.other[i], nullptr);
    ab.addChild (other, -1, nullptr);
    root.addChild (ab, -1, nullptr);

    juce::ValueTree ui ("UI");
    ui.setProperty ("scale", getUIScalePercent(), nullptr);
    root.addChild (ui, -1, nullptr);

    juce::ValueTree preset ("Preset");
    preset.setProperty ("index", snap.preset, nullptr);                                  // factory index, -1 = user
    preset.setProperty ("kind", snap.presetKind == presets::Kind::user ? "user" : "factory", nullptr);
    preset.setProperty ("name", snap.presetName, nullptr);
    root.addChild (preset, -1, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void KickCrafterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;                                   // empty state: keep current values
    const Transaction transaction (*this);

    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (stateRootTag))
        return;                                   // malformed or foreign state: ignore safely

    const juce::ValueTree root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid())
        return;

    // Version policy: `version` must be a positive integer. 4 is the current
    // schema (1: v1.0, 2: v1.1 pitch-source order, 3: v1.2 preset kind/name,
    // 4: v1.3 velocity on/off). A higher number comes from a newer build: its known fields are
    // still read (forward compatible, best effort) and unknown data is ignored.
    // Missing or invalid versions are rejected as a whole.
    // Schema 1 (v1.0) stored pitchSource with 0 = MIDI Note, 1 = Fixed; schema 2
    // (v1.1) reversed the choice order (0 = Fixed, 1 = MIDI Note) so Fixed is the
    // default and listed first. Version-1 documents are migrated on load.
    const juce::String versionText = root.getProperty ("version").toString().trim();
    if (versionText.isEmpty() || ! versionText.containsOnly ("0123456789") || versionText.getIntValue() < 1)
        return;
    const int documentVersion = versionText.getIntValue();
    const bool migratePitchSource = documentVersion == 1;
    const bool migrateVelocity = documentVersion <= 3;     // 0-100 % sensitivity amount -> on/off (v1.3)
    auto migrated = [migratePitchSource, migrateVelocity] (const char* id, float value)
    {
        if (migratePitchSource && juce::String (id) == params::pitchSource && value >= 0.0f && value <= 1.0f)
            return value >= 0.5f ? 0.0f : 1.0f;      // out-of-range junk is left to the normal clamping
        if (migrateVelocity && juce::String (id) == params::velocity)
            return value > 0.0f ? 1.0f : 0.0f;       // any sensitivity at all was "sensitive"
        return value;
    };

    // XML attributes arrive as strings: accept only complete finite decimal tokens
    // within the float range. Rejected fields keep their value;
    // accepted out-of-range values are clamped by the parameter ranges (Params) and
    // by the same ranges for the A/B other slot, so both branches behave alike.
    auto readFinite = [] (const juce::ValueTree& tree, const char* name, float& out)
    {
        if (! tree.hasProperty (name)) return false;
        const juce::var v = tree.getProperty (name);
        if (v.isDouble() || v.isInt() || v.isInt64())
        {
            const double d = (double) v;
            if (! std::isfinite (d) || std::fabs (d) > (double) std::numeric_limits<float>::max()) return false;
            out = (float) d;
            return true;
        }
        return v.isString() && params::parseStrictFloat (v.toString(), out);
    };

    const auto parameters = root.getChildWithName ("Params");
    if (parameters.isValid())
    {
        for (auto* id : params::allIds)
        {
            float value = 0.0f;
            if (readFinite (parameters, id, value))
                if (auto* param = apvts.getParameter (id))
                    params::applyToParameter (*param, migrated (id, value), true);   // clamps into the range
        }
    }

    const auto ab = root.getChildWithName ("AB");
    if (ab.isValid())
    {
        abSlot.store ((int) ab.getProperty ("slot", 0) != 0 ? 1 : 0, std::memory_order_release);
        const auto other = ab.getChildWithName ("Other");
        auto restored = getOtherSlotValues();     // rejected fields keep the previous other-slot value
        if (other.isValid())
        {
            for (size_t i = 0; i < params::synthesisIds.size(); ++i)
            {
                float value = 0.0f;
                if (readFinite (other, params::synthesisIds[i], value))
                    if (auto* param = apvts.getParameter (params::synthesisIds[i]))
                    {
                        const auto& range = param->getNormalisableRange();
                        // clamp, then snap to a legal value (the v1.3 velocity switch: 0.6 -> 1)
                        restored[i] = range.snapToLegalValue (juce::jlimit (range.start, range.end, migrated (params::synthesisIds[i], value)));
                    }
            }
        }
        {
            const juce::SpinLock::ScopedLockType lock (slotLock);
            otherSlot = restored;
        }
    }

    const auto ui = root.getChildWithName ("UI");
    if (ui.isValid())
    {
        const int scale = (int) ui.getProperty ("scale", 100);
        setUIScalePercent (scale >= 50 && scale <= 200 ? scale : 100);
    }

    const auto preset = root.getChildWithName ("Preset");
    if (preset.isValid())
    {
        // Schema 3 names the preset (kind + name); schema 1/2 stored a factory index only.
        // A user preset is referenced by name: its values are the parameters just restored
        // (so it shows unedited), whether or not its file still exists.
        const int index = (int) preset.getProperty ("index", 0);
        const juce::String kind = preset.getProperty ("kind", "factory").toString();
        const juce::String name = preset.getProperty ("name", "").toString();
        presets::Preset loaded;
        if (kind == "user" && presets::isValidName (name))
        {
            loaded.kind = presets::Kind::user;
            loaded.name = name;
            loaded.params = getCurrentParams();
        }
        else if (const auto* byName = presets::findFactory (name))
            loaded = *byName;
        else
            loaded = presets::factory()[(size_t) (index >= 0 && index < presets::count() ? index : 0)];
        setLoadedPreset (loaded);
    }
}

// ----------------------------------------------------------------- editor --

juce::AudioProcessorEditor* KickCrafterProcessor::createEditor()
{
    return new KickCrafterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickCrafterProcessor();
}
