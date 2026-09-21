// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Graphs.h"
#include "plugin/Parameters.h"

#include <algorithm>
#include <array>

namespace kcf::ui
{

namespace
{
    constexpr float handleRadius = 6.0f;
    constexpr float hitRadius = 11.0f;
    constexpr float axisHeight = 18.0f;

    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    juce::String seconds (double s)
    {
        const double ms = s * 1000.0;
        return juce::String (ms, ms < 10.0 ? 1 : 0) + " ms";
    }
}

void buildPreview (PreviewModel& m)
{
    m.params = m.params.sanitized();
    m.snapshot = VoiceSnapshot::make (m.params, m.sampleRate, m.previewNote, 1.0f);
    const int n = juce::jmax (m.snapshot.totalSamples, 0);
    m.waveform.assign ((size_t) juce::jmax (n, 1), 0.0f);
    if (n > 0)
        renderSingleHit (m.params, m.sampleRate, m.previewNote, 1.0f, m.waveform.data(), n);
    // Axis: hit length plus headroom, never shorter than 40 ms so tiny hits stay readable.
    m.axisSeconds = juce::jmax (0.04, m.snapshot.totalSec * 1.12);
}

// ------------------------------------------------------------ GraphPanel --

GraphPanel::GraphPanel (juce::AudioProcessorValueTreeState& s, const PreviewModel& m) : state (s), model (m)
{
    setOpaque (false);
    setWantsKeyboardFocus (false);
}

GraphPanel::~GraphPanel()
{
    endGestures();
}

void GraphPanel::setModel (const PreviewModel& m)
{
    model = m;
    layoutHandles();
    repaint();
}

juce::Rectangle<float> GraphPanel::plotArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 10.0f).withTrimmedTop (18.0f).withTrimmedBottom (axisHeight);
}

double GraphPanel::secondsForX (float x) const
{
    const auto plot = plotArea();
    if (plot.getWidth() <= 0.0f) return 0.0;
    const double axis = dragging >= 0 && frozenAxis > 0.0 ? frozenAxis : model.axisSeconds;
    if (dragging >= 0 && x > plot.getRight())
    {
        // Beyond the right edge while dragging: keep growing so a handle can pass
        // the currently visible range (the parameter range still bounds the value).
        const double overshoot = (double) ((x - plot.getRight()) / plot.getWidth());
        return juce::jmin (10.0, axis * std::pow (overshootGrowthPerWidth, overshoot));
    }
    return juce::jlimit (0.0, axis, (double) ((x - plot.getX()) / plot.getWidth()) * axis);
}

float GraphPanel::xForSeconds (double s) const
{
    const auto plot = plotArea();
    if (plot.getWidth() <= 0.0f || axisOrFrozen() <= 0.0) return plot.getX();
    return plot.getX() + (float) (s / axisOrFrozen()) * plot.getWidth();
}

double GraphPanel::axisOrFrozen() const
{
    return dragging >= 0 && frozenAxis > 0.0 ? frozenAxis : model.axisSeconds;
}

float GraphPanel::getParameterValue (const juce::String& id) const
{
    if (auto* v = state.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void GraphPanel::setParameterValue (const juce::String& id, float value)
{
    if (auto* param = state.getParameter (id))
    {
        if (std::find (activeGestures.begin(), activeGestures.end(), id) == activeGestures.end())
        {
            param->beginChangeGesture();
            activeGestures.push_back (id);
        }
        params::applyToParameter (*param, value, true);
    }
}

void GraphPanel::endGestures()
{
    for (auto& id : activeGestures)
        if (auto* param = state.getParameter (id))
            param->endChangeGesture();
    activeGestures.clear();
}

int GraphPanel::handleAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = hitRadius;
    for (int i = 0; i < (int) handles.size(); ++i)
    {
        const float d = handles[(size_t) i].position.getDistanceFrom (p);
        if (d <= bestDistance) { best = i; bestDistance = d; }
    }
    return best;
}

void GraphPanel::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        setTooltip (h >= 0 ? handles[(size_t) h].tooltip : juce::String());
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void GraphPanel::mouseExit (const juce::MouseEvent&)
{
    if (dragging < 0 && hovered >= 0) { hovered = -1; repaint(); }
}

void GraphPanel::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position);
    if (dragging >= 0)
    {
        dragStart = e.position;
        frozenAxis = model.axisSeconds;
        hovered = dragging;
        dragBegan (handles[(size_t) dragging]);
        repaint();
    }
}

void GraphPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || dragging >= (int) handles.size()) return;
    juce::Point<float> position = e.position;
    juce::Point<float> delta = e.position - dragStart;
    if (e.mods.isShiftDown())     // fine adjustment: quarter speed around the start point
    {
        delta *= 0.25f;
        position = dragStart + delta;
    }
    applyDrag (handles[(size_t) dragging], position, delta);
}

void GraphPanel::mouseUp (const juce::MouseEvent&)
{
    endGestures();
    dragging = -1;
    frozenAxis = 0.0;
    // The preview rebuilt during the drag laid the handles out on the frozen axis; the plot now
    // uses the rescaled one, so lay them out again or they sit off the curve until the next
    // parameter change (1.5.1 fix).
    layoutHandles();
    repaint();
}

void GraphPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h < 0) return;
    for (const auto& id : { handles[(size_t) h].id, handles[(size_t) h].secondaryId })
        if (id.isNotEmpty())
            if (auto* param = state.getParameter (id))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->getDefaultValue());
                param->endChangeGesture();
            }
}

void GraphPanel::paintTimeAxis (juce::Graphics& g, juce::Rectangle<float> plot, bool labels)
{
    const double axis = dragging >= 0 && frozenAxis > 0.0 ? frozenAxis : model.axisSeconds;
    // Choose a tick step giving 4..8 divisions.
    const double candidates[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.025, 0.05, 0.1, 0.2, 0.25, 0.5 };
    double step = 0.5;
    for (double c : candidates) if (axis / c <= 8.0) { step = c; break; }
    g.setFont (font (11.0f, Weight::mono));
    for (double t = 0.0; t <= axis + 1e-9; t += step)
    {
        const float x = xForSeconds (t);
        g.setColour (t == 0.0 ? Palette::gridStrong : Palette::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        if (labels)
        {
            g.setColour (Palette::textDim);
            const auto text = juce::String (t * 1000.0, step < 0.01 ? 0 : 0) + (t == 0.0 ? " ms" : "");
            g.drawText (text, (int) x - 22, (int) plot.getBottom() + 3, 44, 13, juce::Justification::centred);
        }
    }
    // Hit end marker
    const float endX = xForSeconds (model.snapshot.totalSec);
    g.setColour (Palette::textDim.withAlpha (0.6f));
    const float dashes[] = { 3.0f, 3.0f };
    g.drawDashedLine (juce::Line<float> (endX, plot.getY(), endX, plot.getBottom()), dashes, 2, 1.0f);
}

void GraphPanel::drawHandles (juce::Graphics& g)
{
    const auto plot = plotArea();
    for (int i = 0; i < (int) handles.size(); ++i)
    {
        auto h = handles[(size_t) i];
        h.position.x = juce::jmin (h.position.x, plot.getRight());   // a handle dragged past the edge pins to it
        const bool hot = i == hovered || i == dragging;
        const float r = hot ? handleRadius + 1.5f : handleRadius;
        const auto colour = hot ? Palette::copperBright : Palette::ivory;   // every handle drags along at least one axis
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (h.position.x - r, h.position.y - r + 1.5f, 2 * r, 2 * r);
        g.setColour (colour);
        g.fillEllipse (h.position.x - r, h.position.y - r, 2 * r, 2 * r);
        g.setColour (Palette::background);
        g.drawEllipse (h.position.x - r, h.position.y - r, 2 * r, 2 * r, 1.5f);
    }
}

void GraphPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat(), title(), caption());
    const auto plot = plotArea();
    g.setColour (Palette::background.darker (0.15f));
    g.fillRoundedRectangle (plot.expanded (2.0f), 4.0f);
    paintPlot (g, plot);
    drawHandles (g);
}

// ------------------------------------------------------------ PitchGraph --

float PitchGraph::yForHz (double hz, juce::Rectangle<float> plot) const
{
    const double lo = std::log (Limits::minHz), hi = std::log (Limits::maxStartHz);
    const double t = (std::log (juce::jlimit ((double) Limits::minHz, (double) Limits::maxStartHz, hz)) - lo) / (hi - lo);
    return plot.getBottom() - (float) t * plot.getHeight();
}

double PitchGraph::hzForY (float y, juce::Rectangle<float> plot) const
{
    const double lo = std::log (Limits::minHz), hi = std::log (Limits::maxStartHz);
    const double t = juce::jlimit (0.0, 1.0, (double) ((plot.getBottom() - y) / plot.getHeight()));
    return std::exp (lo + t * (hi - lo));
}

