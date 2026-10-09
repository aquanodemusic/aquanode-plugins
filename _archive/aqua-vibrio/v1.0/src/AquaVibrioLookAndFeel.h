#pragma once
//==============================================================================
//  AquaVibrioLookAndFeel.h
//
//  The osTIrus panel layout wearing Aquanova's clothes, rotated from blue to
//  green. Three things carry the look: a vertical gradient on every panel so
//  the surface reads as lit from above, the thin-arc knob with a hairline
//  indicator and its value printed underneath, and section header bars that
//  sit flush against the panel edge.
//
//  Dark chassis, bright green for anything live - arcs, active states, the
//  header rules. A panel this dense goes loud fast if the background is
//  saturated too, so the saturation lives in the controls.
//==============================================================================

#include <JuceHeader.h>

namespace aquavibrio
{

namespace Theme
{
    // The height of a card's header bar. A dropdown sits INSIDE this bar on
    // the grouped cards (Oscillator, Filter, Envelope, Effects...), so the bar
    // has to be tall enough to hold one - at 18 px the combo spilled over the
    // bar's bottom edge.
    constexpr int headerHeight = 26;

    // chassis
    const juce::Colour background      { 0xff13271e };
    const juce::Colour panelTop        { 0xff234032 };
    const juce::Colour panelBottom     { 0xff1a3026 };
    const juce::Colour panelEdge       { 0xff0d1d15 };
    const juce::Colour groupTop        { 0xff2b5140 };
    const juce::Colour groupBottom     { 0xff1f3a2d };

    // green family
    const juce::Colour accent          { 0xff54efa0 };   // live values, arcs
    const juce::Colour accentDim       { 0xff37a870 };
    const juce::Colour accentGlow      { 0xff9dffd0 };
    const juce::Colour headerBar       { 0xff3f9d6d };

    // ink
    const juce::Colour textBright      { 0xffe6f5ec };
    const juce::Colour textLabel       { 0xffabd0bd };
    const juce::Colour textDim         { 0xff7fa091 };

    // controls
    const juce::Colour trackEmpty      { 0xff10241b };
    const juce::Colour knobFace        { 0xff1f3a2d };
    const juce::Colour comboFill       { 0xff264736 };
    const juce::Colour comboBorder     { 0xff44775c };

