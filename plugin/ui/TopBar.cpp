// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TopBar.h"
#include "plugin/Parameters.h"

namespace kcf::ui
{

namespace
{
    const juce::String editedMarker = juce::String::fromUTF8 ("  \xe2\x80\xa2 edited");
    const juce::String missingMarker = " (missing)";
}

void PresetBox::showPopup()
{
    // Reached through ComboBox::showPopupIfNotActive() for every mouse/keyboard path, which has
    // already set the (private) menuActive flag. The only other caller is the accessibility
    // "show menu" action, which bypasses that flag; no accessibility client is in scope here,
    // so that gap is accepted rather than worked around.
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    if (buildMenu) buildMenu (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withStandardItemHeight (juce::jmax (18, getHeight() - 6))
                            .withInitiallySelectedItem (getSelectedId())
                            .withItemThatMustBeVisible (getSelectedId()),
                        [safe = juce::Component::SafePointer<PresetBox> (this)] (int result)
                        {
                            if (safe == nullptr) return;
                            safe->hidePopup();
                            if (auto* handler = safe->getAccessibilityHandler()) handler->grabFocus();
                            if (result > 0 && safe->onPick) safe->onPick (result);
                        });
}

TopBar::TopBar (KickCrafterProcessor& p) : processor (p), library (presets::Library::defaultDirectory())
{
    presetBox.setTooltip ("Factory and user presets. Loading one changes the parameters for the next hits only.");
    presetBox.setComponentID ("presetBox");
    presetBox.buildMenu = [this] (juce::PopupMenu& menu) { buildPresetMenu (menu); };
    presetBox.onPick = [this] (int id) { pickPresetItem (id); };
    presetBox.onChange = [this] { pickPresetItem (presetBox.getSelectedId()); };   // keyboard / programmatic
    for (int i = 0; i < presets::count(); ++i)                                   // items for keyboard navigation and text
        presetBox.addItem (presets::factory()[(size_t) i].name, i + 1);
    addAndMakeVisible (presetBox);

    previousPreset.setTooltip ("Previous preset");
    previousPreset.onClick = [this] { selectPreviousPreset(); };
    nextPreset.setTooltip ("Next preset");
    nextPreset.onClick = [this] { selectNextPreset(); };
    addAndMakeVisible (previousPreset);
    addAndMakeVisible (nextPreset);

    presetMenuButton.setTooltip ("Preset actions: save, save as, rename, delete, open the user preset folder, rescan.");
    presetMenuButton.setComponentID ("presetMenu");
    presetMenuButton.onClick = [this] { showPresetActionsMenu(); };
    addAndMakeVisible (presetMenuButton);

    slotA.setClickingTogglesState (false);
    slotB.setClickingTogglesState (false);
    slotA.setTooltip ("A/B comparison: switch to slot A. Only new hits use the selected slot.");
    slotB.setTooltip ("A/B comparison: switch to slot B. Only new hits use the selected slot.");
    slotA.setComponentID ("slotA");
    slotB.setComponentID ("slotB");
    slotA.onClick = [this] { if (processor.getABSlot() != 0) processor.toggleAB(); };
    slotB.onClick = [this] { if (processor.getABSlot() != 1) processor.toggleAB(); };
    copySlot.setTooltip ("Copy the current settings into the other slot.");
    copySlot.setComponentID ("copySlot");
    copySlot.onClick = [this] { processor.copyCurrentToOtherSlot(); };
    addAndMakeVisible (slotA);
    addAndMakeVisible (slotB);
    addAndMakeVisible (copySlot);

    audition.setTooltip ("Trigger a test hit (A1, full velocity) through the realtime event path. Not an automatable parameter.");
    audition.setComponentID ("audition");
    audition.setColour (juce::TextButton::buttonColourId, Palette::copperDeep);
    audition.onClick = [this] { processor.triggerAudition(); };
    addAndMakeVisible (audition);

    scaleButton.setTooltip ("Window size. The layout scales as vector graphics; the host window can also be resized.");
    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        for (int percent : { 80, 100, 125, 150 })
            menu.addItem (percent, juce::String (percent) + " %", true, processor.getUIScalePercent() == percent);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scaleButton), [this] (int result)
        {
            if (result > 0 && onScaleSelected) onScaleSelected (result);
        });
    };
    addAndMakeVisible (scaleButton);
}

