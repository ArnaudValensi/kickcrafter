// Visual system: warm charcoal, copper/amber accents, IBM Plex typography.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kcf::ui
{

struct Palette
{
    static inline const juce::Colour background   { 0xff1a1917 };
    static inline const juce::Colour topBar       { 0xff15140f };
    static inline const juce::Colour panel        { 0xff211f1c };
    static inline const juce::Colour panelRaised  { 0xff2a2724 };
    static inline const juce::Colour panelEdge    { 0xff383430 };
    static inline const juce::Colour grid         { 0xff2e2b27 };
    static inline const juce::Colour gridStrong   { 0xff3b3732 };
    static inline const juce::Colour text         { 0xffede6da };
    static inline const juce::Colour textMuted    { 0xffb3a99b };
    static inline const juce::Colour textDim      { 0xff8c8377 };
    static inline const juce::Colour copper       { 0xffd9884a };
    static inline const juce::Colour copperBright { 0xfff2a86a };
    static inline const juce::Colour copperDeep   { 0xff8a4d24 };
    static inline const juce::Colour amber        { 0xffe8b86d };
    static inline const juce::Colour amberDeep    { 0xff8f6a33 };
    static inline const juce::Colour ivory        { 0xffd8cfc0 };
    static inline const juce::Colour knobFace     { 0xff2c2926 };
    static inline const juce::Colour knobRim      { 0xff413c36 };
    static inline const juce::Colour knobTrack    { 0xff35312c };
    static inline const juce::Colour led          { 0xffff9a4a };
};

enum class Weight { regular, medium, semibold, mono };

juce::Font font (float height, Weight weight = Weight::regular);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Font getPopupMenuFont() override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};

// Shared drawing helpers.
void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& title, const juce::String& caption = {});
juce::Path diamond (juce::Point<float> centre, float radius);

} // namespace kcf::ui