    inline juce::Colour sectionTint (const juce::String& region)
    {
        // A slight hue shift per functional block, in the same way the
        // Aquanova panels differ from one another without ever leaving the
        // family. Oscillators cool, filters mid, effects warm.
        if (region.startsWith ("osc") || region == "unison")
            return juce::Colour { 0xff49e0b7 };
        if (region.startsWith ("filter") || region.startsWith ("env"))
            return juce::Colour { 0xff54efa0 };
        if (region.startsWith ("lfo") || region.startsWith ("assign") || region == "modmatrix")
            return juce::Colour { 0xff84f28a };
        return juce::Colour { 0xffa5ef80 };  // effects
    }
}

//==============================================================================
class AquaVibrioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AquaVibrioLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
        setColour (juce::Label::textColourId,                 Theme::textLabel);
        setColour (juce::ComboBox::backgroundColourId,        Theme::comboFill);
        setColour (juce::ComboBox::outlineColourId,           Theme::comboBorder);
        setColour (juce::ComboBox::textColourId,              Theme::textBright);
        setColour (juce::ComboBox::arrowColourId,             Theme::accent);
        setColour (juce::PopupMenu::backgroundColourId,       Theme::panelBottom);
        setColour (juce::PopupMenu::textColourId,             Theme::textBright);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accentDim);
        setColour (juce::TextButton::buttonColourId,          Theme::comboFill);
        setColour (juce::TextButton::textColourOffId,         Theme::textLabel);
        setColour (juce::TextButton::textColourOnId,          Theme::background);
        setColour (juce::TextEditor::backgroundColourId,      Theme::trackEmpty);
        setColour (juce::TextEditor::textColourId,            Theme::textBright);
    }

    //== knob ==================================================================
    // Small circular face, a thin arc from the parameter's own zero point, and
    // a hairline indicator. Bipolar parameters grow their arc out of twelve
    // o'clock rather than out of the left stop, which is the only way a
    // detune or an envelope amount reads correctly at a glance.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto centre = area.getCentre();
        const auto angle = startAngle + pos * (endAngle - startAngle);

        const bool bipolar = slider.getProperties()["bipolar"];
        const float originPos = bipolar ? 0.5f : 0.0f;
        const float originAngle = startAngle + originPos * (endAngle - startAngle);

        const auto arcRadius = radius - 2.0f;
        const auto thickness = juce::jmax (2.0f, radius * 0.16f);
        const auto tint = slider.findColour (juce::Slider::rotarySliderFillColourId);

        // face
        g.setGradientFill ({ Theme::knobFace.brighter (0.10f), centre.x, centre.y - radius,
                             Theme::knobFace.darker (0.35f),   centre.x, centre.y + radius, false });
        g.fillEllipse (centre.x - radius + 1.0f, centre.y - radius + 1.0f,
                       (radius - 1.0f) * 2.0f, (radius - 1.0f) * 2.0f);

        // empty track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (Theme::trackEmpty);
        g.strokePath (track, { thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // value arc
        if (std::abs (angle - originAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (originAngle, angle),
                                 juce::jmax (originAngle, angle), true);
            g.setColour (slider.isEnabled() ? tint : tint.withAlpha (0.3f));
            g.strokePath (value, { thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // indicator
        juce::Path pointer;
        const auto pw = juce::jmax (1.5f, radius * 0.11f);
        pointer.addRoundedRectangle (-pw * 0.5f, -radius + 2.0f, pw, radius * 0.68f, pw * 0.5f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
        g.setColour (slider.isEnabled() ? Theme::textBright : Theme::textDim);
        g.fillPath (pointer);
    }

    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        auto bounds = slider.getLocalBounds();
        juce::Slider::SliderLayout layout;

        if (slider.isRotary())
        {
            layout.textBoxBounds = bounds.removeFromBottom (14);
            layout.sliderBounds = bounds;
        }
        else
        {
            layout.textBoxBounds = bounds.removeFromRight (42);
            layout.sliderBounds = bounds;
        }

        return layout;
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        l->setColour (juce::Label::textColourId,             Theme::accent);
        l->setColour (juce::Label::backgroundColourId,       juce::Colours::transparentBlack);
        l->setColour (juce::Label::outlineColourId,          juce::Colours::transparentBlack);
        l->setColour (juce::TextEditor::highlightColourId,   Theme::accentDim);
        l->setColour (juce::TextEditor::backgroundColourId,  Theme::trackEmpty);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::Font (juce::FontOptions (11.0f)));
        return l;
    }

    //== horizontal bar (the Aquanova mixer-style slider) ======================
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        auto r = juce::Rectangle<int> (x, y, width, height).toFloat().withSizeKeepingCentre ((float) width, 8.0f);

        g.setColour (Theme::trackEmpty);
        g.fillRoundedRectangle (r, 2.0f);

        const auto filled = r.withRight (sliderPos);
        g.setGradientFill ({ Theme::accent, r.getX(), 0.0f, Theme::accentDim, r.getRight(), 0.0f, false });
        g.fillRoundedRectangle (filled, 2.0f);

        juce::ignoreUnused (slider);
    }

    //== combo =================================================================
    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

        g.setGradientFill ({ Theme::comboFill.brighter (0.10f), 0.0f, 0.0f,
                             Theme::comboFill.darker (0.20f),   0.0f, (float) height, false });
        g.fillRoundedRectangle (r, 3.0f);

        g.setColour (box.isMouseOver() ? Theme::accent : Theme::comboBorder);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        juce::Path arrow;
        const float cx = (float) width - 10.0f, cy = (float) height * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (Theme::accent);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.0f)); }
    juce::Font getLabelFont (juce::Label&) override       { return juce::Font (juce::FontOptions (11.0f)); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 0, box.getWidth() - 20, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }

    //== buttons ===============================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState() || down;

        if (on)
            g.setGradientFill ({ Theme::accentGlow, 0.0f, 0.0f, Theme::accent, 0.0f, r.getHeight(), false });
        else
            g.setGradientFill ({ Theme::comboFill.brighter (over ? 0.16f : 0.08f), 0.0f, 0.0f,
                                 Theme::comboFill.darker (0.20f), 0.0f, r.getHeight(), false });

        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? Theme::accentGlow : Theme::comboBorder);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    //== panels ================================================================
    // Used by the editor for every section box. Header bar flush to the top
    // edge, name in it, gradient body below.
    static void drawSectionPanel (juce::Graphics& g, juce::Rectangle<int> bounds,
                                  const juce::String& title, juce::Colour tint)
    {
        auto r = bounds.toFloat();

        g.setGradientFill ({ Theme::panelTop, r.getX(), r.getY(),
                             Theme::panelBottom, r.getX(), r.getBottom(), false });
        g.fillRoundedRectangle (r, 4.0f);

        if (title.isNotEmpty())
        {
            auto header = r.removeFromTop ((float) Theme::headerHeight);
            juce::Path p;
            p.addRoundedRectangle (header.getX(), header.getY(), header.getWidth(), header.getHeight(),
                                   4.0f, 4.0f, true, true, false, false);
            g.setGradientFill ({ tint.withAlpha (0.35f), header.getX(), header.getY(),
                                 tint.withAlpha (0.12f), header.getX(), header.getBottom(), false });
            g.fillPath (p);

            g.setColour (tint);
            g.fillRect (header.getX() + 1.0f, header.getBottom() - 1.0f, header.getWidth() - 2.0f, 1.0f);

            g.setColour (Theme::textBright);
            g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
            g.drawText (title.toUpperCase(), header.reduced (7, 0),
                        juce::Justification::centredLeft, false);
        }

        g.setColour (Theme::panelEdge);
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }
};

} // namespace aquavibrio
