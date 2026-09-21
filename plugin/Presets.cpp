// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Presets.h"
#include "Parameters.h"
#include "KcfAssets.h"

#include <algorithm>
#include <cstring>

namespace kcf::presets
{

namespace
{
    const char* const rootTag = "KickCrafterPreset";

    juce::String numberText (float v)
    {
        // Shortest decimal that reads back to the same float (7 significant digits cover float32).
        return juce::String (v, 7).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    }

    std::vector<Preset> loadFactoryBank()
    {
        // BinaryData resources are named after their files ("_01reference_xml"); the
        // numeric prefix of the files fixes the bank order.
        std::vector<std::pair<juce::String, juce::String>> files;   // (original file name, xml)
        for (int i = 0; i < KcfAssets::namedResourceListSize; ++i)
        {
            const juce::String original (KcfAssets::originalFilenames[i]);
            if (! original.endsWithIgnoreCase (".xml")) continue;
            int size = 0;
            const char* data = KcfAssets::getNamedResource (KcfAssets::namedResourceList[i], size);
            if (data == nullptr || size <= 0) continue;
            files.emplace_back (original, juce::String::fromUTF8 (data, size));
        }
        std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<Preset> bank;
        for (const auto& f : files)
        {
            Preset p;
            juce::String error;
            if (fromXml (f.second, p, error))
            {
                p.kind = Kind::factory;
                bank.push_back (p);
            }
            else
            {
                jassertfalse;   // an embedded factory preset must parse; a bad file is dropped, never half-applied
            }
        }
        if (bank.empty())       // never an empty bank: the processor's default identity is factory()[0]
        {
            Preset fallback;
            fallback.name = "Reference";
            fallback.kind = Kind::factory;
            fallback.params = KickParams {};
            bank.push_back (fallback);
        }
        return bank;
    }
}

const std::vector<Preset>& factory()
{
    static const std::vector<Preset> bank = loadFactoryBank();
    return bank;
}

int count() { return (int) factory().size(); }

const Preset* findFactory (const juce::String& name)
{
    for (const auto& p : factory())
        if (p.name == name) return &p;
    return nullptr;
}

bool isValidName (const juce::String& name)
{
    const auto t = name.trim();
    if (t.isEmpty() || t.length() > maxNameLength || t != name) return false;
    for (auto c : t)
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return false;
    return true;
}

juce::String fileNameFor (const juce::String& name)
{
    juce::String out;
    for (auto c : name)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_' || c == ' ' || c == '.' || c == '+')
            out << juce::String::charToString (c);
        else
            out << '_';
    }
    out = out.trim();
    if (out.isEmpty() || out.startsWith (".")) out = "preset" + out;
    return out + ".xml";
}

juce::String toXml (const juce::String& name, const KickParams& params)
{
    juce::XmlElement root (rootTag);
    root.setAttribute ("version", schemaVersion);
    root.setAttribute ("name", name);
    auto* values = root.createNewChildElement ("Params");
    const auto display = params::displayArrayFromParams (params);
    for (size_t i = 0; i < params::synthesisIds.size(); ++i)
        values->setAttribute (params::synthesisIds[i], numberText (display[i]));
    return root.toString (juce::XmlElement::TextFormat().withoutHeader());
}

bool fromXml (const juce::String& xml, Preset& out, juce::String& error)
{
    std::unique_ptr<juce::XmlElement> root (juce::XmlDocument::parse (xml));
    if (root == nullptr || ! root->hasTagName (rootTag)) { error = "not a KickCrafter preset"; return false; }
    const auto version = root->getStringAttribute ("version").trim();
    if (version.isEmpty() || ! version.containsOnly ("0123456789") || version.getIntValue() < 1) { error = "missing or invalid version"; return false; }
    const auto name = root->getStringAttribute ("name");
    if (! isValidName (name)) { error = "missing or invalid name"; return false; }
    auto* values = root->getChildByName ("Params");
    if (values == nullptr) { error = "missing Params"; return false; }
    params::DisplayValues display {};
    const bool legacyVelocity = version.getIntValue() <= 3;   // 0-100 % sensitivity amount -> on/off (v1.3)
    for (size_t i = 0; i < params::synthesisIds.size(); ++i)
    {
        const char* id = params::synthesisIds[i];
        if (! values->hasAttribute (id)) { error = juce::String ("missing parameter ") + id; return false; }
        float v = 0.0f;
        if (! params::parseStrictFloat (values->getStringAttribute (id), v)) { error = juce::String ("invalid value for ") + id; return false; }
        if (legacyVelocity && std::strcmp (id, params::velocity) == 0) v = v > 0.0f ? 1.0f : 0.0f;
        display[i] = v;
    }
    out.name = name;
    out.params = params::paramsFromDisplayArray (display);   // clamped into the engine bounds
    out.kind = Kind::user;
    out.file = juce::File();
    error.clear();
    return true;
}

