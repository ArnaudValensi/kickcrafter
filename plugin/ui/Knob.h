// Vector rotary knob bound to one host parameter (drag, wheel, fine drag,
// double-click reset, click-to-type). Host refreshes arrive through poll()
// on the message thread and never emit gestures.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PolledBinding.h"
#include "Theme.h"

namespace kcf::ui
{

class Knob final : public juce::Component
{
public:
    enum class Kind { time, frequency, ratio, plain };
    // Colour family (v1.1): the arc takes the colour of the graph that edits the same
    // value: copper = pitch graph, amber = amplitude graph, ivory = level/tone
    // (the waveform). Kind still decides text parsing and the unit mark.
    enum class Family { pitch, envelope, level };
    static juce::Colour colourFor (Family family);

    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& title,
          Kind kind, Family family, const juce::String& tooltip);
    ~Knob() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void poll() { binding.poll(); }

    juce::Slider& getSlider() noexcept { return slider; }
    juce::Label& getValueLabel() noexcept { return valueLabel; }
    const juce::String& getParameterId() const noexcept { return parameterId; }

private:
    class Dial;
    void refreshText();
    void commitText (const juce::String& text);
    void controlChanged();

    juce::String parameterId;
    juce::RangedAudioParameter& parameter;
    juce::String title;
    Kind kind;
    Family family;
    std::unique_ptr<juce::LookAndFeel> dial;   // declared before the slider: outlives it
    juce::Slider slider;
    PolledBinding binding;
    juce::Label valueLabel;
    juce::String subCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace kcf::ui
