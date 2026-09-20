// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Parameters.h"

#include <cstring>

#include <cmath>
#include <limits>

namespace kcf::params
{

namespace
{
    using Range = juce::NormalisableRange<float>;

    Range logRange (float lo, float hi)
    {
        return Range (lo, hi,
                      [lo, hi] (float, float, float v) { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, v)); },
                      [lo, hi] (float, float, float v) { return std::log (juce::jlimit (lo, hi, v) / lo) / std::log (hi / lo); },
                      [lo, hi] (float, float, float v) { return juce::jlimit (lo, hi, v); });
    }

    // No separate VST3 label: the value text already carries its unit ("250.0 Hz"),
    // and generic host UIs append the label again (REAPER showed "245.7 Hz Hz").
    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const juce::String& name, Range range,
                                                          float def,
                                                          std::function<juce::String (float, int)> toText,
                                                          std::function<float (const juce::String&)> fromText)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withStringFromValueFunction (std::move (toText))
                              .withValueFromStringFunction (std::move (fromText));
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, versionHint }, name, range, def, attributes);
    }
}

bool isStrictNumberToken (const juce::String& text)
{
    const auto s = text.trim();
    int i = 0;
    const int n = s.length();
    if (n == 0) return false;
    if (s[i] == '+' || s[i] == '-') ++i;
    int mantissaDigits = 0;
    while (i < n && juce::CharacterFunctions::isDigit (s[i])) { ++i; ++mantissaDigits; }
    if (i < n && s[i] == '.')
    {
        ++i;
        while (i < n && juce::CharacterFunctions::isDigit (s[i])) { ++i; ++mantissaDigits; }
    }
    if (mantissaDigits == 0) return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E'))
    {
        ++i;
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        int exponentDigits = 0;
        while (i < n && juce::CharacterFunctions::isDigit (s[i])) { ++i; ++exponentDigits; }
        if (exponentDigits == 0) return false;
    }
    return i == n;
}

bool parseStrictFloat (const juce::String& text, float& out)
{
    if (! isStrictNumberToken (text)) return false;
    const double d = text.trim().getDoubleValue();
    if (! std::isfinite (d) || std::fabs (d) > (double) std::numeric_limits<float>::max()) return false;
    out = (float) d;
    return true;
}

