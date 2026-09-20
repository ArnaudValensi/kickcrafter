// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Knob.h"
#include "plugin/Parameters.h"

namespace kcf::ui
{

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;

    juce::Colour accentFor (Knob::Family family) { return Knob::colourFor (family); }

    juce::RangedAudioParameter& requireParameter (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    {
        auto* p = s.getParameter (id);
        jassert (p != nullptr);
        return *p;
    }
}

// The dial only draws; the slider handles the mouse so JUCE's velocity-mode
// (fine) and double-click reset behaviour stays intact.
class Knob::Dial final : public juce::LookAndFeel_V4
{
public:
    explicit Dial (Family f) : family (f) {}

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float proportion,
                           float, float, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + proportion * (endAngle - startAngle);
        const bool hot = s.isMouseOverOrDragging() || s.hasKeyboardFocus (true);
        const auto accent = accentFor (family);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (centre.x - radius * 0.78f, centre.y - radius * 0.78f + 2.0f, radius * 1.56f, radius * 1.56f);
        juce::ColourGradient face (Palette::knobFace.brighter (0.18f), centre.x, centre.y - radius,
                                   Palette::knobFace.darker (0.25f), centre.x, centre.y + radius, false);
        g.setGradientFill (face);
        g.fillEllipse (centre.x - radius * 0.78f, centre.y - radius * 0.78f, radius * 1.56f, radius * 1.56f);
        g.setColour (Palette::knobRim);
        g.drawEllipse (centre.x - radius * 0.78f, centre.y - radius * 0.78f, radius * 1.56f, radius * 1.56f, 1.0f);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::knobTrack);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (hot ? accent.brighter (0.2f) : accent);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto tip = centre.getPointOnCircumference (radius * 0.66f, angle);
        const auto tail = centre.getPointOnCircumference (radius * 0.30f, angle);
        g.setColour (Palette::text);
        g.drawLine (juce::Line<float> (tail, tip), 2.2f);
        g.setColour (accent);
        g.fillEllipse (tip.x - 2.0f, tip.y - 2.0f, 4.0f, 4.0f);
    }

private:
    Family family;
};

juce::Colour Knob::colourFor (Family family)
{
    switch (family)
    {
        case Family::pitch:    return Palette::copper;
        case Family::envelope: return Palette::amber;
        case Family::level:    break;
    }
    return Palette::ivory;
}

Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& t, Kind k, Family f,
            const juce::String& tooltip)
    : parameterId (id), parameter (requireParameter (s, id)), title (t), kind (k), family (f), dial (std::make_unique<Dial> (f)),
      binding (parameter, [this] (float normalised)
      {
          slider.setValue (parameter.convertFrom0to1 (normalised), juce::dontSendNotification);
          refreshText();
      })
{
    // Same mapping as JUCE's SliderAttachment: the slider works in display units,
    // the parameter's own range does the normalisation.
    const auto& range = parameter.getNormalisableRange();
    juce::NormalisableRange<double> sliderRange (
        (double) range.start, (double) range.end,
        [this] (double, double, double v) { return (double) parameter.convertFrom0to1 ((float) v); },
        [this] (double, double, double v) { return (double) parameter.convertTo0to1 ((float) v); },
        [this] (double, double, double v) { return (double) parameter.convertFrom0to1 (parameter.convertTo0to1 ((float) v)); });
    slider.setNormalisableRange (sliderRange);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (startAngle, endAngle, true);
    slider.setLookAndFeel (dial.get());
    slider.setScrollWheelEnabled (true);
    slider.setVelocityModeParameters (0.6, 1, 0.02, true, juce::ModifierKeys::shiftModifier);   // shift = fine
    slider.setDoubleClickReturnValue (true, parameter.convertFrom0to1 (parameter.getDefaultValue()));
    slider.setWantsKeyboardFocus (true);
    slider.setTooltip (tooltip);
    slider.setName (t);
    slider.setTitle (t);
    slider.setComponentID ("knob_" + id);
    slider.onDragStart = [this] { binding.beginGesture(); };
    slider.onDragEnd = [this] { binding.endGesture(); };
    slider.onValueChange = [this] { controlChanged(); };
    addAndMakeVisible (slider);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (font (13.5f, Weight::mono));
    valueLabel.setColour (juce::Label::textColourId, Palette::text);
    valueLabel.setEditable (true, false, false);
    valueLabel.setTooltip ("Click to type a value" + juce::String (kind == Kind::frequency ? " (Hz or a note name such as A1, C#2, Bb1 +10c)" : ""));
    valueLabel.setComponentID ("value_" + id);
    valueLabel.onEditorShow = [this]
    {
        if (auto* editor = valueLabel.getCurrentTextEditor())
        {
            editor->setJustification (juce::Justification::centred);
            editor->setFont (font (13.5f, Weight::mono));
            editor->setInputRestrictions (16);
            editor->selectAll();
        }
    };
    valueLabel.onTextChange = [this] { commitText (valueLabel.getText()); };
    addAndMakeVisible (valueLabel);
    binding.sendInitial();
}