TopBar::~TopBar()
{
    dialog.reset();          // an open name prompt must not outlive the bar
    messageBox.close();      // nor a confirmation / error box (their callbacks are dropped)
    errorBox.close();
}

// ---------------------------------------------------------------- presets --

void TopBar::buildPresetMenu (juce::PopupMenu& menu)
{
    library.rescan();                                    // files may have been edited by hand
    const auto loaded = processor.getLoadedPreset();
    menu.addSectionHeader ("Factory");
    for (int i = 0; i < presets::count(); ++i)
        menu.addItem (i + 1, presets::factory()[(size_t) i].name, true,
                      loaded.kind == presets::Kind::factory && loaded.factoryIndex == i);
    if (! library.presets().empty() || library.skippedFiles() > 0)
    {
        menu.addSectionHeader ("User");
        int id = userItemBase;
        for (const auto& u : library.presets())
            menu.addItem (id++, u.name, true, loaded.kind == presets::Kind::user && loaded.name == u.name);
        if (library.skippedFiles() > 0)     // files that did not load are not silently invisible
            menu.addItem (skippedInfoItemId, juce::String (library.skippedFiles()) + " file(s) skipped (see \"...\" > Rescan)", false);
    }
}

void TopBar::pickPresetItem (int itemId)
{
    if (itemId >= 1 && itemId <= presets::count())
    {
        const int index = itemId - 1;
        const auto loaded = processor.getLoadedPreset();
        const bool same = loaded.kind == presets::Kind::factory && loaded.factoryIndex == index;
        if (! same || ! processor.currentValuesMatchLoadedPreset())
            processor.loadFactoryPreset (index);
    }
    else if (itemId >= userItemBase && itemId - userItemBase < (int) library.presets().size())
    {
        const auto& u = library.presets()[(size_t) (itemId - userItemBase)];
        const auto loaded = processor.getLoadedPreset();
        const bool same = loaded.kind == presets::Kind::user && loaded.name == u.name;
        if (! same || ! processor.currentValuesMatchLoadedPreset())
            processor.loadPreset (u);
    }
}

bool TopBar::selectPresetByName (presets::Kind kind, const juce::String& name)
{
    if (kind == presets::Kind::factory)
    {
        if (const auto* f = presets::findFactory (name)) { processor.loadPreset (*f); return true; }
    }
    else if (const auto* u = library.find (name)) { processor.loadPreset (*u); return true; }
    lastError = "no preset named '" + name + "'";
    return false;
}

void TopBar::selectPreviousPreset()
{
    // Combined order: factory bank, then user presets (by name). Wraps around.
    const int total = presets::count() + (int) library.presets().size();
    if (total <= 0) return;
    const auto loaded = processor.getLoadedPreset();
    int position = 0;
    if (loaded.kind == presets::Kind::factory) position = juce::jlimit (0, presets::count() - 1, loaded.factoryIndex);
    else
    {
        position = presets::count();   // a missing user preset counts as the first user slot
        for (size_t i = 0; i < library.presets().size(); ++i)
            if (library.presets()[i].name == loaded.name) position = presets::count() + (int) i;
    }
    const int target = (position + total - 1) % total;
    pickPresetItem (target < presets::count() ? target + 1 : userItemBase + target - presets::count());
}

void TopBar::selectNextPreset()
{
    const int total = presets::count() + (int) library.presets().size();
    if (total <= 0) return;
    const auto loaded = processor.getLoadedPreset();
    int position = 0;
    if (loaded.kind == presets::Kind::factory) position = juce::jlimit (0, presets::count() - 1, loaded.factoryIndex);
    else
    {
        position = presets::count() - 1;   // a missing user preset: next = first user (or wraps to factory 0)
        for (size_t i = 0; i < library.presets().size(); ++i)
            if (library.presets()[i].name == loaded.name) position = presets::count() + (int) i;
    }
    const int target = (position + 1) % total;
    pickPresetItem (target < presets::count() ? target + 1 : userItemBase + target - presets::count());
}

