// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

using namespace kcf;
using namespace kcf::ui;

namespace
{
    constexpr int topBarHeight = 56;
    constexpr int margin = 18;
    constexpr int gap = 12;
}

KickCrafterEditor::KickCrafterEditor (KickCrafterProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      topBar (p),
      pitchGraph (p.getState(), preview),
      amplitudeGraph (p.getState(), preview),
      waveformPreview (p.getState(), preview)
{
    // Read the wanted scale first: installing resize limits below constrains the
    // (still unsized) editor and would otherwise overwrite it.
    const int wantedScale = juce::jlimit (minScalePercent, maxScalePercent, processor.getUIScalePercent());

    setLookAndFeel (&lookAndFeel);
    content.setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.addAndMakeVisible (topBar);
    content.addAndMakeVisible (pitchGraph);
    content.addAndMakeVisible (amplitudeGraph);
    content.addAndMakeVisible (waveformPreview);

    auto& state = processor.getState();
    using Kind = Knob::Kind;
    using Family = Knob::Family;      // arc colour = the graph that edits the same value
    struct Spec { const char* id; const char* title; Kind kind; Family family; const char* tip; };
    const Spec specs[] = {
        { params::startFreq, "Start", Kind::frequency, Family::pitch,    "Start frequency of the pitch sweep (20 Hz to 2 kHz, logarithmic). Next hit only. Copper = pitch graph." },
        { params::endFreq,   "End",   Kind::frequency, Family::pitch,    "End frequency (the note) when Pitch Source is Fixed. In MIDI Note mode the played note sets it." },
        { params::sweep,     "Sweep", Kind::time,      Family::pitch,    "Time to sweep from the start to the end frequency (0 to 250 ms). Also the square handle of the pitch graph." },
        { params::hold,      "Hold",  Kind::time,      Family::envelope, "Time the hit continues after the sweep (0 to 1000 ms). Hit length = sweep + hold. Amber = amplitude graph." },
        { params::fade,      "Fade",  Kind::ratio,     Family::envelope, "Fade-out length as a fraction of the hit: 0 % = 10 ms fade, 100 % = the whole hit. Amber = amplitude graph." },
        { params::attack,    "Attack", Kind::time,     Family::envelope, "Linear ramp-in at the start of the hit (0.4 to 50 ms). Amber = amplitude graph." },
        { params::curve,     "Curve", Kind::plain,     Family::pitch,    "Sweep curve exponent: 1 = linear pitch drop, higher bends the pitch down faster. Copper = pitch graph." },
        { params::shape,     "Shape", Kind::ratio,     Family::level,    "Oscillator morph: 0 % sine, 100 % square (2048-point wavetables, as the Daisy original). Ivory = waveform." },
        { params::drive,     "Drive", Kind::ratio,     Family::level,    "Per-hit gain before the bus soft limiter (0 to 4x). Frozen when the note starts. Ivory = waveform." },
    };
    for (const auto& s : specs)
    {
        knobs.push_back (std::make_unique<Knob> (state, s.id, s.title, s.kind, s.family, s.tip));
        content.addAndMakeVisible (*knobs.back());
    }

    auto styleSmallLabel = [] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (font (12.5f, Weight::medium));
        l.setColour (juce::Label::textColourId, Palette::textMuted);
        l.setJustificationType (juce::Justification::centred);
    };
    styleSmallLabel (pitchSourceLabel, "Pitch source");
    styleSmallLabel (channelLabel, "MIDI channel");
    styleSmallLabel (velocityLabel, "Velocity");
    content.addAndMakeVisible (pitchSourceLabel);
    content.addAndMakeVisible (channelLabel);
    content.addAndMakeVisible (velocityLabel);

    // Velocity sensitivity switch (v1.3, replaces the 0-100 % knob): On = the MIDI velocity
    // scales the hit level linearly (127 = full), Off = every note plays at full level.
    velocityButton.setTooltip ("On: the MIDI velocity scales the hit level (127 = full level). Off: every note plays at full level, as the Daisy original. Snapshotted per hit.");
    velocityButton.setComponentID ("velocityButton");
    velocityButton.setTitle ("Velocity sensitivity");        // assistive technology: not just "On"/"Off"
    velocityButton.setClickingTogglesState (false);
    if (auto* param = state.getParameter (params::velocity))
    {
        velocityBinding = std::make_unique<PolledBinding> (*param, [this] (float v)
        {
            const bool on = v >= 0.5f;
            velocityButton.setToggleState (on, juce::dontSendNotification);
            velocityButton.setButtonText (on ? "On" : "Off");
        });
        velocityBinding->sendInitial();
        velocityButton.onClick = [this] { velocityBinding->setAsCompleteGesture (velocityButton.getToggleState() ? 0.0f : 1.0f); };
    }
    content.addAndMakeVisible (velocityButton);

    pitchMidi.setTooltip ("End frequency follows the played MIDI note (A1 = 55 Hz). Snapshotted per hit.");
    pitchFixed.setTooltip ("End frequency is the End knob, whatever note is played.");
    pitchMidi.setComponentID ("pitchMidi");
    pitchFixed.setComponentID ("pitchFixed");
    pitchMidi.setClickingTogglesState (false);
    pitchFixed.setClickingTogglesState (false);
    pitchFixed.setConnectedEdges (juce::Button::ConnectedOnRight);    // Fixed sits first (left), as in the parameter
    pitchMidi.setConnectedEdges (juce::Button::ConnectedOnLeft);
    if (auto* param = state.getParameter (params::pitchSource))
    {
        pitchSourceBinding = std::make_unique<PolledBinding> (*param, [this] (float v)
        {
            const bool fixed = v < 0.5f;         // choice index 0 = Fixed, 1 = MIDI Note (v1.1)
            pitchMidi.setToggleState (! fixed, juce::dontSendNotification);
            pitchFixed.setToggleState (fixed, juce::dontSendNotification);
        });
        pitchSourceBinding->sendInitial();
        pitchFixed.onClick = [this] { pitchSourceBinding->setAsCompleteGesture (0.0f); };
        pitchMidi.onClick = [this] { pitchSourceBinding->setAsCompleteGesture (1.0f); };
    }
    content.addAndMakeVisible (pitchMidi);
    content.addAndMakeVisible (pitchFixed);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (params::midiChannel)))
    {
        channelBox.addItemList (choice->choices, 1);
        channelBinding = std::make_unique<PolledBinding> (*choice, [this] (float v)
        {
            const int index = juce::roundToInt (v * (float) (channelBox.getNumItems() - 1));
            channelBox.setSelectedItemIndex (index, juce::dontSendNotification);
        });
        channelBinding->sendInitial();
        channelBox.onChange = [this]
        {
            if (channelBinding == nullptr || channelBinding->isApplyingFromHost()) return;
            const int n = channelBox.getNumItems() - 1;
            channelBinding->setAsCompleteGesture (n > 0 ? (float) channelBox.getSelectedItemIndex() / (float) n : 0.0f);
        };
    }
    channelBox.setTooltip ("MIDI input channel filter. Omni accepts every channel (the Daisy original listened to channel 10 only).");
    channelBox.setComponentID ("channelBox");
    content.addAndMakeVisible (channelBox);

    footer.setText (juce::String::fromUTF8 ("Knobs and graphs describe the next hit; sounding hits keep their frozen settings.   \xc2\xb7   knob colour = its graph (copper pitch, amber amplitude, ivory level and tone)   \xc2\xb7   Shift-drag: fine   \xc2\xb7   double-click: reset   \xc2\xb7   click a value to type"),
                    juce::dontSendNotification);
    footer.setFont (font (12.0f));
    footer.setColour (juce::Label::textColourId, Palette::textDim);
    footer.setJustificationType (juce::Justification::centredLeft);
    content.addAndMakeVisible (footer);

    topBar.onScaleSelected = [this] (int percent) { applyScalePercent (percent); };

    preview.sampleRate = processor.getCurrentSampleRate();
    preview.previewNote = previewNote;
    preview.params = processor.getCurrentParams();
    preview.waveform.clear();
    rebuildPreviewIfNeeded();

    // Resize policy: fixed aspect, 80 %..160 %. `constructing` keeps the
    // constrainer's initial bounds check from writing a scale back to the processor.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) logicalWidth / (double) logicalHeight);
    setResizeLimits (logicalWidth * minScalePercent / 100, logicalHeight * minScalePercent / 100,
                     logicalWidth * maxScalePercent / 100, logicalHeight * maxScalePercent / 100);
    constructing = false;
    applyScalePercent (wantedScale);
    seenNoteCounter = processor.getNoteCounter();
    startTimerHz (30);
}

