// Presets: the factory bank embedded from resources/presets/*.xml and the user
// library on disk (~/.config/KickCrafter/Presets, or $KCF_PRESET_DIR). One
// text format for both. Loading a preset only changes the parameter model, so it
// affects subsequent hits only (per-note snapshot rule). Everything here runs on
// the message thread (or test threads); nothing is called from the audio thread.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include "engine/KickParams.h"

#include <vector>

namespace kcf::presets
{

enum class Kind { factory, user };

struct Preset
{
    juce::String name;
    Kind kind = Kind::factory;
    KickParams params;
    juce::File file;            // user presets only
};

inline constexpr int schemaVersion = 4;           // <KickCrafterPreset version="4" name="..."><Params .../>; 4 (v1.3): velocity 0/1 (<= 3: 0-100 % amount, > 0 -> on)
inline constexpr int maxNameLength = 64;

// Factory bank (embedded XML, fixed order). `count()` is its size.
const std::vector<Preset>& factory();
int count();
const Preset* findFactory (const juce::String& name);

// Text format. `fromXml` is strict: root tag, positive integer version, non-empty
// name, every synthesis parameter present as a finite decimal token (values are then
// clamped by the engine); anything else is rejected and `error` says why.
juce::String toXml (const juce::String& name, const KickParams& params);
bool fromXml (const juce::String& xml, Preset& out, juce::String& error);

// Preset names: trimmed, 1..64 characters, no control characters, no path separators.
bool isValidName (const juce::String& name);
juce::String fileNameFor (const juce::String& name);   // "<sanitised>.xml"

// User library: one XML file per preset in one directory, scanned on demand.
class Library
{
public:
    static juce::File defaultDirectory();             // $KCF_PRESET_DIR or ~/.config/KickCrafter/Presets
    // <base>/KickCrafter/Presets; a library left by 1.0 to 1.4 under <base>/KickCrafterFable/Presets
    // is moved there once (and stays in use if the move fails). Exposed for the tests.
    static juce::File resolveDefaultDirectory (const juce::File& base);
    explicit Library (const juce::File& directory = defaultDirectory());

    const juce::File& directory() const noexcept { return dir; }
    const std::vector<Preset>& presets() const noexcept { return items; }   // sorted by name (case-insensitive)
    int skippedFiles() const noexcept { return skipped; }                   // unreadable/malformed files of the last scan
    const juce::StringArray& skippedReasons() const noexcept { return reasons; }

    void rescan();                                                            // creates nothing; missing dir = empty library
    const Preset* find (const juce::String& name) const;

    // All three return false and set `error` on invalid names, I/O failures or (rename) name collisions.
    bool save (const juce::String& name, const KickParams& params, juce::String& error);   // create or overwrite
    bool rename (const juce::String& oldName, const juce::String& newName, juce::String& error);
    bool remove (const juce::String& name, juce::String& error);

private:
    juce::File dir;
    std::vector<Preset> items;
    int skipped = 0;
    juce::StringArray reasons;
};

} // namespace kcf::presets