void TopBar::rescanLibrary()
{
    library.rescan();
    refresh (led, voices, lastNote, lastVelocity);
}

bool TopBar::saveCurrentAs (const juce::String& name)
{
    juce::String error;
    if (! library.save (name, processor.getCurrentParams(), error)) { lastError = error; return false; }
    if (const auto* saved = library.find (name))
        processor.adoptPresetIdentity (*saved);
    lastError.clear();
    refresh (led, voices, lastNote, lastVelocity);
    return true;
}

bool TopBar::saveCurrent()
{
    const auto loaded = processor.getLoadedPreset();
    if (loaded.kind != presets::Kind::user || library.find (loaded.name) == nullptr)
    {
        lastError = "not a user preset (use Save as)";
        return false;
    }
    return saveCurrentAs (loaded.name);
}

bool TopBar::renameCurrent (const juce::String& newName)
{
    const auto loaded = processor.getLoadedPreset();
    if (loaded.kind != presets::Kind::user || library.find (loaded.name) == nullptr) { lastError = "not a user preset"; return false; }
    juce::String error;
    if (! library.rename (loaded.name, newName, error)) { lastError = error; return false; }
    // Identity follows the new name; the reference values stay those of the file, so the
    // current parameters and the "edited" state are untouched.
    const auto* renamed = library.find (newName);
    if (renamed == nullptr) { lastError = "renamed preset '" + newName + "' not found after rescan"; return false; }
    processor.relabelLoadedPreset (*renamed);
    lastError.clear();
    refresh (led, voices, lastNote, lastVelocity);
    return true;
}

bool TopBar::deleteCurrent()
{
    const auto loaded = processor.getLoadedPreset();
    if (loaded.kind != presets::Kind::user || library.find (loaded.name) == nullptr) { lastError = "not a user preset"; return false; }
    juce::String error;
    if (! library.remove (loaded.name, error)) { lastError = error; return false; }
    lastError.clear();
    refresh (led, voices, lastNote, lastVelocity);   // the identity stays: the name now shows "(missing)"
    return true;
}

juce::String TopBar::currentDisplayName() const
{
    const auto loaded = processor.getLoadedPreset();
    juce::String name = loaded.name;
    if (loaded.kind == presets::Kind::user && library.find (loaded.name) == nullptr) name += missingMarker;
    if (! processor.currentValuesMatchLoadedPreset()) name += editedMarker;
    return name;
}

void TopBar::showPresetActionsMenu()
{
    const auto loaded = processor.getLoadedPreset();
    const bool userLoaded = loaded.kind == presets::Kind::user && library.find (loaded.name) != nullptr;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addItem (1, userLoaded ? "Save" : "Save (as a user preset)");
    menu.addItem (2, "Save as...");
    menu.addItem (3, "Rename...", userLoaded);
    menu.addItem (4, "Delete", userLoaded);
    menu.addSeparator();
    menu.addItem (5, "Open user presets folder");
    menu.addItem (6, "Rescan");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetMenuButton),
                        [safe = juce::Component::SafePointer<TopBar> (this)] (int result)
    {
        if (safe != nullptr && result > 0) safe->performPresetAction (result);
    });
}

