// Single-page editor: top bar, timeline graphs (left), knob grid (right).
// Laid out at a logical 1000 x 640 and scaled as vectors to the window size.
// All host -> UI synchronisation is polled from the message-thread timer; no
// listener ever runs (or posts) from the audio thread.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Graphs.h"
#include "ui/Knob.h"
#include "ui/PolledBinding.h"
#include "ui/Theme.h"
#include "ui/TopBar.h"

class KickCrafterEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit KickCrafterEditor (KickCrafterProcessor&);
    ~KickCrafterEditor() override;

    static constexpr int logicalWidth = 1000;
    static constexpr int logicalHeight = 640;
    static constexpr int minScalePercent = 80;
    static constexpr int maxScalePercent = 160;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Introspection for tests/automation (message thread).
    kcf::ui::PitchGraph& getPitchGraph() noexcept { return pitchGraph; }
    kcf::ui::AmplitudeGraph& getAmplitudeGraph() noexcept { return amplitudeGraph; }
    kcf::ui::WaveformPreview& getWaveformPreview() noexcept { return waveformPreview; }
    kcf::ui::TopBar& getTopBar() noexcept { return topBar; }
    kcf::ui::Knob* findKnob (const juce::String& parameterId);
    float getScale() const noexcept { return scale; }
    const kcf::ui::PreviewModel& getPreviewModel() const noexcept { return preview; }
    void pollNow() { timerCallback(); }      // tests: synchronous refresh instead of waiting for the timer

private:
    void timerCallback() override;
    void rebuildPreviewIfNeeded();
    void applyScalePercent (int percent);

    KickCrafterProcessor& processor;
    kcf::ui::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { nullptr, 600 };

    // Content lives in one child so a single affine transform scales everything.
    juce::Component content;
    kcf::ui::TopBar topBar;
    kcf::ui::PreviewModel preview;
    kcf::ui::PitchGraph pitchGraph;
    kcf::ui::AmplitudeGraph amplitudeGraph;
    kcf::ui::WaveformPreview waveformPreview;
    std::vector<std::unique_ptr<kcf::ui::Knob>> knobs;
    juce::Label pitchSourceLabel, channelLabel, velocityLabel, footer;
    juce::Label versionLabel;              // project version, right end of the footer (1.5.3)
    juce::TextButton pitchMidi { "MIDI Note" }, pitchFixed { "Fixed" };
    juce::TextButton velocityButton { "On" };                 // v1.3: velocity sensitivity on/off (was a knob)
    juce::ComboBox channelBox;
    std::unique_ptr<kcf::ui::PolledBinding> pitchSourceBinding, channelBinding, velocityBinding;

    float scale = 1.0f;
    std::uint32_t seenNoteCounter = 0;
    float ledLevel = 0.0f;
    int previewNote = KickCrafterProcessor::auditionNote;
    bool constructing = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickCrafterEditor)
};
