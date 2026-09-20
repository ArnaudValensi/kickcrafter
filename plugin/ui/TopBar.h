// Restrained top bar: wordmark, presets (factory + user library), A/B, audition,
// voice/LED feedback, UI scale.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Presets.h"

namespace kcf::ui
{

// Preset combo whose popup is built by the owner (sections: Factory / User) and always
// applies the picked item, even when it is the item already selected (e.g. "Reference •
// edited": the user expects the preset to reload; the stock ComboBox only notifies when
// the selected id changes, so such a click flashed the item and did nothing).
class PresetBox final : public juce::ComboBox
{
public:
    std::function<void (juce::PopupMenu&)> buildMenu;    // fills the popup (ids: see TopBar)
    std::function<void (int itemId)> onPick;             // popup result (mouse) and keyboard changes
    void showPopup() override;
};

class TopBar final : public juce::Component
{
public:
    explicit TopBar (KickCrafterProcessor&);
    ~TopBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // Called by the editor timer: refreshes preset name, A/B state, LED and voice count.
    void refresh (float ledLevel, int activeVoices, int lastNote, int lastVelocity);

    std::function<void (int percent)> onScaleSelected;

    juce::TextButton& getAuditionButton() noexcept { return audition; }
    juce::ComboBox& getPresetBox() noexcept { return presetBox; }
    juce::TextButton& getPresetMenuButton() noexcept { return presetMenuButton; }

    // Preset actions (message thread). The menu's dialogs end up calling these; tests
    // call them directly. Each returns false and reports the reason through
    // `lastError` when it fails (invalid name, file error, nothing to act on).
    presets::Library& getLibrary() noexcept { return library; }
    void rescanLibrary();
    bool saveCurrent();                                       // overwrite the loaded user preset (factory/missing -> false)
    bool saveCurrentAs (const juce::String& name);            // create or overwrite a user preset with the current values
    bool renameCurrent (const juce::String& newName);         // user preset only
    bool deleteCurrent();                                     // user preset only; the identity stays ("missing")
    void selectPreviousPreset();
    void selectNextPreset();
    bool selectPresetByName (presets::Kind kind, const juce::String& name);
    void buildPresetMenu (juce::PopupMenu& menu);             // what the popup shows (ids: factory 1..N, user 1001..)
    juce::String getLastError() const { return lastError; }
    static constexpr int userItemBase = 1001;
    static constexpr int skippedInfoItemId = 999;             // disabled popup row: "N file(s) skipped"

    // The "..." actions menu. `performPresetAction` is what a picked menu row runs; the
    // tests call it directly and then drive the dialogs it opens (name prompt:
    // `getOpenDialog()`; confirmations/errors: the AlertWindow on the desktop titled
    // "Overwrite preset" / "Delete preset" / "Preset").
    enum PresetAction { actionSave = 1, actionSaveAs, actionRename, actionDelete, actionOpenFolder, actionRescan };
    void performPresetAction (int action);
    juce::AlertWindow* getOpenDialog() const noexcept { return dialog.get(); }

private:
    void pickPresetItem (int itemId);
    void showPresetActionsMenu();
    // Dialogs are asynchronous and owned here: `dialog` (name prompt) and `messageBox` /
    // `errorBox` (scoped confirmation / error boxes) are dismissed by ~TopBar, and every
    // callback re-checks a SafePointer to the bar, so closing the editor while a dialog
    // is open can never reach a destroyed bar.
    void promptForName (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> onOk);
    void confirm (const juce::String& title, const juce::String& message, std::function<void()> onYes);
    void reportError (const juce::String& what);
    juce::String currentDisplayName() const;

    KickCrafterProcessor& processor;
    presets::Library library;
    PresetBox presetBox;
    juce::TextButton previousPreset { "<" }, nextPreset { ">" };
    juce::TextButton presetMenuButton { "..." };
    juce::TextButton slotA { "A" }, slotB { "B" }, copySlot { "Copy" };
    juce::TextButton audition { "Audition" };
    juce::TextButton scaleButton { "100 %" };
    std::unique_ptr<juce::AlertWindow> dialog;
    int dialogSerial = 0;                                   // identifies the prompt a modal callback belongs to
    juce::ScopedMessageBox messageBox, errorBox;
    juce::String lastError;
    float led = 0.0f;
    int voices = 0;
    int lastNote = -1;
    int lastVelocity = 0;
};

} // namespace kcf::ui
