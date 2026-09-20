#pragma once
#include <JuceHeader.h>

namespace aquanova {

//==============================================================================
/** The house style: bright cyan on a cool deep blue, with warm accents for the
    things that are on. Meant to feel like daylight on water rather than the
    usual black rack panel.
*/
struct Palette
{
    // Tropical: shallow-water blues, lit from above. The background is the
    // brightest surface and panels sit a shade deeper on top of it, so the
    // sections read as cards floating on water rather than holes cut into it.
    static juce::Colour background()   { return juce::Colour (0xff1d8fc4); }
    static juce::Colour backgroundLo() { return juce::Colour (0xff1577a8); }
    static juce::Colour panel()        { return juce::Colour (0xff10618f); }
    static juce::Colour panelAlt()     { return juce::Colour (0xff1c7fb3); }
    static juce::Colour cyan()         { return juce::Colour (0xff6ffaff); }
    static juce::Colour cyanBright()   { return juce::Colour (0xffcbffff); }
    static juce::Colour cyanDim()      { return juce::Colour (0xff3fc8e0); }
    // Pure cyan (0, 255, 255) - reserved for the actual volume/level knobs so
    // they stand out at a glance from the rest of the (softer) house cyan.
    static juce::Colour volumeCyan()   { return juce::Colour (0xff00ffff); }
    static juce::Colour aqua()         { return juce::Colour (0xff72ffe0); }
    static juce::Colour sun()          { return juce::Colour (0xff9fe4ff); }
    static juce::Colour coral()        { return juce::Colour (0xffb4d8ff); }
    static juce::Colour text()         { return juce::Colour (0xffffffff); }
    static juce::Colour textDim()      { return juce::Colour (0xffe4f8ff); }
    static juce::Colour rule()         { return juce::Colour (0x44ffffff); }
};

//==============================================================================
class AquaNovaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AquaNovaLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Palette::background());
        setColour (juce::Label::textColourId,                 Palette::text());
        setColour (juce::ComboBox::backgroundColourId,        Palette::panelAlt());
        setColour (juce::ComboBox::textColourId,              Palette::text());
        setColour (juce::ComboBox::outlineColourId,           Palette::cyanDim());
        setColour (juce::ComboBox::arrowColourId,             Palette::cyan());
        setColour (juce::PopupMenu::backgroundColourId,       Palette::panel());
        setColour (juce::PopupMenu::textColourId,             Palette::text());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::cyanDim());
        setColour (juce::TextButton::buttonColourId,          Palette::panelAlt());
        setColour (juce::TextButton::textColourOffId,         Palette::text());
        setColour (juce::TextEditor::backgroundColourId,      Palette::panelAlt());
        setColour (juce::TextEditor::textColourId,            Palette::text());
        setColour (juce::TextEditor::outlineColourId,         Palette::cyanDim());
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + pos * (endAngle - startAngle);
        const auto thickness = juce::jmax (2.5f, radius * 0.22f);

        // Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness,
                             0.0f, startAngle, endAngle, true);
        g.setColour (Palette::panelAlt());
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Value arc. Bipolar controls fill outwards from the centre.
        // The control itself tells us whether it is bipolar; no need to sniff
        // the range, which meant comparing floats for equality.
        const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness,
                             0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);

        const bool isVolume = (bool) slider.getProperties().getWithDefault ("isVolume", false);
        g.setColour (isVolume ? Palette::volumeCyan()
                              : (slider.isMouseOverOrDragging() ? Palette::aqua() : Palette::cyan()));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Pointer
        juce::Path pointer;
        pointer.addRoundedRectangle (-thickness * 0.22f, -radius + thickness * 0.2f,
                                     thickness * 0.44f, radius * 0.55f, thickness * 0.2f);
        g.setColour (Palette::text());
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }

    juce::Font getLabelFont (juce::Label& l) override
    {
        return juce::Font (juce::FontOptions ((float) juce::jlimit (12, 24, l.getHeight() - 2)));
    }

    // The header buttons and dropdowns were set in tiny type by the defaults.
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions ((float) juce::jlimit (13, 17, buttonHeight - 12)));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font (juce::FontOptions (15.0f));
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

        g.setColour (Palette::panelAlt());
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (box.isMouseOver() ? Palette::cyan() : Palette::cyanDim());
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        juce::Path arrow;
        const float cx = (float) width - 9.0f, cy = (float) height * 0.5f;
        arrow.addTriangle (cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
        g.setColour (Palette::cyan());
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (juce::FontOptions ((float) juce::jlimit (12, 15, box.getHeight() - 8)));
    }
};

} // namespace aquanova
