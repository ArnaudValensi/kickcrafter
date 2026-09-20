// Timeline graphs: pitch curve, amplitude envelope and next-hit waveform
// preview sharing one time axis. Handles edit a fixed set of parameters.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "engine/KickEngine.h"

#include <vector>

namespace kcf::ui
{

// Snapshot of what the graphs display: the NEXT hit, never an active voice.
struct PreviewModel
{
    KickParams params;
    double sampleRate = 48000.0;
    int previewNote = 33;            // note used to resolve the MIDI pitch source
    VoiceSnapshot snapshot;          // derived from the above
    std::vector<float> waveform;     // rendered single hit (offline, message thread)
    double axisSeconds = 0.2;        // shared time axis length

    bool sameInputs (const PreviewModel& o) const noexcept
    {
        return params == o.params && sampleRate == o.sampleRate && previewNote == o.previewNote;
    }
};

class GraphPanel : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    struct Handle
    {
        juce::String id;                 // parameter this handle edits (primary)
        juce::String secondaryId;        // optional second parameter (x/y)
        juce::Point<float> position;     // pixels, updated in layoutHandles()
        juce::String tooltip;
        bool lockedVertical = false;     // horizontal-only handle (tooltip/tests; drawn like the others)
        // Every handle is a circle (v1.5): the former square knee and diamond time handles
        // looked like a legend nobody could read. The tooltip says which way a handle drags.
    };

    GraphPanel (juce::AudioProcessorValueTreeState& state, const PreviewModel& model);
    ~GraphPanel() override;

    void setModel (const PreviewModel& m);   // repaint + relayout handles
    void resized() override { layoutHandles(); }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    juce::Rectangle<float> plotArea() const;
    float xForSeconds (double s) const;
    double secondsForX (float x) const;
    double axisOrFrozen() const;             // the axis in use (frozen while dragging)

    // Dragging a time handle past the plot's right edge keeps growing the value
    // (exponentially with the overshoot: one plot width beyond the edge = x6), so
    // hold/attack/sweep can exceed what the current axis shows; the axis rescales
    // on release. Exposed for the tests.
    static constexpr double overshootGrowthPerWidth = 6.0;

    // Handle positions in component coordinates (used by GUI tests/automation).
    std::vector<Handle> getHandles() const { return handles; }

protected:
    virtual void paintPlot (juce::Graphics&, juce::Rectangle<float> plot) = 0;
    virtual void layoutHandles() = 0;
    virtual void applyDrag (const Handle&, juce::Point<float> position, juce::Point<float> delta) = 0;
    virtual void dragBegan (const Handle&) {}     // capture values for relative drags
    virtual juce::String title() const = 0;
    virtual juce::String caption() const = 0;

    void setParameterValue (const juce::String& id, float displayValue);   // during a gesture
    float getParameterValue (const juce::String& id) const;
    void paintTimeAxis (juce::Graphics&, juce::Rectangle<float> plot, bool labels);
    void drawHandles (juce::Graphics&);

    juce::AudioProcessorValueTreeState& state;
    PreviewModel model;
    std::vector<Handle> handles;
    int hovered = -1;
    int dragging = -1;
    juce::Point<float> dragStart;
    double frozenAxis = 0.0;                 // axis length captured while dragging
    std::vector<juce::String> activeGestures;

private:
    int handleAt (juce::Point<float>) const;
    void endGestures();
};

class PitchGraph final : public GraphPanel
{
public:
    using GraphPanel::GraphPanel;
protected:
    void paintPlot (juce::Graphics&, juce::Rectangle<float>) override;
    void layoutHandles() override;
    void applyDrag (const Handle&, juce::Point<float>, juce::Point<float>) override;
    juce::String title() const override { return "Pitch"; }
    juce::String caption() const override;
    void dragBegan (const Handle&) override;
private:
    float yForHz (double hz, juce::Rectangle<float> plot) const;
    double hzForY (float y, juce::Rectangle<float> plot) const;
    float curveAtDragStart = 1.0f;
};

class AmplitudeGraph final : public GraphPanel
{
public:
    using GraphPanel::GraphPanel;
protected:
    void paintPlot (juce::Graphics&, juce::Rectangle<float>) override;
    void layoutHandles() override;
    void applyDrag (const Handle&, juce::Point<float>, juce::Point<float>) override;
    juce::String title() const override { return "Amplitude"; }
    juce::String caption() const override;
};

class WaveformPreview final : public GraphPanel
{
public:
    using GraphPanel::GraphPanel;
    void setTriggerFlash (float amount) { flash = amount; repaint(); }
protected:
    void paintPlot (juce::Graphics&, juce::Rectangle<float>) override;
    void layoutHandles() override { handles.clear(); }
    void applyDrag (const Handle&, juce::Point<float>, juce::Point<float>) override {}
    juce::String title() const override { return juce::String::fromUTF8 ("Waveform  \xc2\xb7  Next hit"); }
    juce::String caption() const override;
private:
    float flash = 0.0f;
};

// Builds the preview: derives the snapshot, renders the hit offline, sets the axis.
void buildPreview (PreviewModel& model);

} // namespace kcf::ui
