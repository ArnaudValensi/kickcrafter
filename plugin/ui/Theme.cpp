// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"
#include "KcfAssets.h"

namespace kcf::ui
{

namespace
{
    juce::Typeface::Ptr typefaceFor (Weight weight)
    {
        // Typefaces are process-wide immutable resources; sharing them across
        // instances is safe and avoids re-parsing the font on every editor.
        static const juce::Typeface::Ptr regular  = juce::Typeface::createSystemTypefaceFor (KcfAssets::IBMPlexSansRegular_ttf, (size_t) KcfAssets::IBMPlexSansRegular_ttfSize);
        static const juce::Typeface::Ptr medium   = juce::Typeface::createSystemTypefaceFor (KcfAssets::IBMPlexSansMedium_ttf, (size_t) KcfAssets::IBMPlexSansMedium_ttfSize);
        static const juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (KcfAssets::IBMPlexSansSemiBold_ttf, (size_t) KcfAssets::IBMPlexSansSemiBold_ttfSize);
        static const juce::Typeface::Ptr mono     = juce::Typeface::createSystemTypefaceFor (KcfAssets::IBMPlexMonoMedium_ttf, (size_t) KcfAssets::IBMPlexMonoMedium_ttfSize);
        switch (weight)
        {
            case Weight::medium:   return medium;
            case Weight::semibold: return semibold;
            case Weight::mono:     return mono;
            case Weight::regular:  break;
        }
        return regular;
    }
}

juce::Font font (float height, Weight weight)
{
    return juce::Font (juce::FontOptions (typefaceFor (weight)).withHeight (height));
}

LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypeface (typefaceFor (Weight::regular));
    setColour (juce::ComboBox::backgroundColourId, Palette::panelRaised);
    setColour (juce::ComboBox::textColourId, Palette::text);
    setColour (juce::ComboBox::outlineColourId, Palette::panelEdge);
    setColour (juce::ComboBox::arrowColourId, Palette::copper);
    setColour (juce::PopupMenu::backgroundColourId, Palette::panelRaised);
    setColour (juce::PopupMenu::textColourId, Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::copperDeep);
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::text);
    setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, Palette::copperDeep);
    setColour (juce::TextButton::textColourOffId, Palette::text);
    setColour (juce::TextButton::textColourOnId, Palette::text);
    setColour (juce::Label::textColourId, Palette::text);
    setColour (juce::TextEditor::backgroundColourId, Palette::background);
    setColour (juce::TextEditor::textColourId, Palette::text);
    setColour (juce::TextEditor::highlightColourId, Palette::copperDeep);
    setColour (juce::TextEditor::focusedOutlineColourId, Palette::copper);
    setColour (juce::TextEditor::outlineColourId, Palette::panelEdge);
    setColour (juce::CaretComponent::caretColourId, Palette::copperBright);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::panelRaised);
    setColour (juce::TooltipWindow::textColourId, Palette::text);
    setColour (juce::TooltipWindow::outlineColourId, Palette::panelEdge);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }
juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (14.0f, Weight::medium); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l) { return l.getFont(); }
juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font (13.0f, Weight::medium); }

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Palette::copper : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (Palette::panelRaised);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                     bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                     const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (Palette::panelEdge);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (Palette::copperDeep.withAlpha (0.75f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }
    g.setColour (isActive ? Palette::text : Palette::textDim);
    g.setFont (font (14.0f, isTicked ? Weight::semibold : Weight::regular));
    g.drawText (text, r.reduced (10, 0), juce::Justification::centredLeft);
    if (isTicked)
    {
        g.setColour (Palette::copper);
        g.fillEllipse ((float) r.getX() + 3.0f, (float) r.getCentreY() - 2.5f, 5.0f, 5.0f);
    }
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                                        bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = background;
    if (down) colour = colour.brighter (0.15f);
    else if (highlighted) colour = colour.brighter (0.07f);
    g.setColour (colour);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (b.getToggleState() ? Palette::copper.withAlpha (0.8f) : Palette::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                    juce::Rectangle<int> parentArea)
{
    const juce::AttributedString s = [&]
    {
        juce::AttributedString a;
        a.setJustification (juce::Justification::centredLeft);
        a.append (tipText, font (13.0f), Palette::text);
        return a;
    }();
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 300.0f);
    const int w = (int) (layout.getWidth() + 18.0f);
    const int h = (int) (layout.getHeight() + 12.0f);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (Palette::panelRaised);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (Palette::copper.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, font (13.0f), Palette::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, (float) width - 18.0f);
    layout.draw (g, r.reduced (9.0f, 6.0f));
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 4.0f);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? Palette::copper : Palette::panelEdge);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f), 4.0f, 1.0f);
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool)
{
    g.setColour (isMouseOver ? Palette::copper : Palette::textDim);
    for (int i = 0; i < 3; ++i)
    {
        const float o = 4.0f + (float) i * 4.0f;
        g.drawLine ((float) w - o, (float) h - 2.0f, (float) w - 2.0f, (float) h - o, 1.2f);
    }
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, const juce::String& caption)
{
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (Palette::textMuted);
        g.setFont (font (12.0f, Weight::semibold));
        g.drawText (title.toUpperCase(), bounds.reduced (14.0f, 9.0f).withHeight (14.0f), juce::Justification::topLeft);
    }
    if (caption.isNotEmpty())
    {
        g.setColour (Palette::textDim);
        g.setFont (font (12.0f));
        g.drawText (caption, bounds.reduced (14.0f, 9.0f).withHeight (14.0f), juce::Justification::topRight);
    }
}

juce::Path diamond (juce::Point<float> c, float r)
{
    juce::Path p;
    p.startNewSubPath (c.x, c.y - r);
    p.lineTo (c.x + r, c.y);
    p.lineTo (c.x, c.y + r);
    p.lineTo (c.x - r, c.y);
    p.closeSubPath();
    return p;
}

} // namespace kcf::ui
