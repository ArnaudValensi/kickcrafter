// Message-thread-only binding between a host parameter and a UI control.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// JUCE's stock attachments and APVTS listeners are notified synchronously on
// whichever thread changes the parameter, and post async updates from the audio
// thread (a lock + queue insertion). Here nothing is posted from the audio
// thread: the editor's timer calls poll() and pushes changed values into the
// control. Control edits flow to the parameter with begin/value/end gestures.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace kcf::ui
{

class PolledBinding
{
public:
    PolledBinding (juce::RangedAudioParameter& p, std::function<void (float normalised)> applyToControl)
        : parameter (p), apply (std::move (applyToControl)) {}

    ~PolledBinding() { endGesture(); }

    juce::RangedAudioParameter& getParameter() noexcept { return parameter; }

    // Push the current parameter value into the control (constructor / first poll).
    void sendInitial()
    {
        lastSeen = parameter.getValue();
        applyingFromHost = true;
        apply (lastSeen);
        applyingFromHost = false;
    }

    // Message thread: update the control if the host/automation moved the parameter.
    void poll()
    {
        const float v = parameter.getValue();
        if (v == lastSeen)
            return;
        lastSeen = v;
        applyingFromHost = true;
        apply (v);
        applyingFromHost = false;
    }

    // True while apply() runs, so control callbacks can ignore host-driven refreshes.
    bool isApplyingFromHost() const noexcept { return applyingFromHost; }
    bool isGestureActive() const noexcept { return gestureActive; }

    void beginGesture()
    {
        if (gestureActive) return;
        gestureActive = true;
        parameter.beginChangeGesture();
    }

    void setNormalised (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        parameter.setValueNotifyingHost (v);
        // Show the value the parameter actually committed (it may snap/clamp), so
        // controls that rely on the binding for their appearance (buttons, combo
        // boxes) never stay stale after a native click. Not a host refresh, not a gesture.
        lastSeen = parameter.getValue();
        applyingFromHost = true;
        apply (lastSeen);
        applyingFromHost = false;
    }

    void endGesture()
    {
        if (! gestureActive) return;
        gestureActive = false;
        parameter.endChangeGesture();
    }

    void setAsCompleteGesture (float v)
    {
        if (gestureActive) { setNormalised (v); return; }
        beginGesture();
        setNormalised (v);
        endGesture();
    }

private:
    juce::RangedAudioParameter& parameter;
    std::function<void (float)> apply;
    float lastSeen = -1.0f;
    bool applyingFromHost = false;
    bool gestureActive = false;
};

} // namespace kcf::ui
