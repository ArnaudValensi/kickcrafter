// KickCrafter - plugin/parameter/state integration around the engine.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Presets.h"
#include "engine/KickEngine.h"

#include <array>
#include <atomic>
#include <vector>

class KickCrafterProcessor final : public juce::AudioProcessor
{
public:
    KickCrafterProcessor();
    ~KickCrafterProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.25 + 0.01; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- KickCrafter API (message thread unless noted)
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getState() const noexcept { return apvts; }
    kcf::KickParams getCurrentParams() const { return kcf::params::paramsFromDisplayValues (apvts); }

    // Audition: realtime-safe request consumed by the next processBlock.
    void triggerAudition() noexcept { auditionRequested.store (true, std::memory_order_release); }
    static constexpr int auditionNote = 33;       // A1 = 55 Hz, the reference end frequency
    static constexpr int auditionVelocity = 127;

    // Note feedback for the UI (any thread).
    std::uint32_t getNoteCounter() const noexcept { return noteCounter.load (std::memory_order_acquire); }
    int getLastNote() const noexcept { return lastNote.load (std::memory_order_relaxed); }
    int getLastVelocity() const noexcept { return lastVelocity.load (std::memory_order_relaxed); }
    int getActiveVoiceCount() const noexcept { return activeVoices.load (std::memory_order_relaxed); }
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load (std::memory_order_relaxed); }

    // Control-thread contract (non-realtime operations). State save/restore and the
    // editor's presets/A-B are "control transactions": each one
    // holds `controlLock` for its whole duration so current parameters, the other
    // A/B slot and the preset/slot indices are captured and published coherently
    //. processBlock never takes this lock and never calls into
    // these functions; it only reads parameter atomics. XML parsing, preset
    // catalogue initialisation (done in the constructor) and preview rendering
    // never run on the audio callback. Parameter writes still go through JUCE's
    // parameter objects, which hold the framework's own listener locks briefly;
    // that inherited behaviour is documented, not claimed away. No host program
    // parameter is published (a host Program envelope would reach the plug-in from
    // the processing side, where a state transaction cannot be taken); presets are
    // an editor/state feature and always go through the transaction.

    // A/B comparison. Slot values hold the synthesis parameters in display units.
    int getABSlot() const noexcept { return abSlot.load (std::memory_order_acquire); }
    void toggleAB();
    void copyCurrentToOtherSlot();
    std::array<float, kcf::params::synthesisIds.size()> getOtherSlotValues() const;

    // Presets (editor + state only; no host programs are published, see the .cpp).
    // Loading is a control transaction; gestures and the host-display notification
    // are sent only when called on the message thread. The loaded preset's identity
    // (kind + name) and its reference values are kept so the editor can show
    // "<name> • edited" and the saved state can name the preset; a user preset is
    // referenced by name only (the file may disappear: the values still live in
    // the parameters). v1.2.
    struct LoadedPreset
    {
        kcf::presets::Kind kind = kcf::presets::Kind::factory;
        juce::String name;
        int factoryIndex = 0;          // -1 for a user preset
    };
    LoadedPreset getLoadedPreset() const;
    int getPresetIndex() const noexcept { return presetIndex.load (std::memory_order_acquire); }   // factory index, -1 = user
    void loadFactoryPreset (int index);
    void loadPreset (const kcf::presets::Preset& preset);
    // After Save / Save as / Rename: the current values become the reference of this
    // (user) preset without touching any parameter.
    void adoptPresetIdentity (const kcf::presets::Preset& preset);
    // After Rename: identity + reference values taken from the (renamed) preset itself;
    // the parameters and the "edited" state are untouched.
    void relabelLoadedPreset (const kcf::presets::Preset& preset);
    bool currentValuesMatchPreset (int factoryIndex) const;
    bool currentValuesMatchLoadedPreset() const;

    // UI scale (percent), persisted with the state.
    int getUIScalePercent() const noexcept { return uiScalePercent.load (std::memory_order_relaxed); }
    void setUIScalePercent (int percent) noexcept { uiScalePercent.store (juce::jlimit (50, 200, percent), std::memory_order_relaxed); }

    static constexpr int stateVersion = 4;    // 2 (v1.1): pitchSource order Fixed, MIDI Note; 3 (v1.2): preset kind + name;
                                              // 4 (v1.3): velocity is on/off (older documents: 0-100 % amount, > 0 -> on)
    static constexpr const char* stateRootTag = "KickCrafter";

private:
    std::array<float, kcf::params::synthesisIds.size()> captureSynthesisValues() const;
    void applySynthesisValues (const std::array<float, kcf::params::synthesisIds.size()>& values);
    void handleMidiBytes (const juce::uint8* data, int numBytes, const kcf::KickParams& params, int channelFilter) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    kcf::params::Refs refs;
    kcf::KickEngine engine;
    std::vector<float> monoScratch;

    std::atomic<bool> auditionRequested { false };
    std::atomic<std::uint32_t> noteCounter { 0 };
    std::atomic<int> lastNote { -1 };
    std::atomic<int> lastVelocity { 0 };
    std::atomic<int> activeVoices { 0 };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<int> uiScalePercent { 100 };

    // Coherent control-state snapshot (display units). Every transaction captures
    // one before it starts; a save re-entered synchronously from a parameter or
    // gesture callback during that transaction serialises this "before" snapshot,
    // so saved state is always a complete before-or-after state.
    struct ControlSnapshot
    {
        std::array<float, kcf::params::allIds.size()> parameters {};
        std::array<float, kcf::params::synthesisIds.size()> other {};
        int slot = 0;
        int preset = 0;                                    // factory index, -1 = user preset
        kcf::presets::Kind presetKind = kcf::presets::Kind::factory;
        juce::String presetName;
        kcf::params::DisplayValues presetValues {};        // reference values of the loaded preset
    };
    ControlSnapshot captureControlSnapshot() const;
    struct Transaction;
    int transactionDepth = 0;                 // guarded by controlLock (recursive, same thread)
    ControlSnapshot beforeTransaction;        // valid while transactionDepth > 0

    std::atomic<int> abSlot { 0 };
    std::atomic<int> presetIndex { 0 };                        // factory index, -1 = user preset
    kcf::presets::Kind presetKind = kcf::presets::Kind::factory;   // guarded by slotLock
    juce::String presetName;                                   // guarded by slotLock
    kcf::params::DisplayValues loadedPresetValues {};          // guarded by slotLock
    void setLoadedPreset (const kcf::presets::Preset& preset);   // identity + reference values (slotLock)
    std::atomic<int> programChangedOnMessageThread { -1 };   // diagnostic: thread of the last native preset load (-1 none, 1 message, 0 other)
    juce::CriticalSection controlLock;                        // control transactions (never audio)
    mutable juce::SpinLock slotLock;
    std::array<float, kcf::params::synthesisIds.size()> otherSlot {};   // guarded by slotLock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickCrafterProcessor)
};