// ---------------------------------------------------------------- Library --

juce::File Library::defaultDirectory()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("KCF_PRESET_DIR", {});
    if (env.isNotEmpty()) return juce::File (env);
    // Linux: $XDG_CONFIG_HOME or ~/.config; Windows: %APPDATA%; macOS: ~/Library, where JUCE leaves
    // out the "Application Support" component that Apple's convention requires.
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return resolveDefaultDirectory (base);
}

juce::File Library::resolveDefaultDirectory (const juce::File& base)
{
    const auto current = base.getChildFile ("KickCrafter").getChildFile ("Presets");
    const auto legacy = base.getChildFile ("KickCrafterFable").getChildFile ("Presets");   // the folder before 1.5.0
    if (! current.exists() && legacy.isDirectory())
        if (! (current.getParentDirectory().createDirectory() && legacy.moveFileTo (current)))
            return legacy;                                    // the move failed: the old library stays in use
    return current;
}

Library::Library (const juce::File& directory) : dir (directory)
{
    rescan();
}

void Library::rescan()
{
    items.clear();
    skipped = 0;
    reasons.clear();
    if (! dir.isDirectory()) return;
    juce::Array<juce::File> files = dir.findChildFiles (juce::File::findFiles, false, "*.xml");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    for (const auto& file : files)
    {
        Preset p;
        juce::String error;
        if (file.getSize() > 1 << 20) { ++skipped; reasons.add (file.getFileName() + ": too large"); continue; }
        if (! fromXml (file.loadFileAsString(), p, error)) { ++skipped; reasons.add (file.getFileName() + ": " + error); continue; }
        if (find (p.name) != nullptr) { ++skipped; reasons.add (file.getFileName() + ": duplicate name '" + p.name + "'"); continue; }
        p.kind = Kind::user;
        p.file = file;
        items.push_back (p);
    }
    std::sort (items.begin(), items.end(), [] (const Preset& a, const Preset& b) { return a.name.compareIgnoreCase (b.name) < 0; });
}

const Preset* Library::find (const juce::String& name) const
{
    for (const auto& p : items)
        if (p.name == name) return &p;
    return nullptr;
}

bool Library::save (const juce::String& name, const KickParams& params, juce::String& error)
{
    if (! isValidName (name)) { error = "invalid preset name"; return false; }
    if (! dir.isDirectory() && ! dir.createDirectory()) { error = "cannot create " + dir.getFullPathName(); return false; }
    rescan();                                                             // decide overwrite/collision on the disk as it is now
    juce::File target = dir.getChildFile (fileNameFor (name));
    if (const auto* existing = find (name)) target = existing->file;    // overwrite in place, whatever the file name
    else if (target.existsAsFile())                                       // a different preset already uses that file name
        target = dir.getNonexistentChildFile (target.getFileNameWithoutExtension(), ".xml");
    if (! target.replaceWithText (toXml (name, params))) { error = "cannot write " + target.getFullPathName(); return false; }
    rescan();
    error.clear();
    return true;
}

bool Library::rename (const juce::String& oldName, const juce::String& newName, juce::String& error)
{
    rescan();
    const auto* existing = find (oldName);
    if (existing == nullptr) { error = "no preset named '" + oldName + "'"; return false; }
    if (! isValidName (newName)) { error = "invalid preset name"; return false; }
    if (newName != oldName && find (newName) != nullptr) { error = "a preset named '" + newName + "' already exists"; return false; }
    const juce::File oldFile = existing->file;
    const KickParams params = existing->params;
    juce::File target = dir.getChildFile (fileNameFor (newName));
    if (target != oldFile && target.existsAsFile())
        target = dir.getNonexistentChildFile (target.getFileNameWithoutExtension(), ".xml");
    if (! target.replaceWithText (toXml (newName, params))) { error = "cannot write " + target.getFullPathName(); return false; }
    if (target != oldFile && ! oldFile.deleteFile()) { error = "cannot delete " + oldFile.getFullPathName(); return false; }
    rescan();
    error.clear();
    return true;
}

bool Library::remove (const juce::String& name, juce::String& error)
{
    rescan();
    const auto* existing = find (name);
    if (existing == nullptr) { error = "no preset named '" + name + "'"; return false; }
    const juce::File file = existing->file;
    if (! file.deleteFile()) { error = "cannot delete " + file.getFullPathName(); return false; }
    rescan();
    error.clear();
    return true;
}

} // namespace kcf::presets