void TopBar::performPresetAction (int action)
{
    // Everything below runs on the message thread, possibly long after the menu row was
    // picked: the loaded preset is re-read now, and the dialog continuations only touch
    // the bar through a SafePointer (the bar may be gone by the time a dialog closes).
    const auto loaded = processor.getLoadedPreset();
    const bool userLoaded = loaded.kind == presets::Kind::user && library.find (loaded.name) != nullptr;
    const juce::Component::SafePointer<TopBar> safe (this);
    switch (action)
    {
        case actionSave:
            if (userLoaded) { if (! saveCurrent()) reportError (lastError); }
            else promptForName ("Save preset as", loaded.name, [safe] (const juce::String& name)
                                { if (safe != nullptr && ! safe->saveCurrentAs (name)) safe->reportError (safe->lastError); });
            break;
        case actionSaveAs:
            promptForName ("Save preset as", loaded.name, [safe] (const juce::String& name)
            {
                if (safe == nullptr) return;
                safe->library.rescan();
                if (safe->library.find (name) != nullptr)
                    safe->confirm ("Overwrite preset", "A user preset named \"" + name + "\" already exists. Overwrite it?",
                                   [safe, name] { if (safe != nullptr && ! safe->saveCurrentAs (name)) safe->reportError (safe->lastError); });
                else if (! safe->saveCurrentAs (name)) safe->reportError (safe->lastError);
            });
            break;
        case actionRename:
            if (! userLoaded) { reportError ("not a user preset"); break; }
            promptForName ("Rename preset", loaded.name, [safe] (const juce::String& name)
                           { if (safe != nullptr && ! safe->renameCurrent (name)) safe->reportError (safe->lastError); });
            break;
        case actionDelete:
            if (! userLoaded) { reportError ("not a user preset"); break; }
            confirm ("Delete preset", "Delete the user preset \"" + loaded.name + "\"? The file is removed; the current values stay.",
                     [safe] { if (safe != nullptr && ! safe->deleteCurrent()) safe->reportError (safe->lastError); });
            break;
        case actionOpenFolder:
            library.directory().createDirectory();
            library.directory().revealToUser();
            break;
        case actionRescan:
            rescanLibrary();
            if (library.skippedFiles() > 0)
                reportError (juce::String (library.skippedFiles()) + " preset file(s) in " + library.directory().getFullPathName()
                             + " were skipped:\n" + library.skippedReasons().joinIntoString ("\n"));
            break;
        default: break;
    }
}

void TopBar::promptForName (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> onOk)
{
    dialog.reset();          // a previous prompt (if any) is cancelled; its pending callback sees another window and stays out
    dialog = std::make_unique<juce::AlertWindow> (title, "Preset name (1 to 64 characters):", juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("name", initial, "Name");
    dialog->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->setLookAndFeel (&getLookAndFeel());
    const int serial = ++dialogSerial;                  // not the pointer: a replacement can reuse the address
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<TopBar> (this), serial, onOk] (int result)
    {
        if (safe == nullptr) return;
        auto& bar = *safe;
        if (bar.dialogSerial != serial || bar.dialog == nullptr) return;   // callback of a prompt that was replaced
        const juce::String name = bar.dialog->getTextEditorContents ("name");
        bar.dialog.reset();
        if (result == 1 && onOk) onOk (name);
    }), false);
    dialog->toFront (true);                             // in front of the host window, with keyboard focus
    if (auto* editor = dialog->getTextEditor ("name"))  // typing replaces the prefilled name
    {
        editor->selectAll();
        editor->grabKeyboardFocus();
    }
}

void TopBar::confirm (const juce::String& title, const juce::String& message, std::function<void()> onYes)
{
    // Scoped: ~TopBar closes the box (the callback is then never run); the callback itself
    // re-checks the bar. The associated component selects the plug-in's look and feel.
    messageBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                         .withTitle (title).withMessage (message).withButton ("Yes").withButton ("No")
                                                         .withAssociatedComponent (this),
                                                     [safe = juce::Component::SafePointer<TopBar> (this), onYes] (int result)
                                                     { if (safe != nullptr && result == 1 && onYes) onYes(); });
}

void TopBar::reportError (const juce::String& what)
{
    errorBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                       .withTitle ("Preset").withMessage (what).withButton ("OK").withAssociatedComponent (this),
                                                   nullptr);
}