Knob::~Knob()
{
    slider.onValueChange = nullptr;
    slider.onDragStart = nullptr;
    slider.onDragEnd = nullptr;
    slider.setLookAndFeel (nullptr);
}

void Knob::controlChanged()
{
    refreshText();
    if (binding.isApplyingFromHost())
        return;                                    // host refresh: no gesture, no echo
    const float normalised = parameter.convertTo0to1 ((float) slider.getValue());
    if (binding.isGestureActive())
        binding.setNormalised (normalised);        // inside a mouse drag
    else
        binding.setAsCompleteGesture (normalised); // wheel, double-click reset, keyboard
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (17);                    // title
    valueLabel.setBounds (r.removeFromBottom (19));
    r.removeFromBottom (13);                 // note caption zone for frequency knobs, kept empty otherwise:
                                             // every knob gets the same dial diameter (v1.3, user request)
    slider.setBounds (r.reduced (2));
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (Palette::textMuted);
    g.setFont (font (12.5f, Weight::medium));
    g.drawText (title, getLocalBounds().withHeight (17), juce::Justification::centred);
    if (kind == Kind::frequency)
    {
        g.setColour (Palette::textDim);
        g.setFont (font (11.5f));
        auto r = getLocalBounds();
        r.removeFromBottom (19);
        g.drawText (subCaption, r.removeFromBottom (13), juce::Justification::centred);
    }
    // Unit family mark: bar = time (ms), dot = ratio (%, x).
    if (kind == Kind::time || kind == Kind::ratio)
    {
        g.setColour (accentFor (family).withAlpha (0.8f));
        const float x = (float) getWidth() - 12.0f, y = 7.0f;
        if (kind == Kind::time) g.fillRect (x, y, 7.0f, 2.0f);
        else g.fillEllipse (x + 1.5f, y - 1.5f, 4.5f, 4.5f);
    }
}

void Knob::refreshText()
{
    const float value = (float) slider.getValue();
    valueLabel.setText (parameter.getText (parameter.convertTo0to1 (value), 0), juce::dontSendNotification);
    if (kind == Kind::frequency)
        subCaption = params::noteNameForHz (value);
    repaint();
}

void Knob::commitText (const juce::String& text)
{
    // Validate before touching the parameter: invalid text keeps the current value.
    const float display = kind == Kind::frequency ? params::parseFrequencyText (text, std::numeric_limits<float>::quiet_NaN())
                                                  : params::parseNumberText (text, std::numeric_limits<float>::quiet_NaN());
    if (! std::isfinite (display))
    {
        refreshText();
        return;
    }
    binding.setAsCompleteGesture (parameter.convertTo0to1 (juce::jlimit (parameter.getNormalisableRange().start,
                                                                            parameter.getNormalisableRange().end, display)));
    slider.setValue (parameter.convertFrom0to1 (parameter.getValue()), juce::dontSendNotification);
    refreshText();
}

} // namespace kcf::ui