juce::String PitchGraph::caption() const
{
    const auto& s = model.snapshot;
    if (model.params.pitchSource == PitchSource::midiNote)
        return "MIDI note" + dot + "Ends at " + params::formatHz ((float) s.endHz) + " (" + params::noteNameForHz ((float) s.endHz) + ")";
    return "Fixed" + dot + "Ends at " + params::formatHz ((float) s.endHz) + " (" + params::noteNameForHz ((float) s.endHz) + ")";
}

void PitchGraph::paintPlot (juce::Graphics& g, juce::Rectangle<float> plot)
{
    paintTimeAxis (g, plot, false);
    // Frequency grid: octaves of A (55, 110, 220, 440, 880, 1760) with note names.
    g.setFont (font (11.0f, Weight::mono));
    for (double hz : { 27.5, 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0 })
    {
        const float y = yForHz (hz, plot);
        g.setColour (Palette::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        if (y - 13.0f < plot.getY() + 14.0f || y > plot.getBottom() - 2.0f) continue;   // inside the plot, below the caption
        g.setColour (Palette::textDim);
        g.drawText (juce::String ((int) hz) + " Hz " + params::noteNameForHz ((float) hz), (int) plot.getRight() - 96, (int) y - 12, 92, 11, juce::Justification::centredRight);
    }

    const auto& s = model.snapshot;
    juce::Path curve;
    const int steps = juce::jmax (2, (int) plot.getWidth());
    const double axis = dragging >= 0 && frozenAxis > 0.0 ? frozenAxis : model.axisSeconds;
    bool started = false;
    for (int i = 0; i <= steps; ++i)
    {
        const double t = axis * (double) i / (double) steps;
        if (t > s.totalSec) break;
        const auto p = juce::Point<float> (xForSeconds (t), yForHz (s.frequencyAt (t), plot));
        if (! started) { curve.startNewSubPath (p); started = true; } else curve.lineTo (p);
    }
    if (started)
    {
        juce::Path fill (curve);
        fill.lineTo (xForSeconds (juce::jmin (s.totalSec, axis)), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Palette::copper.withAlpha (0.28f), 0.0f, plot.getY(),
                                                 Palette::copper.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (Palette::copper);
        g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // Sweep end marker
    const float sweepX = xForSeconds (s.sweepSec);
    g.setColour (Palette::copper.withAlpha (0.35f));
    g.drawVerticalLine ((int) sweepX, plot.getY(), plot.getBottom());
    g.setColour (Palette::textMuted);
    g.setFont (font (11.0f, Weight::mono));
    g.drawText ("Sweep " + seconds (s.sweepSec) + dot + "Start " + params::formatHz ((float) s.startHz),
                (int) plot.getRight() - 200, (int) plot.getY() + 3, 196, 12, juce::Justification::centredRight);
}

void PitchGraph::layoutHandles()
{
    const auto plot = plotArea();
    const auto& s = model.snapshot;
    const bool midi = model.params.pitchSource == PitchSource::midiNote;
    handles.clear();
    Handle start;
    start.id = params::startFreq;
    start.position = { xForSeconds (0.0), yForHz (s.startHz, plot) };
    start.tooltip = "Start frequency (drag up/down). Double-click resets. Applies to the next hit.";
    handles.push_back (start);

    // The knee only edits the sweep time (v1.1): the note is chosen with the End
    // knob (Fixed) or by the played note (MIDI), and must not move while the sweep
    // and the other elements are shaped afterwards.
    Handle knee;
    knee.id = params::sweep;
    knee.position = { xForSeconds (s.sweepSec), yForHz (s.endHz, plot) };
    knee.lockedVertical = true;
    knee.tooltip = midi ? "Sweep time (drag left/right). The end frequency follows the played MIDI note."
                        : "Sweep time (drag left/right). The end frequency is the End knob.";
    handles.push_back (knee);

    if (s.sweepSec > 0.0 && std::fabs (s.startHz - s.endHz) > 0.5)
    {
        Handle curve;
        curve.id = params::curve;
        curve.position = { xForSeconds (s.sweepSec * 0.5), yForHz (s.frequencyAt (s.sweepSec * 0.5), plot) };
        curve.tooltip = "Sweep curve (drag up/down, whole plot height = 1 to 30): bends the pitch trajectory between start and end.";
        handles.push_back (curve);
    }
}

void PitchGraph::dragBegan (const Handle& h)
{
    if (h.id == params::curve)
        curveAtDragStart = getParameterValue (params::curve);
}

void PitchGraph::applyDrag (const Handle& h, juce::Point<float> position, juce::Point<float> delta)
{
    const auto plot = plotArea();
    if (h.id == params::startFreq)
    {
        setParameterValue (params::startFreq, (float) hzForY (position.y, plot));
    }
    else if (h.id == params::sweep)
    {
        setParameterValue (params::sweep, (float) (secondsForX (position.x) * 1000.0));
    }
    else if (h.id == params::curve)
    {
        // Relative, logarithmic: dragging over the whole plot height multiplies or divides
        // the exponent by maxSlope/minSlope, so the handle reaches the full 1..30 range of
        // the knob (mapping the mid-sweep frequency directly saturated at about 13 because
        // f(sweep/2) = start + (end-start) * (1 - 0.5^slope) becomes indistinguishable from
        // `end` for large exponents).
        // The handle follows the mouse (v1.5 fix): a larger exponent pulls the mid-sweep
        // frequency towards `end`, which is DOWN for a falling sweep and UP for a rising one,
        // so the sign of the drag depends on the direction of the sweep.
        const auto& s = model.snapshot;
        const double towardsEnd = s.endHz < s.startHz ? 1.0 : -1.0;   // screen direction (y grows downwards) of a larger exponent
        const double octaves = std::log ((double) Limits::maxSlope / (double) Limits::minSlope);
        const double factor = std::exp (towardsEnd * (double) delta.y / (double) plot.getHeight() * octaves);
        setParameterValue (params::curve, (float) juce::jlimit ((double) Limits::minSlope, (double) Limits::maxSlope,
                                                                (double) curveAtDragStart * factor));
    }
}

// -------------------------------------------------------- AmplitudeGraph --

juce::String AmplitudeGraph::caption() const
{
    const auto& s = model.snapshot;
    return "Attack " + seconds (s.attackSec) + dot + "Fade " + seconds (s.fadeSec) + dot + "Total " + seconds (s.totalSec);
}

void AmplitudeGraph::paintPlot (juce::Graphics& g, juce::Rectangle<float> plot)
{
    paintTimeAxis (g, plot, true);
    for (double level : { 0.25, 0.5, 0.75, 1.0 })
    {
        const float y = plot.getBottom() - (float) level * plot.getHeight();
        g.setColour (Palette::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
    }
    const auto& s = model.snapshot;
    const double axis = dragging >= 0 && frozenAxis > 0.0 ? frozenAxis : model.axisSeconds;
    juce::Path env;
    const int steps = juce::jmax (2, (int) plot.getWidth() * 2);
    env.startNewSubPath (xForSeconds (0.0), plot.getBottom());
    for (int i = 0; i <= steps; ++i)
    {
        const double t = juce::jmin (axis, s.totalSec) * (double) i / (double) steps;
        env.lineTo (xForSeconds (t), plot.getBottom() - (float) s.envelopeAt (juce::jmin (t, s.totalSec - 1e-9)) * plot.getHeight());
    }
    env.lineTo (xForSeconds (juce::jmin (axis, s.totalSec)), plot.getBottom());
    env.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Palette::amber.withAlpha (0.30f), 0.0f, plot.getY(),
                                             Palette::amber.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
    g.fillPath (env);
    g.setColour (Palette::amber);
    g.strokePath (env, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Fade-start marker
    const float fadeX = xForSeconds (s.fadeStartSec);
    g.setColour (Palette::amber.withAlpha (0.35f));
    g.drawVerticalLine ((int) fadeX, plot.getY(), plot.getBottom());
}

void AmplitudeGraph::layoutHandles()
{
    const auto plot = plotArea();
    const auto& s = model.snapshot;
    handles.clear();
    Handle attack;
    attack.id = params::attack;
    attack.position = { xForSeconds (s.attackSec), plot.getY() + 1.0f };
    attack.tooltip = "Attack time (drag left/right): linear ramp-in of the next hit.";
    handles.push_back (attack);

    Handle fade;
    fade.id = params::fade;
    fade.position = { xForSeconds (s.fadeStartSec), plot.getY() + 1.0f };
    fade.tooltip = "Fade-out start (drag left/right). Stored as a fraction of the hit length: 0 % = 10 ms fade, 100 % = fades over the whole hit.";
    handles.push_back (fade);

    Handle end;
    end.id = params::hold;
    end.position = { xForSeconds (s.totalSec), plot.getBottom() - 1.0f };
    end.tooltip = "Hit end (drag left/right): sets the hold time after the sweep.";
    handles.push_back (end);
}

void AmplitudeGraph::applyDrag (const Handle& h, juce::Point<float> position, juce::Point<float>)
{
    const double t = secondsForX (position.x);
    const auto& s = model.snapshot;
    if (h.id == params::attack)
        setParameterValue (params::attack, (float) (t * 1000.0));
    else if (h.id == params::hold)
        setParameterValue (params::hold, (float) ((t - s.sweepSec) * 1000.0));
    else if (h.id == params::fade)
    {
        const double total = s.totalSec;
        const double fadeSec = juce::jlimit (0.0, total, total - t);
        const double denominator = total - Limits::minFadeSec;
        const double fraction = denominator > 1e-6 ? (fadeSec - Limits::minFadeSec) / denominator : 1.0;
        setParameterValue (params::fade, (float) (juce::jlimit (0.0, 1.0, fraction) * 100.0));
    }
}

// ------------------------------------------------------- WaveformPreview --

juce::String WaveformPreview::caption() const
{
    const auto& s = model.snapshot;
    const juce::String note = params::noteNameForMidi (model.previewNote);   // same convention as the pitch graph
    return "Preview note " + note + dot + juce::String (s.totalSamples) + " samples @ " + juce::String ((int) model.sampleRate) + " Hz";
}

void WaveformPreview::paintPlot (juce::Graphics& g, juce::Rectangle<float> plot)
{
    paintTimeAxis (g, plot, true);
    const float mid = plot.getCentreY();
    g.setColour (Palette::gridStrong);
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
    const auto& w = model.waveform;
    const int n = (int) w.size();
    if (n > 1 && model.snapshot.totalSamples > 0)
    {
        const int columns = juce::jmax (1, (int) plot.getWidth());
        const double samplesPerSecond = model.sampleRate;
        juce::Path body;
        std::vector<float> minima ((size_t) columns, 0.0f), maxima ((size_t) columns, 0.0f);
        for (int c = 0; c < columns; ++c)
        {
            const double t0 = secondsForX (plot.getX() + (float) c), t1 = secondsForX (plot.getX() + (float) c + 1.0f);
            const int i0 = juce::jlimit (0, n, (int) (t0 * samplesPerSecond));
            const int i1 = juce::jlimit (i0 + 1, n, (int) (t1 * samplesPerSecond) + 1);
            float lo = 0.0f, hi = 0.0f;
            for (int i = i0; i < i1 && i < n; ++i) { lo = juce::jmin (lo, w[(size_t) i]); hi = juce::jmax (hi, w[(size_t) i]); }
            minima[(size_t) c] = lo;
            maxima[(size_t) c] = hi;
        }
        const float half = plot.getHeight() * 0.48f;
        body.startNewSubPath (plot.getX(), mid);
        for (int c = 0; c < columns; ++c) body.lineTo (plot.getX() + (float) c, mid - maxima[(size_t) c] * half);
        for (int c = columns - 1; c >= 0; --c) body.lineTo (plot.getX() + (float) c, mid - minima[(size_t) c] * half);
        body.closeSubPath();
        g.setColour (Palette::ivory.withAlpha (0.55f + 0.35f * flash));
        g.fillPath (body);
        g.setColour (Palette::ivory.withAlpha (0.9f));
        g.strokePath (body, juce::PathStrokeType (1.0f));
    }
    // Segment markers (v1.1): the sequential pieces of the hit, in the colours of the
    // graphs that edit them (copper = pitch graph, amber = amplitude graph). The hit
    // end is the dashed line drawn by the time axis.
    {
        const auto& s = model.snapshot;
        struct Marker { double t; const char* label; juce::Colour colour; bool skip; };
        std::array<Marker, 3> markers { {
            { s.attackSec,    "Attack", Palette::amber,  s.attackSec < 0.002 },   // 0.4 ms default sits on the origin
            { s.sweepSec,     "Sweep",  Palette::copper, s.sweepSec <= 0.0 },
            { s.fadeStartSec, "Fade",   Palette::amber,  s.fadeStartSec <= 0.0 },
        } };
        std::sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.t < b.t; });
        g.setFont (font (10.0f, Weight::mono));
        float lastLabelRight = -1000.0f;
        for (const auto& m : markers)
        {
            if (m.skip || m.t > model.axisSeconds) continue;
            const float x = xForSeconds (m.t);
            g.setColour (m.colour.withAlpha (0.55f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            const float labelX = juce::jmin (x + 3.0f, plot.getRight() - 44.0f);
            if (labelX > lastLabelRight + 4.0f)                     // avoid stacking labels on top of each other
            {
                g.setColour (m.colour.withAlpha (0.9f));
                g.drawText (m.label, (int) labelX, (int) plot.getY() + 2, 44, 11, juce::Justification::centredLeft);
                lastLabelRight = labelX + 40.0f;
            }
        }
    }
    if (flash > 0.001f)
    {
        g.setColour (Palette::copper.withAlpha (0.25f * flash));
        g.fillRoundedRectangle (plot, 4.0f);
    }
}

} // namespace kcf::ui