KickCrafterEditor::~KickCrafterEditor()
{
    stopTimer();
    knobs.clear();
    pitchSourceBinding.reset();
    channelBinding.reset();
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

Knob* KickCrafterEditor::findKnob (const juce::String& id)
{
    for (auto& k : knobs)
        if (k->getParameterId() == id) return k.get();
    return nullptr;
}

void KickCrafterEditor::applyScalePercent (int percent)
{
    percent = juce::jlimit (minScalePercent, maxScalePercent, percent);
    processor.setUIScalePercent (percent);
    setSize (logicalWidth * percent / 100, logicalHeight * percent / 100);   // resized() lays out
}

void KickCrafterEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void KickCrafterEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    scale = (float) getWidth() / (float) logicalWidth;
    if (! constructing)
        processor.setUIScalePercent (juce::jlimit (minScalePercent, maxScalePercent, (int) std::lround (scale * 100.0f)));
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, logicalWidth, logicalHeight);

    auto r = juce::Rectangle<int> (0, 0, logicalWidth, logicalHeight);
    topBar.setBounds (r.removeFromTop (topBarHeight));
    r.reduce (margin, margin);
    footer.setBounds (r.removeFromBottom (16));
    r.removeFromBottom (6);

    auto left = r.removeFromLeft (600);
    r.removeFromLeft (gap + 4);
    auto right = r;

    const int totalLeft = left.getHeight();
    const int pitchH = (int) (totalLeft * 0.40f);
    const int ampH = (int) (totalLeft * 0.28f);
    pitchGraph.setBounds (left.removeFromTop (pitchH));
    left.removeFromTop (gap);
    amplitudeGraph.setBounds (left.removeFromTop (ampH));
    left.removeFromTop (gap);
    waveformPreview.setBounds (left);

    // Knob grid: 3 columns x 4 rows (9 knobs); the last row hosts the velocity switch + mode controls.
    const int cols = 3;
    const int cellW = right.getWidth() / cols;
    const int cellH = right.getHeight() / 4;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int row = (int) i / cols, col = (int) i % cols;
        knobs[i]->setBounds (right.getX() + col * cellW, right.getY() + row * cellH, cellW, cellH);
    }
    auto velocityArea = juce::Rectangle<int> (right.getX(), right.getY() + 3 * cellH, cellW, cellH).reduced (6, 8);
    velocityArea = velocityArea.removeFromTop (velocityArea.getHeight() / 2).reduced (0, 4);   // aligned with "Pitch source"
    velocityLabel.setBounds (velocityArea.removeFromTop (17));
    velocityArea.removeFromTop (4);
    velocityButton.setBounds (velocityArea.removeFromTop (28).withSizeKeepingCentre (96, 28));
    auto modeArea = juce::Rectangle<int> (right.getX() + cellW, right.getY() + 3 * cellH, cellW * 2, cellH).reduced (6, 8);
    auto pitchArea = modeArea.removeFromTop (modeArea.getHeight() / 2).reduced (0, 4);
    pitchSourceLabel.setBounds (pitchArea.removeFromTop (17));
    pitchArea.removeFromTop (4);
    auto buttons = pitchArea.removeFromTop (28);
    pitchFixed.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2));
    pitchMidi.setBounds (buttons);
    auto channelArea = modeArea.reduced (0, 4);
    channelLabel.setBounds (channelArea.removeFromTop (17));
    channelArea.removeFromTop (4);
    channelBox.setBounds (channelArea.removeFromTop (28).withSizeKeepingCentre (110, 28));
}