float parseFrequencyText (const juce::String& raw, float fallback)
{
    const auto text = raw.trim();
    if (text.isEmpty()) return fallback;
    const juce::juce_wchar first = juce::CharacterFunctions::toUpperCase (text[0]);
    if (first >= 'A' && first <= 'G')
    {
        // Note name: letter, optional accidental, octave -1..9, optional " +NNc" / " -NNc".
        static constexpr int semitones[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int semitone = semitones[first - 'A'];
        int i = 1;
        if (text.length() > i && (text[i] == '#' || text[i] == 's')) { ++semitone; ++i; }
        else if (text.length() > i && text[i] == 'b') { --semitone; ++i; }
        int sign = 1;
        if (text.length() > i && text[i] == '-') { sign = -1; ++i; }
        const int digitsStart = i;
        while (i < text.length() && juce::CharacterFunctions::isDigit (text[i])) ++i;
        if (i == digitsStart || i - digitsStart > 1) return fallback;      // exactly one octave digit
        const int octave = sign * text.substring (digitsStart, i).getIntValue();
        if (octave < -1 || octave > 9) return fallback;
        float cents = 0.0f;
        const auto rest = text.substring (i).trim();
        if (rest.isNotEmpty())
        {
            if (rest[0] != '+' && rest[0] != '-') return fallback;
            auto number = rest.substring (1).trim();
            if (number.endsWithIgnoreCase ("c")) number = number.dropLastCharacters (1).trim();
            if (number.isEmpty() || ! number.containsOnly ("0123456789")) return fallback;
            cents = (rest[0] == '-' ? -1.0f : 1.0f) * number.getFloatValue();
            if (cents < -100.0f || cents > 100.0f) return fallback;
        }
        const float note = (float) ((octave + 1) * 12 + semitone) + cents / 100.0f;
        return midiNoteToHz (note);
    }
    auto number = text;
    if (number.endsWithIgnoreCase ("hz")) number = number.dropLastCharacters (2).trim();
    number = number.replaceCharacter (',', '.');
    float value = 0.0f;
    if (! parseStrictFloat (number, value) || ! (value > 0.0f)) return fallback;
    return value;
}

float parseNumberText (const juce::String& raw, float fallback)
{
    auto text = raw.trim();
    for (const char* unit : { "ms", "%", "x", "hz", "Hz", "s" })
        if (text.endsWithIgnoreCase (unit)) { text = text.dropLastCharacters ((int) juce::String (unit).length()).trim(); break; }
    text = text.replaceCharacter (',', '.');
    float v = 0.0f;
    return parseStrictFloat (text, v) ? v : fallback;
}

juce::String formatHz (float hz, int decimals)
{
    return juce::String (hz, hz < 100.0f ? decimals : (hz < 1000.0f ? 1 : 0)) + " Hz";
}

juce::String noteNameForHz (float hz)
{
    if (! (hz > 0.0f)) return {};
    const float note = hzToMidiNote (hz);
    const int nearest = (int) std::lround (note);
    const int cents = (int) std::lround ((note - (float) nearest) * 100.0f);
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = nearest / 12 - 1;
    const int idx = ((nearest % 12) + 12) % 12;
    juce::String s = juce::String (names[idx]) + juce::String (octave);
    if (cents != 0) s += (cents > 0 ? " +" : " ") + juce::String (cents) + "c";
    return s;
}

juce::String noteNameForMidi (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = juce::jlimit (0, 127, note);
    return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto hzText = [] (float v, int) { return formatHz (v); };
    auto hzFromText = [] (const juce::String& t) { return parseFrequencyText (t, 250.0f); };
    auto hzFromTextEnd = [] (const juce::String& t) { return parseFrequencyText (t, 55.0f); };
    auto msText = [] (float v, int) { return juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms"; };
    auto msFromText = [] (const juce::String& t) { return parseNumberText (t, 0.0f); };
    auto pctText = [] (float v, int) { return juce::String (v, 0) + " %"; };
    auto pctFromText = [] (const juce::String& t) { return parseNumberText (t, 0.0f); };

    layout.add (makeFloat (startFreq, "Start Frequency", logRange (Limits::minHz, Limits::maxStartHz), 250.0f, hzText, hzFromText));
    layout.add (makeFloat (endFreq, "End Frequency", logRange (Limits::minHz, Limits::maxEndHz), 55.0f, hzText, hzFromTextEnd));
    layout.add (makeFloat (sweep, "Sweep Time", Range (0.0f, 250.0f, 0.0f), 47.0f, msText, msFromText));
    layout.add (makeFloat (hold, "Hold Time", Range (0.0f, 1000.0f, 0.0f), 132.0f, msText, msFromText));
    layout.add (makeFloat (fade, "Fade Out", Range (0.0f, 100.0f, 0.0f), 50.0f, pctText, pctFromText));
    layout.add (makeFloat (attack, "Attack", Range (0.4f, 50.0f, 0.0f, 0.4f), 0.4f, msText, msFromText));
    layout.add (makeFloat (curve, "Sweep Curve", logRange (Limits::minSlope, Limits::maxSlope), 1.0f,
                           [] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1); },
                           [] (const juce::String& t) { return parseNumberText (t, 1.0f); }));
    layout.add (makeFloat (shape, "Shape", Range (0.0f, 100.0f, 0.0f), 0.0f,
                           [] (float v, int) { return juce::String (v, 0) + " %"; }, pctFromText));
    layout.add (makeFloat (drive, "Drive", Range (0.0f, 4.0f, 0.0f), 1.0f,
                           [] (float v, int) { return juce::String (v, 2) + " x"; },
                           [] (const juce::String& t) { return parseNumberText (t, 1.0f); }));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { velocity, versionHint }, "Velocity Sensitivity", false));   // v1.3: on/off (was 0-100 %); v1.5: Off by default
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pitchSource, versionHint }, "Pitch Source",
                                                              juce::StringArray { "Fixed", "MIDI Note" }, 0));   // v1.1: Fixed first and default
    juce::StringArray channels { "Omni" };
    for (int i = 1; i <= 16; ++i) channels.add (juce::String (i));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { midiChannel, versionHint }, "MIDI Channel", channels, 0));
    return layout;
}

void Refs::attach (juce::AudioProcessorValueTreeState& apvts)
{
    startFreq = apvts.getRawParameterValue (params::startFreq);
    endFreq = apvts.getRawParameterValue (params::endFreq);
    sweep = apvts.getRawParameterValue (params::sweep);
    hold = apvts.getRawParameterValue (params::hold);
    fade = apvts.getRawParameterValue (params::fade);
    attack = apvts.getRawParameterValue (params::attack);
    curve = apvts.getRawParameterValue (params::curve);
    shape = apvts.getRawParameterValue (params::shape);
    drive = apvts.getRawParameterValue (params::drive);
    velocity = apvts.getRawParameterValue (params::velocity);
    pitchSource = apvts.getRawParameterValue (params::pitchSource);
    midiChannel = apvts.getRawParameterValue (params::midiChannel);
    jassert (startFreq && endFreq && sweep && hold && fade && attack && curve && shape && drive && velocity && pitchSource && midiChannel);
}

