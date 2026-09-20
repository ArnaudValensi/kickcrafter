// Parameter model: the single source of truth shared by UI, host automation and state.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/KickParams.h"

#include <array>
#include <atomic>

namespace kcf::params
{

// Persistent parameter IDs. Never rename: VST3 IDs are derived from them.
inline constexpr const char* startFreq   = "startFreq";
inline constexpr const char* endFreq     = "endFreq";
inline constexpr const char* sweep       = "sweep";
inline constexpr const char* hold        = "hold";
inline constexpr const char* fade        = "fade";
inline constexpr const char* attack      = "attack";
inline constexpr const char* curve       = "curve";
inline constexpr const char* shape       = "shape";
inline constexpr const char* drive       = "drive";
inline constexpr const char* velocity    = "velocity";
inline constexpr const char* pitchSource = "pitchSource";
inline constexpr const char* midiChannel = "midiChannel";

inline constexpr int versionHint = 1;

// All synthesis parameters that are frozen per note (everything except midiChannel).
inline constexpr std::array<const char*, 11> synthesisIds {
    startFreq, endFreq, sweep, hold, fade, attack, curve, shape, drive, velocity, pitchSource
};
inline constexpr std::array<const char*, 12> allIds {
    startFreq, endFreq, sweep, hold, fade, attack, curve, shape, drive, velocity, pitchSource, midiChannel
};

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Cached atomic pointers for the audio thread (no string lookups per block).
struct Refs
{
    void attach (juce::AudioProcessorValueTreeState& apvts);
    KickParams read() const noexcept;
    int midiChannelFilter() const noexcept;   // 0 = omni, 1..16

    std::atomic<float>* startFreq = nullptr;
    std::atomic<float>* endFreq = nullptr;
    std::atomic<float>* sweep = nullptr;
    std::atomic<float>* hold = nullptr;
    std::atomic<float>* fade = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* curve = nullptr;
    std::atomic<float>* shape = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* velocity = nullptr;
    std::atomic<float>* pitchSource = nullptr;
    std::atomic<float>* midiChannel = nullptr;
};

// Denormalised value of a parameter, with the KickParams unit convention
// (seconds, fractions) rather than the display units (ms, %).
void applyToParameter (juce::RangedAudioParameter& parameter, float denormalisedDisplayValue, bool notifyHost);
juce::String noteNameForHz (float hz);            // e.g. "A1 +3c" (C4 = MIDI 60 convention)
juce::String noteNameForMidi (int note);          // same convention: 33 -> "A1"
juce::String formatHz (float hz, int decimals = 1);

// True only for a complete decimal token: [+-]digits[.digits][e[+-]digits] (or .digits).
bool isStrictNumberToken (const juce::String& text);
// Parses a strict token into a finite float; returns false for junk, non-finite or
// values outside the float range (representability policy: reject, never clamp).
bool parseStrictFloat (const juce::String& text, float& out);

// Text entry parsers. Return `fallback` (pass NaN to detect rejection) for anything
// that is not a complete, valid token: "55", "55.5 Hz", "A1", "c#2", "Bb1 +10c", "A1 -25c".
float parseFrequencyText (const juce::String& text, float fallback);
float parseNumberText (const juce::String& text, float fallback);   // "12.5", "12.5 ms", "50 %" 

// Maps between KickParams (engine units) and the parameter display units.
// DisplayValues holds the synthesis parameters in display units, in synthesisIds order
// (the same array the A/B slots and the preset files use).
using DisplayValues = std::array<float, synthesisIds.size()>;
KickParams paramsFromDisplayArray (const DisplayValues& values);
DisplayValues displayArrayFromParams (const KickParams& params);
KickParams paramsFromDisplayValues (const juce::AudioProcessorValueTreeState& apvts);
void setDisplayValuesFromParams (juce::AudioProcessorValueTreeState& apvts, const KickParams& params, bool notifyHost, bool withGestures = true);

} // namespace kcf::params