void KickCrafterEditor::rebuildPreviewIfNeeded()
{
    PreviewModel next;
    next.params = processor.getCurrentParams();       // reads the parameter atomics (message thread)
    next.sampleRate = processor.getCurrentSampleRate();
    next.previewNote = previewNote;
    if (next.sameInputs (preview) && ! preview.waveform.empty())
        return;                                       // unchanged: no recalculation
    buildPreview (next);
    preview = std::move (next);
    pitchGraph.setModel (preview);
    amplitudeGraph.setModel (preview);
    waveformPreview.setModel (preview);
    topBar.repaint();
}

void KickCrafterEditor::timerCallback()
{
    // 1. Host/automation -> controls, without any audio-thread posting.
    for (auto& k : knobs) k->poll();
    if (pitchSourceBinding) pitchSourceBinding->poll();
    if (channelBinding) channelBinding->poll();
    if (velocityBinding) velocityBinding->poll();

    // 2. Note feedback.
    const auto counter = processor.getNoteCounter();
    if (counter != seenNoteCounter)
    {
        seenNoteCounter = counter;
        ledLevel = 1.0f;
        const int note = processor.getLastNote();
        if (note >= 0 && note != previewNote && processor.getCurrentParams().pitchSource == PitchSource::midiNote)
            previewNote = note;
    }
    else
    {
        ledLevel = juce::jmax (0.0f, ledLevel - 0.09f);
    }
    waveformPreview.setTriggerFlash (ledLevel);
    topBar.refresh (ledLevel, processor.getActiveVoiceCount(), processor.getLastNote(), processor.getLastVelocity());

    // 3. Next-hit preview, only when its inputs changed.
    rebuildPreviewIfNeeded();
}