KickParams Refs::read() const noexcept
{
    KickParams p;
    p.startHz = startFreq->load (std::memory_order_relaxed);
    p.endHz = endFreq->load (std::memory_order_relaxed);
    p.sweepSec = sweep->load (std::memory_order_relaxed) / 1000.0f;
    p.holdSec = hold->load (std::memory_order_relaxed) / 1000.0f;
    p.fadeFraction = fade->load (std::memory_order_relaxed) / 100.0f;
    p.attackSec = attack->load (std::memory_order_relaxed) / 1000.0f;
    p.slope = curve->load (std::memory_order_relaxed);
    p.morph = shape->load (std::memory_order_relaxed) / 100.0f;
    p.gain = drive->load (std::memory_order_relaxed);
    p.velocitySensitive = velocity->load (std::memory_order_relaxed) >= 0.5f;
    p.pitchSource = pitchSource->load (std::memory_order_relaxed) >= 0.5f ? PitchSource::midiNote : PitchSource::fixed;
    return p.sanitized();
}

int Refs::midiChannelFilter() const noexcept
{
    const int v = (int) std::lround (midiChannel->load (std::memory_order_relaxed));
    return juce::jlimit (0, 16, v);
}

void applyToParameter (juce::RangedAudioParameter& parameter, float value, bool notifyHost)
{
    if (! std::isfinite (value)) return;
    const float normalised = parameter.convertTo0to1 (juce::jlimit (parameter.getNormalisableRange().start,
                                                                     parameter.getNormalisableRange().end, value));
    if (notifyHost)
        parameter.setValueNotifyingHost (normalised);
    else
        parameter.setValue (normalised);
}

KickParams paramsFromDisplayArray (const DisplayValues& values)
{
    auto get = [&] (const char* id)
    {
        for (size_t i = 0; i < synthesisIds.size(); ++i)
            if (std::strcmp (synthesisIds[i], id) == 0) return values[i];
        return 0.0f;
    };
    KickParams p;
    p.startHz = get (startFreq);
    p.endHz = get (endFreq);
    p.sweepSec = get (sweep) / 1000.0f;        // the same divisions as the APVTS path: exact roundtrip
    p.holdSec = get (hold) / 1000.0f;
    p.fadeFraction = get (fade) / 100.0f;
    p.attackSec = get (attack) / 1000.0f;
    p.slope = get (curve);
    p.morph = get (shape) / 100.0f;
    p.gain = get (drive);
    p.velocitySensitive = get (velocity) >= 0.5f;
    p.pitchSource = get (pitchSource) >= 0.5f ? PitchSource::midiNote : PitchSource::fixed;
    return p.sanitized();
}

DisplayValues displayArrayFromParams (const KickParams& raw)
{
    const KickParams p = raw.sanitized();
    DisplayValues v {};
    auto set = [&] (const char* id, float value)
    {
        for (size_t i = 0; i < synthesisIds.size(); ++i)
            if (std::strcmp (synthesisIds[i], id) == 0) v[i] = value;
    };
    set (startFreq, p.startHz);
    set (endFreq, p.endHz);
    set (sweep, p.sweepSec * 1000.0f);
    set (hold, p.holdSec * 1000.0f);
    set (fade, p.fadeFraction * 100.0f);
    set (attack, p.attackSec * 1000.0f);
    set (curve, p.slope);
    set (shape, p.morph * 100.0f);
    set (drive, p.gain);
    set (velocity, p.velocitySensitive ? 1.0f : 0.0f);
    set (pitchSource, p.pitchSource == PitchSource::midiNote ? 1.0f : 0.0f);
    return v;
}

KickParams paramsFromDisplayValues (const juce::AudioProcessorValueTreeState& apvts)
{
    auto get = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    KickParams p;
    p.startHz = get (startFreq);
    p.endHz = get (endFreq);
    p.sweepSec = get (sweep) / 1000.0f;
    p.holdSec = get (hold) / 1000.0f;
    p.fadeFraction = get (fade) / 100.0f;
    p.attackSec = get (attack) / 1000.0f;
    p.slope = get (curve);
    p.morph = get (shape) / 100.0f;
    p.gain = get (drive);
    p.velocitySensitive = get (velocity) >= 0.5f;
    p.pitchSource = get (pitchSource) >= 0.5f ? PitchSource::midiNote : PitchSource::fixed;
    return p.sanitized();
}

void setDisplayValuesFromParams (juce::AudioProcessorValueTreeState& apvts, const KickParams& raw, bool notifyHost, bool withGestures)
{
    const KickParams p = raw.sanitized();
    const bool gestures = notifyHost && withGestures;
    auto set = [&] (const char* id, float v)
    {
        if (auto* param = apvts.getParameter (id))
        {
            if (gestures) param->beginChangeGesture();
            applyToParameter (*param, v, notifyHost);
            if (gestures) param->endChangeGesture();
        }
    };
    set (startFreq, p.startHz);
    set (endFreq, p.endHz);
    set (sweep, p.sweepSec * 1000.0f);
    set (hold, p.holdSec * 1000.0f);
    set (fade, p.fadeFraction * 100.0f);
    set (attack, p.attackSec * 1000.0f);
    set (curve, p.slope);
    set (shape, p.morph * 100.0f);
    set (drive, p.gain);
    set (velocity, p.velocitySensitive ? 1.0f : 0.0f);
    set (pitchSource, p.pitchSource == PitchSource::midiNote ? 1.0f : 0.0f);
}

} // namespace kcf::params