// ----------------------------------------------------------------- layout --

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    r.removeFromLeft (200);                                    // wordmark
    scaleButton.setBounds (r.removeFromRight (64));
    r.removeFromRight (12);
    audition.setBounds (r.removeFromRight (92));
    r.removeFromRight (150);                                   // LED + voices text
    auto centre = r;
    const int presetWidth = 260;
    auto presetArea = centre.withSizeKeepingCentre (presetWidth + 60 + 12 + 40 + 40 + 64 + 16 + 32, centre.getHeight());
    previousPreset.setBounds (presetArea.removeFromLeft (28));
    presetArea.removeFromLeft (4);
    presetBox.setBounds (presetArea.removeFromLeft (presetWidth - 60));
    presetArea.removeFromLeft (4);
    nextPreset.setBounds (presetArea.removeFromLeft (28));
    presetArea.removeFromLeft (4);
    presetMenuButton.setBounds (presetArea.removeFromLeft (28));
    presetArea.removeFromLeft (20);
    slotA.setBounds (presetArea.removeFromLeft (36));
    presetArea.removeFromLeft (4);
    slotB.setBounds (presetArea.removeFromLeft (36));
    presetArea.removeFromLeft (8);
    copySlot.setBounds (presetArea.removeFromLeft (60));
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (Palette::topBar);
    g.setColour (Palette::panelEdge);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Wordmark
    auto r = getLocalBounds().reduced (16, 0);
    g.setColour (Palette::text);
    g.setFont (font (17.0f, Weight::semibold));
    g.drawText ("KICKCRAFTER", r.removeFromLeft (128), juce::Justification::centredLeft);
    g.setColour (Palette::copper);
    g.setFont (font (11.5f, Weight::semibold));
    g.drawText ("FABLE", r.removeFromLeft (52).translated (-6, 1), juce::Justification::centredLeft);

    // LED + note/voice feedback, left of the audition button.
    auto feedback = juce::Rectangle<int> (audition.getX() - 150, 0, 138, getHeight());
    const auto ledCentre = juce::Point<float> ((float) feedback.getRight() - 8.0f, (float) getHeight() * 0.5f);
    g.setColour (Palette::led.withAlpha (0.15f + 0.55f * led));
    g.fillEllipse (ledCentre.x - 9.0f, ledCentre.y - 9.0f, 18.0f, 18.0f);
    g.setColour (Palette::led.withAlpha (0.35f + 0.65f * led));
    g.fillEllipse (ledCentre.x - 4.5f, ledCentre.y - 4.5f, 9.0f, 9.0f);
    g.setColour (Palette::textDim);
    g.setFont (font (11.5f, Weight::mono));
    juce::String text = juce::String (voices) + "/" + juce::String (Limits::maxVoices) + " voices";
    if (lastNote >= 0)
        text = params::noteNameForMidi (lastNote) + " v" + juce::String (lastVelocity) + "   " + text;
    g.drawText (text, feedback.withTrimmedRight (24), juce::Justification::centredRight);
}

void TopBar::refresh (float ledLevel, int activeVoices, int note, int velocity)
{
    bool changed = std::fabs (ledLevel - led) > 0.01f || activeVoices != voices || note != lastNote || velocity != lastVelocity;
    led = ledLevel;
    voices = activeVoices;
    lastNote = note;
    lastVelocity = velocity;

    // Selected id: factory items only live in the combo's own list (keyboard navigation);
    // a user preset leaves the id at 0 and shows its name as text.
    const auto loaded = processor.getLoadedPreset();
    const int wantedId = loaded.kind == presets::Kind::factory ? loaded.factoryIndex + 1 : 0;
    if (presetBox.getSelectedId() != wantedId)
    {
        presetBox.setSelectedId (wantedId, juce::dontSendNotification);
        changed = true;
    }
    // "Modified" / "(missing)" markers: the combo box is a child painted above the bar, so
    // the markers live in its text.
    const juce::String wanted = currentDisplayName();
    if (presetBox.getText() != wanted)
    {
        presetBox.setText (wanted, juce::dontSendNotification);
        changed = true;
    }
    const bool isA = processor.getABSlot() == 0;
    if (slotA.getToggleState() != isA || slotB.getToggleState() != ! isA)
    {
        slotA.setToggleState (isA, juce::dontSendNotification);
        slotB.setToggleState (! isA, juce::dontSendNotification);
        copySlot.setButtonText (isA ? "A>B" : "B>A");
        changed = true;
    }
    const auto scaleText = juce::String (processor.getUIScalePercent()) + " %";
    if (scaleButton.getButtonText() != scaleText)
    {
        scaleButton.setButtonText (scaleText);
        changed = true;
    }
    if (changed) repaint();
}

} // namespace kcf::ui
