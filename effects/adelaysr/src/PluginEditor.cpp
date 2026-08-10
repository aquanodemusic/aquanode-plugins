#include "PluginProcessor.h"
#include "PluginEditor.h"

// ════════════════════════════════════════════════════════════════════════════
//  Colour palette  (deep-navy aero glass)
// ════════════════════════════════════════════════════════════════════════════
namespace Col
{
    // Backgrounds
    const juce::Colour bgTop    { 0xff0b1220 };
    const juce::Colour bgBot    { 0xff060a12 };
    const juce::Colour slotLine { 0xff162233 };
    const juce::Colour hdrLine  { 0xff1c3050 };

    // Knob body
    const juce::Colour knobTop  { 0xff1e3050 };
    const juce::Colour knobBot  { 0xff050a10 };
    const juce::Colour rimLight { 0xff5a7090 };
    const juce::Colour rimDark  { 0xff101820 };

    // Accents per control
    const juce::Colour accentBlue   { 0xff3db8ff };  // knobs / SYNC on
    const juce::Colour accentOrange { 0xffffaa44 };  // NOTE on
    const juce::Colour accentGreen  { 0xff44ee88 };  // TRIG on
    const juce::Colour accentRed    { 0xffdd2233 };  // REC on / blink

    // Text
    const juce::Colour textBright   { 0xffc0d8f0 };
    const juce::Colour textDim      { 0xff4a6880 };
    const juce::Colour textLabel    { 0xff3a5870 };

    // Combo / popup
    const juce::Colour comboBg      { 0xff0d1a2a };
    const juce::Colour comboBorder  { 0xff1e3a55 };
    const juce::Colour popupBg      { 0xff0a1525 };
    const juce::Colour popupHover   { 0xff162840 };

    // Knob body inner / misc (dark)
    const juce::Colour knobBodyInner { 0xff080f1c };
    const juce::Colour innerRimRing  { 0xff1a2e45 };
    const juce::Colour trackArcBg    { 0xff0d1b28 };
    const juce::Colour btnOff        { 0xff0e1c2a };
    const juce::Colour btnHover      { 0xff1e3248 };
    const juce::Colour btnDown       { 0xff12202e };
}

// ── Light (cyan / underwater-blue) theme ────────────────────────────────────
namespace LCol
{
    const juce::Colour bgTop    { 0xfff0f8ff };
    const juce::Colour bgBot    { 0xffb8daf4 };
    const juce::Colour slotLine { 0xff8cbcd8 };
    const juce::Colour hdrLine  { 0xff60a8cc };

    const juce::Colour knobTop  { 0xffe4f2ff };
    const juce::Colour knobBot  { 0xff88c0dc };
    const juce::Colour rimLight { 0xffffffff };
    const juce::Colour rimDark  { 0xff60a0c0 };

    const juce::Colour accentBlue   { 0xff0070c0 };
    const juce::Colour accentOrange { 0xffee6600 };
    const juce::Colour accentGreen  { 0xff009944 };
    const juce::Colour accentRed    { 0xffcc1133 };

    const juce::Colour textBright   { 0xff062030 };
    const juce::Colour textDim      { 0xff185878 };
    const juce::Colour textLabel    { 0xff0a5878 };

    const juce::Colour comboBg      { 0xffe0f4ff };
    const juce::Colour comboBorder  { 0xff68acd0 };
    const juce::Colour popupBg      { 0xfff4fbff };
    const juce::Colour popupHover   { 0xffc0dff0 };

    const juce::Colour knobBodyInner { 0xffc8e4f8 };
    const juce::Colour innerRimRing  { 0xff80b4d0 };
    const juce::Colour trackArcBg    { 0xff90c8e8 };
    const juce::Colour btnOff        { 0xffdceefa };
    const juce::Colour btnHover      { 0xffcce8f8 };
    const juce::Colour btnDown       { 0xffb8e0f4 };
}

// Inline theme switch helper  (d = dark colour, l = light colour)
static inline juce::Colour th (bool light,
                                const juce::Colour& d,
                                const juce::Colour& l) noexcept
{
    return light ? l : d;
}

// ════════════════════════════════════════════════════════════════════════════
//  AeroLookAndFeel  –  implementation
// ════════════════════════════════════════════════════════════════════════════

AeroLookAndFeel::AeroLookAndFeel()
{
    // Default colours that look right on a dark background
    setColour (juce::PopupMenu::backgroundColourId,     Col::popupBg);
    setColour (juce::PopupMenu::textColourId,           Col::textBright);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Col::popupHover);
    setColour (juce::PopupMenu::highlightedTextColourId, Col::accentBlue);
}

void AeroLookAndFeel::setLightMode (bool light)
{
    lightMode = light;
    setColour (juce::PopupMenu::backgroundColourId,
               th (light, Col::popupBg,    LCol::popupBg));
    setColour (juce::PopupMenu::textColourId,
               th (light, Col::textBright, LCol::textBright));
    setColour (juce::PopupMenu::highlightedBackgroundColourId,
               th (light, Col::popupHover, LCol::popupHover));
    setColour (juce::PopupMenu::highlightedTextColourId,
               th (light, Col::accentBlue, LCol::accentBlue));
}

// ── Rotary slider ────────────────────────────────────────────────────────────
void AeroLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                         int x, int y, int width, int height,
                                         float sliderPos,
                                         float startAngle, float endAngle,
                                         juce::Slider& slider)
{
    const float cx   = (float)x + (float)width  * 0.5f;
    const float cy   = (float)y + (float)height * 0.5f;
    const float outerR = juce::jmin ((float)width, (float)height) * 0.5f - 9.0f;
    const float innerR = outerR - 3.0f;   // inside the metallic rim

    // Read per-slider accent colour
    const juce::Colour accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // ─── 1.  Soft drop shadow ───────────────────────────────────────────────
    for (int i = 4; i >= 1; --i)
    {
        const float a = 0.12f * (float)i;
        const float spread = (float)i * 1.2f;
        g.setColour (juce::Colours::black.withAlpha (a));
        g.fillEllipse (cx - outerR - 0.5f + spread * 0.4f,
                       cy - outerR - 0.5f + spread,
                       (outerR + 0.5f) * 2.0f, (outerR + 0.5f) * 2.0f);
    }

    // ─── 2.  Metallic outer rim  (light-top / dark-bottom bevel) ───────────
    {
        juce::ColourGradient rim (th (lightMode, Col::rimLight, LCol::rimLight),
                                   cx - outerR, cy - outerR,
                                   th (lightMode, Col::rimDark, LCol::rimDark),
                                   cx + outerR, cy + outerR, true);
        g.setGradientFill (rim);
        g.fillEllipse (cx - outerR, cy - outerR, outerR * 2.0f, outerR * 2.0f);
    }

    // ─── 3.  Knob face (deep glassy body) ───────────────────────────────────
    {
        juce::ColourGradient body (th (lightMode, Col::knobTop, LCol::knobTop).withAlpha (1.0f),
                                    cx - innerR * 0.35f, cy - innerR * 0.75f,
                                    th (lightMode, Col::knobBot, LCol::knobBot),
                                    cx + innerR * 0.25f, cy + innerR * 0.75f, false);
        body.addColour (0.55, th (lightMode, Col::knobBodyInner, LCol::knobBodyInner));
        g.setGradientFill (body);
        g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    }

    // ─── 4.  Inner rim ring (subtle halo just inside bevel) ─────────────────
    {
        g.setColour (th (lightMode, Col::innerRimRing, LCol::innerRimRing).withAlpha (0.6f));
        g.drawEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 0.8f);
    }

    // ─── 5.  Track arc ───────────────────────────────────────────────────────
    {
        juce::Path track;
        const float tr = outerR + 3.5f;
        track.addArc (cx - tr, cy - tr, tr * 2.0f, tr * 2.0f,
                      startAngle, endAngle, true);
        g.setColour (th (lightMode, Col::trackArcBg, LCol::trackArcBg));
        g.strokePath (track, juce::PathStrokeType (3.0f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ─── 6.  Value arc  (glowing accent colour) ──────────────────────────────
    const float curAngle = startAngle + sliderPos * (endAngle - startAngle);
    if (sliderPos > 0.0005f)
    {
        const float tr = outerR + 3.5f;
        juce::Path varc;
        varc.addArc (cx - tr, cy - tr, tr * 2.0f, tr * 2.0f,
                     startAngle, curAngle, true);

        // Outer glow pass
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (varc, juce::PathStrokeType (6.5f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Core bright pass
        juce::ColourGradient arcGrad (accent.brighter (0.25f),
                                       cx - tr, cy,
                                       accent.darker (0.15f),
                                       cx + tr, cy, false);
        g.setGradientFill (arcGrad);
        g.strokePath (varc, juce::PathStrokeType (2.8f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ─── 7.  Gloss bubble  (upper-half reflection) ───────────────────────────
    {
        juce::Path gloss;
        gloss.addEllipse (cx - innerR * 0.72f,
                           cy - innerR * 0.88f,
                           innerR * 1.44f,
                           innerR * 0.60f);
        juce::ColourGradient gg (juce::Colours::white.withAlpha (0.28f),
                                  cx, cy - innerR * 0.88f,
                                  juce::Colours::white.withAlpha (0.0f),
                                  cx, cy - innerR * 0.28f, false);
        g.setGradientFill (gg);
        g.fillPath (gloss);

        // Second, smaller specular spot
        juce::Path spec;
        spec.addEllipse (cx - innerR * 0.28f,
                          cy - innerR * 0.78f,
                          innerR * 0.56f,
                          innerR * 0.25f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillPath (spec);
    }

    // ─── 8.  Indicator line + dot ────────────────────────────────────────────
    {
        const float dotTravel = innerR * 0.60f;
        const float px = cx + std::sin (curAngle) * dotTravel;
        const float py = cy - std::cos (curAngle) * dotTravel;

        // Subtle centre-to-dot line
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawLine (cx, cy, px, py, 1.2f);

        // Outer dot glow
        const float dr = 4.2f;
        g.setColour (accent.withAlpha (0.45f));
        g.fillEllipse (px - dr * 1.6f, py - dr * 1.6f, dr * 3.2f, dr * 3.2f);

        // Dot body
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.fillEllipse (px - dr, py - dr, dr * 2.0f, dr * 2.0f);

        // Dot inner accent
        g.setColour (accent.withAlpha (0.80f));
        g.fillEllipse (px - dr * 0.52f, py - dr * 0.52f, dr * 1.04f, dr * 1.04f);
    }

    // ─── 9.  Centre shimmer (faint hotspot) ──────────────────────────────────
    {
        const float sr = innerR * 0.22f;
        juce::ColourGradient sg (juce::Colours::white.withAlpha (0.10f),
                                  cx - sr * 0.5f, cy - sr * 1.8f,
                                  juce::Colours::transparentWhite, cx, cy, false);
        g.setGradientFill (sg);
        g.fillEllipse (cx - sr, cy - sr * 2.0f, sr * 2.0f, sr * 2.0f);
    }
}

// ── Label (text box beneath knob) ────────────────────────────────────────────
juce::Font AeroLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (11.5f);
}

// ── Button background ─────────────────────────────────────────────────────────
void AeroLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& btn,
                                              const juce::Colour& /*bg*/,
                                              bool isHighlighted, bool isDown)
{
    const auto bounds = btn.getLocalBounds().toFloat().reduced (0.5f);
    const float cr    = 5.0f;
    const bool  isOn  = btn.getToggleState();

    const juce::Colour onColour = btn.findColour (juce::TextButton::buttonOnColourId);

    if (isOn)
    {
        // Active: glowing gradient
        juce::ColourGradient g2 (onColour.brighter (0.20f),
                                  bounds.getX(), bounds.getY(),
                                  onColour.darker  (0.35f),
                                  bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (g2);
        g.fillRoundedRectangle (bounds, cr);

        // Glow border
        g.setColour (onColour.withAlpha (0.65f));
        g.drawRoundedRectangle (bounds, cr, 1.2f);

        // Inner top shine
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawLine (bounds.getX() + cr, bounds.getY() + 0.7f,
                    bounds.getRight() - cr, bounds.getY() + 0.7f, 0.9f);
    }
    else
    {
        // Inactive: dark glass (or light glass in light mode)
        const juce::Colour base = isDown        ? th (lightMode, Col::btnDown,  LCol::btnDown)
                                : isHighlighted ? th (lightMode, Col::btnHover, LCol::btnHover)
                                                : th (lightMode, Col::btnOff,   LCol::btnOff);

        juce::ColourGradient g2 (base.brighter (0.08f),
                                  bounds.getX(), bounds.getY(),
                                  base.darker (0.15f),
                                  bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (g2);
        g.fillRoundedRectangle (bounds, cr);

        g.setColour (th (lightMode, Col::comboBorder, LCol::comboBorder));
        g.drawRoundedRectangle (bounds, cr, 0.9f);

        // Subtle top highlight
        g.setColour (juce::Colours::white.withAlpha (lightMode ? 0.55f : 0.06f));
        g.drawLine (bounds.getX() + cr, bounds.getY() + 0.7f,
                    bounds.getRight() - cr, bounds.getY() + 0.7f, 0.7f);
    }
}

void AeroLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& btn,
                                       bool, bool)
{
    const bool  isOn   = btn.getToggleState();
    g.setColour (isOn ? juce::Colour (0xff050c14)
                      : th (lightMode, Col::textDim, LCol::textDim));
    g.setFont (juce::Font (10.5f, juce::Font::bold));
    g.drawText (btn.getButtonText(), btn.getLocalBounds(),
                juce::Justification::centred, false);
}

// ── ComboBox ──────────────────────────────────────────────────────────────────
void AeroLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h,
                                     bool isDown, int bx, int by, int bw, int bh,
                                     juce::ComboBox&)
{
    const float cr = 5.0f;
    const juce::Rectangle<float> bounds (0.0f, 0.0f, (float)w, (float)h);
    const juce::Colour cb = th (lightMode, Col::comboBg,     LCol::comboBg);
    const juce::Colour ab = th (lightMode, Col::accentBlue,  LCol::accentBlue);
    const juce::Colour bd = th (lightMode, Col::comboBorder, LCol::comboBorder);

    juce::ColourGradient bg (cb.brighter (0.07f), 0.0f, 0.0f,
                              cb.darker   (0.10f), 0.0f, (float)h, false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (bounds, cr);

    g.setColour (isDown ? ab.withAlpha (0.60f) : bd);
    g.drawRoundedRectangle (bounds.reduced (0.5f), cr, 1.0f);

    // Top gloss line
    g.setColour (juce::Colours::white.withAlpha (lightMode ? 0.45f : 0.07f));
    g.drawLine (cr, 0.8f, (float)w - cr, 0.8f, 0.7f);

    // Arrow
    const float acx = (float)bx + (float)bw * 0.5f;
    const float acy = (float)by + (float)bh * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (acx - 4.5f, acy - 2.0f,
                        acx + 4.5f, acy - 2.0f,
                        acx,         acy + 3.5f);
    g.setColour (ab.withAlpha (0.85f));
    g.fillPath (arrow);
}

juce::Font AeroLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (11.5f);
}

void AeroLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& lbl)
{
    lbl.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    lbl.setFont (getComboBoxFont (box));
}

// ── Popup menu ────────────────────────────────────────────────────────────────
void AeroLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (th (lightMode, Col::popupBg, LCol::popupBg));
    g.setColour (th (lightMode, Col::comboBorder, LCol::comboBorder));
    g.drawRect (0, 0, w, h, 1);
}

void AeroLookAndFeel::drawPopupMenuItem (juce::Graphics& g,
                                          const juce::Rectangle<int>& area,
                                          bool isSeparator, bool /*isActive*/,
                                          bool isHighlighted, bool isTicked,
                                          bool /*hasSubMenu*/,
                                          const juce::String& text,
                                          const juce::String& /*shortcut*/,
                                          const juce::Drawable* /*icon*/,
                                          const juce::Colour* /*textColour*/)
{
    const juce::Colour ab = th (lightMode, Col::accentBlue,  LCol::accentBlue);
    const juce::Colour tb = th (lightMode, Col::textBright,  LCol::textBright);
    const juce::Colour ph = th (lightMode, Col::popupHover,  LCol::popupHover);

    if (isSeparator)
    {
        g.setColour (th (lightMode, Col::comboBorder, LCol::comboBorder));
        g.drawHorizontalLine (area.getCentreY(), (float)area.getX() + 4.0f,
                              (float)area.getRight() - 4.0f);
        return;
    }

    if (isHighlighted)
    {
        juce::ColourGradient hg (ph.brighter (0.1f),
                                  (float)area.getX(), (float)area.getY(),
                                  ph, (float)area.getX(),
                                  (float)area.getBottom(), false);
        g.setGradientFill (hg);
        g.fillRect (area);
    }

    const juce::Colour textCol = isHighlighted ? ab : tb.withAlpha (0.85f);
    g.setColour (textCol);
    g.setFont (juce::Font (11.5f, isTicked ? juce::Font::bold : juce::Font::plain));
    g.drawText (text, area.reduced (8, 0), juce::Justification::centredLeft);

    if (isTicked)
    {
        g.setColour (ab);
        g.fillEllipse ((float)area.getRight() - 14.0f,
                        (float)area.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
}


// ════════════════════════════════════════════════════════════════════════════
//  Helper – style a knob slider
// ════════════════════════════════════════════════════════════════════════════
static void styleKnob (juce::Slider& s,
                        juce::LookAndFeel& laf,
                        bool lightMode,
                        juce::Colour accentD = Col::accentBlue,
                        juce::Colour accentL = LCol::accentBlue)
{
    const juce::Colour accent = th (lightMode, accentD, accentL);
    s.setLookAndFeel (&laf);
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 17);
    s.setColour (juce::Slider::rotarySliderFillColourId,    accent);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, th (lightMode, Col::trackArcBg,  LCol::trackArcBg));
    s.setColour (juce::Slider::textBoxTextColourId,         th (lightMode, Col::textBright,  LCol::textBright).withAlpha (0.85f));
    s.setColour (juce::Slider::textBoxBackgroundColourId,   th (lightMode, Col::knobBodyInner, LCol::knobBodyInner));
    s.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxHighlightColourId,    accent.withAlpha (0.3f));
}

static void styleKnobLabel (juce::Label& l, const juce::String& text, bool lightMode)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::Font (9.5f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, th (lightMode, Col::textLabel, LCol::textLabel));
    l.setJustificationType (juce::Justification::centred);
}

static void styleToggleBtn (juce::TextButton& b,
                              juce::LookAndFeel& laf,
                              juce::Colour onColour)
{
    b.setLookAndFeel (&laf);
    b.setClickingTogglesState (true);
    // off-state body is drawn theme-aware in drawButtonBackground; just set on colour here
    b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff0e1c2a));
    b.setColour (juce::TextButton::buttonOnColourId, onColour);
    b.setColour (juce::TextButton::textColourOffId,  Col::textDim);
    b.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff040b12));
}

static void styleCombo (juce::ComboBox& c, juce::LookAndFeel& laf, bool lightMode)
{
    c.setLookAndFeel (&laf);
    c.setColour (juce::ComboBox::backgroundColourId, th (lightMode, Col::comboBg,     LCol::comboBg));
    c.setColour (juce::ComboBox::textColourId,       th (lightMode, Col::textBright,  LCol::textBright));
    c.setColour (juce::ComboBox::outlineColourId,    th (lightMode, Col::comboBorder, LCol::comboBorder));
    c.setColour (juce::ComboBox::arrowColourId,      th (lightMode, Col::accentBlue,  LCol::accentBlue));
}


// ════════════════════════════════════════════════════════════════════════════
//  ADelaySREditor  –  implementation
// ════════════════════════════════════════════════════════════════════════════

ADelaySREditor::ADelaySREditor (ADelaySRProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&aeroLAF);

    // ── Timing section ─────────────────────────────────────────────────────
    styleToggleBtn (syncButton,      aeroLAF, Col::accentBlue);
    styleToggleBtn (noteDelayButton, aeroLAF, Col::accentOrange);
    addAndMakeVisible (syncButton);
    addAndMakeVisible (noteDelayButton);

    styleKnob (delayTimeSlider, aeroLAF, false, Col::accentBlue, LCol::accentBlue);
    styleKnobLabel (delayTimeLabel, "DELAY TIME", false);
    addAndMakeVisible (delayTimeSlider);
    addAndMakeVisible (delayTimeLabel);

    // ── Delay smoothing vertical slider ────────────────────────────────────
    smoothingSlider.setLookAndFeel (&aeroLAF);
    smoothingSlider.setSliderStyle (juce::Slider::LinearVertical);
    smoothingSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 17);
    smoothingSlider.setNumDecimalPlacesToDisplay (0);
    smoothingSlider.setTextValueSuffix (" ms");
    smoothingSlider.setColour (juce::Slider::trackColourId,             Col::accentBlue.withAlpha (0.35f));
    smoothingSlider.setColour (juce::Slider::thumbColourId,             Col::accentBlue);
    smoothingSlider.setColour (juce::Slider::backgroundColourId,        Col::trackArcBg);
    smoothingSlider.setColour (juce::Slider::textBoxTextColourId,       Col::textBright.withAlpha (0.85f));
    smoothingSlider.setColour (juce::Slider::textBoxBackgroundColourId, Col::knobBodyInner);
    smoothingSlider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    styleKnobLabel (smoothingLabel, "SMOOTH", false);
    addAndMakeVisible (smoothingSlider);
    addAndMakeVisible (smoothingLabel);

    styleKnob (noteDelaySlider, aeroLAF, false, Col::accentOrange, LCol::accentOrange);
    noteDelaySlider.setNumDecimalPlacesToDisplay (5);
    noteDelaySlider.setTextValueSuffix (" ms");
    styleKnobLabel (noteDelayLabel, "NOTE DELAY", false);
    addAndMakeVisible (noteDelaySlider);
    addAndMakeVisible (noteDelayLabel);

    syncDivCombo.addItemList ({ "1/1","3/4","1/2","1/4","1/8","1/4T","1/8T" }, 1);
    styleCombo (syncDivCombo, aeroLAF, false);
    addAndMakeVisible (syncDivCombo);
    styleKnobLabel (syncDivLabel, "DIVISION", false);
    addAndMakeVisible (syncDivLabel);

    // ── Envelope section ───────────────────────────────────────────────────
    styleKnob (attackSlider,  aeroLAF, false);
    styleKnobLabel (attackLabel,  "ATTACK",  false);
    styleKnob (releaseSlider, aeroLAF, false);
    styleKnobLabel (releaseLabel, "RELEASE", false);
    styleKnob (tapVolSlider,  aeroLAF, false);
    styleKnobLabel (tapVolLabel,  "TAP VOL", false);
    addAndMakeVisible (attackSlider);   addAndMakeVisible (attackLabel);
    addAndMakeVisible (releaseSlider);  addAndMakeVisible (releaseLabel);
    addAndMakeVisible (tapVolSlider);   addAndMakeVisible (tapVolLabel);

    // ── Routing section ────────────────────────────────────────────────────
    styleToggleBtn (trigButton, aeroLAF, Col::accentGreen);
    addAndMakeVisible (trigButton);

    // ── 1.2x Tap Vol boost button ──────────────────────────────────────────
    styleToggleBtn (tapVolBoostButton, aeroLAF, Col::accentOrange);
    addAndMakeVisible (tapVolBoostButton);

    styleToggleBtn (wetOnlyButton, aeroLAF, Col::accentBlue);
    addAndMakeVisible (wetOnlyButton);

    // ── Clear delay buffer button ──────────────────────────────────────────
    clearButton.setLookAndFeel (&aeroLAF);
    clearButton.setClickingTogglesState (false);
    clearButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff0e1c2a));
    clearButton.setColour (juce::TextButton::textColourOffId, Col::textDim);
    clearButton.onClick = [this]() { proc.clearDelayBuffer(); };
    addAndMakeVisible (clearButton);

    modeCombo.addItemList ({ "Mono", "Stereo", "Ping Pong", "Hard Ping Pong" }, 1);
    styleCombo (modeCombo, aeroLAF, false);
    addAndMakeVisible (modeCombo);
    styleKnobLabel (modeLabel, "DELAY MODE", false);
    addAndMakeVisible (modeLabel);

    // ── Theme button (left of REC) ─────────────────────────────────────────
    themeButton.setLookAndFeel (&aeroLAF);
    themeButton.setClickingTogglesState (true);
    themeButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff0e1c2a));
    themeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff0070c0));
    themeButton.setColour (juce::TextButton::textColourOffId,  Col::textDim);
    themeButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    themeButton.onClick = [this]()
    {
        applyTheme (themeButton.getToggleState());
    };
    addAndMakeVisible (themeButton);

    // ── Recording button (not an APVTS param – manual click logic) ────────
    recButton.setLookAndFeel (&aeroLAF);
    recButton.setClickingTogglesState (false);
    recButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff0e1c2a));
    recButton.setColour (juce::TextButton::buttonOnColourId, Col::accentRed);
    recButton.setColour (juce::TextButton::textColourOffId,  Col::textDim);
    recButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    recButton.onClick = [this]()
    {
        if (!proc.isRecording.load())
        {
            // ── Start recording ─────────────────────────────────────────
            proc.startRecording();
            startTimer (450);
        }
        else
        {
            // ── Stop recording → open save dialog ───────────────────────
            proc.stopRecording();
            stopTimer();
            recButton.setToggleState (false, juce::dontSendNotification);
            blinkOn = false;

            fileChooser = std::make_unique<juce::FileChooser> (
                "Save Recording as WAV",
                juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                           .getChildFile ("recording.wav"),
                "*.wav");
            fileChooser->launchAsync (
                juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (f != juce::File{})
                        saveRecordingToFile (f);
                });
        }
    };
    addAndMakeVisible (recButton);

    // ── APVTS attachments ──────────────────────────────────────────────────
    delayTimeAtt      = std::make_unique<SliderAtt> (proc.apvts, "delayTimeMs",     delayTimeSlider);
    noteDelayAtt      = std::make_unique<SliderAtt> (proc.apvts, "noteDelayTimeMs", noteDelaySlider);
    attackAtt         = std::make_unique<SliderAtt> (proc.apvts, "attack",          attackSlider);
    releaseAtt        = std::make_unique<SliderAtt> (proc.apvts, "release",         releaseSlider);
    tapVolAtt         = std::make_unique<SliderAtt> (proc.apvts, "tapVolume",       tapVolSlider);
    smoothingAtt      = std::make_unique<SliderAtt> (proc.apvts, "delaySmoothMs",   smoothingSlider);
    syncDivAtt        = std::make_unique<ComboAtt>  (proc.apvts, "syncDivision",    syncDivCombo);
    modeAtt           = std::make_unique<ComboAtt>  (proc.apvts, "delayMode",       modeCombo);
    syncBtnAtt        = std::make_unique<BtnAtt>    (proc.apvts, "timeSyncEnabled",  syncButton);
    noteDelayBtnAtt   = std::make_unique<BtnAtt>    (proc.apvts, "noteDelayEnabled", noteDelayButton);
    trigBtnAtt        = std::make_unique<BtnAtt>    (proc.apvts, "triggerEnabled",   trigButton);
    wetOnlyBtnAtt     = std::make_unique<BtnAtt>    (proc.apvts, "wetOnly",          wetOnlyButton);
    tapVolBoostBtnAtt = std::make_unique<BtnAtt>    (proc.apvts, "tapVolBoost",      tapVolBoostButton);

    // Cap slider to 100 initially (boost off by default)
    tapVolSlider.setRange (0.0, 100.0, 0.1);

    // ── Listeners ──────────────────────────────────────────────────────────
    proc.apvts.addParameterListener ("timeSyncEnabled",  this);
    proc.apvts.addParameterListener ("noteDelayEnabled", this);
    proc.apvts.addParameterListener ("tapVolBoost",      this);

    setSize (800, 210);
    updateTimingVisibility();
}

ADelaySREditor::~ADelaySREditor()
{
    proc.apvts.removeParameterListener ("timeSyncEnabled",  this);
    proc.apvts.removeParameterListener ("noteDelayEnabled", this);
    proc.apvts.removeParameterListener ("tapVolBoost",      this);
    setLookAndFeel (nullptr);
}

// ────────────────────────────────────────────────────────────────────────────
void ADelaySREditor::timerCallback()
{
    // Auto-stop if the recording buffer filled up on the audio thread
    if (!proc.isRecording.load())
    {
        stopTimer();
        recButton.setToggleState (false, juce::dontSendNotification);
        blinkOn = false;
        return;
    }

    blinkOn = !blinkOn;
    recButton.setToggleState (blinkOn, juce::dontSendNotification);
}

// ────────────────────────────────────────────────────────────────────────────
void ADelaySREditor::saveRecordingToFile (const juce::File& file)
{
    const int numSamples = proc.getRecordedSamples();
    if (numSamples <= 0) return;

    juce::WavAudioFormat format;
    auto outStream = std::make_unique<juce::FileOutputStream> (file);
    if (!outStream->openedOk()) return;

    // 24-bit stereo WAV at the session sample rate
    auto* writer = format.createWriterFor (
        outStream.get(),
        proc.getSampleRate(), 2, 24, {}, 0);

    if (writer != nullptr)
    {
        outStream.release();   // writer now owns the stream
        writer->writeFromAudioSampleBuffer (proc.recordBuffer, 0, numSamples);
        delete writer;
    }
}

// ────────────────────────────────────────────────────────────────────────────
void ADelaySREditor::parameterChanged (const juce::String& id, float)
{
    if (id == "timeSyncEnabled" || id == "noteDelayEnabled")
        juce::MessageManager::callAsync ([this] { updateTimingVisibility(); });
    else if (id == "tapVolBoost")
        juce::MessageManager::callAsync ([this] { updateTapVolRange(); });
}

void ADelaySREditor::updateTimingVisibility()
{
    const bool nd = proc.apvts.getRawParameterValue ("noteDelayEnabled")->load() > 0.5f;
    const bool sy = proc.apvts.getRawParameterValue ("timeSyncEnabled") ->load() > 0.5f;

    delayTimeSlider.setVisible (!nd && !sy);
    delayTimeLabel .setVisible (!nd && !sy);
    noteDelaySlider.setVisible ( nd);
    noteDelayLabel .setVisible ( nd);
    syncDivCombo   .setVisible (!nd &&  sy);
    syncDivLabel   .setVisible (!nd &&  sy);

    syncButton.setEnabled (!nd);
    syncButton.setAlpha   (nd ? 0.28f : 1.0f);
}

void ADelaySREditor::updateTapVolRange()
{
    const bool boost = proc.apvts.getRawParameterValue ("tapVolBoost")->load() > 0.5f;
    if (boost)
    {
        tapVolSlider.setRange (0.0, 120.0, 0.1);
    }
    else
    {
        tapVolSlider.setRange (0.0, 100.0, 0.1);
        if (tapVolSlider.getValue() > 100.0)
            tapVolSlider.setValue (100.0, juce::sendNotification);
    }
}

void ADelaySREditor::applyTheme (bool light)
{
    isLightMode = light;
    aeroLAF.setLightMode (light);
    themeButton.setButtonText (light ? "LIGHT" : "DARK");

    // Knobs
    styleKnob (delayTimeSlider, aeroLAF, light, Col::accentBlue,   LCol::accentBlue);
    styleKnob (noteDelaySlider, aeroLAF, light, Col::accentOrange, LCol::accentOrange);
    styleKnob (attackSlider,    aeroLAF, light);
    styleKnob (releaseSlider,   aeroLAF, light);
    styleKnob (tapVolSlider,    aeroLAF, light);

    // Smoothing vertical slider
    smoothingSlider.setColour (juce::Slider::trackColourId,
                                th (light, Col::accentBlue,      LCol::accentBlue).withAlpha (0.35f));
    smoothingSlider.setColour (juce::Slider::thumbColourId,
                                th (light, Col::accentBlue,      LCol::accentBlue));
    smoothingSlider.setColour (juce::Slider::backgroundColourId,
                                th (light, Col::trackArcBg,      LCol::trackArcBg));
    smoothingSlider.setColour (juce::Slider::textBoxTextColourId,
                                th (light, Col::textBright,      LCol::textBright).withAlpha (0.85f));
    smoothingSlider.setColour (juce::Slider::textBoxBackgroundColourId,
                                th (light, Col::knobBodyInner,   LCol::knobBodyInner));

    // Labels
    styleKnobLabel (delayTimeLabel, "DELAY TIME", light);
    styleKnobLabel (noteDelayLabel, "NOTE DELAY", light);
    styleKnobLabel (attackLabel,    "ATTACK",     light);
    styleKnobLabel (releaseLabel,   "RELEASE",    light);
    styleKnobLabel (tapVolLabel,    "TAP VOL",    light);
    styleKnobLabel (smoothingLabel, "SMOOTH",     light);
    styleKnobLabel (syncDivLabel,   "DIVISION",   light);
    styleKnobLabel (modeLabel,      "DELAY MODE", light);

    // Combos
    styleCombo (syncDivCombo, aeroLAF, light);
    styleCombo (modeCombo,    aeroLAF, light);

    // REC button text colours (on-colour stays red)
    recButton.setColour (juce::TextButton::buttonColourId,
                         th (light, juce::Colour (0xff0e1c2a), LCol::btnOff));
    recButton.setColour (juce::TextButton::textColourOffId,
                         th (light, Col::textDim, LCol::textDim));

    // CLEAR button
    clearButton.setColour (juce::TextButton::buttonColourId,
                           th (light, juce::Colour (0xff0e1c2a), LCol::btnOff));
    clearButton.setColour (juce::TextButton::textColourOffId,
                           th (light, Col::textDim, LCol::textDim));

    repaint();
}

// ════════════════════════════════════════════════════════════════════════════
//  paint()
// ════════════════════════════════════════════════════════════════════════════
void ADelaySREditor::paint (juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();
    const bool light = isLightMode;

    // ── Background gradient ────────────────────────────────────────────────
    {
        juce::ColourGradient bg (th (light, Col::bgTop, LCol::bgTop), 0.0f, 0.0f,
                                  th (light, Col::bgBot, LCol::bgBot), 0.0f, (float)H, false);
        g.setGradientFill (bg);
        g.fillAll();
    }

    // ── Subtle scanline texture (every 2px) ───────────────────────────────
    g.setColour (juce::Colours::black.withAlpha (light ? 0.02f : 0.06f));
    for (int yy = 0; yy < H; yy += 2)
        g.drawHorizontalLine (yy, 0.0f, (float)W);

    // ── Top edge glow ─────────────────────────────────────────────────────
    {
        const juce::Colour glowC = th (light, Col::accentBlue, LCol::accentBlue);
        juce::ColourGradient tg (glowC.withAlpha (0.18f), 0.0f, 0.0f,
                                  juce::Colours::transparentBlack, 0.0f, 14.0f, false);
        g.setGradientFill (tg);
        g.fillRect (0, 0, W, 14);
    }

    // ── Bottom edge glow ──────────────────────────────────────────────────
    {
        const juce::Colour glowC = th (light, Col::accentBlue, LCol::accentBlue);
        juce::ColourGradient bg2 (juce::Colours::transparentBlack, 0.0f, (float)(H - 10),
                                   glowC.withAlpha (0.10f), 0.0f, (float)H, false);
        g.setGradientFill (bg2);
        g.fillRect (0, H - 10, W, 10);
    }

    // ── Slot separator lines ───────────────────────────────────────────────
    // Slot 1 (TIMING) is 200 px wide; slots 2–5 are 150 px each.
    constexpr int kSep1 = 200;          // after slot 1
    constexpr int kSep2 = 200 + 150*3;  // = 650, after slot 4
    const int sy  = 14;
    const int sh  = H - 28;
    g.setColour (th (light, Col::slotLine, LCol::slotLine).withAlpha (0.55f));
    g.drawVerticalLine (kSep1, (float)sy, (float)(sy + sh));
    g.drawVerticalLine (kSep2, (float)sy, (float)(sy + sh));

    // ── Bottom strip separator ─────────────────────────────────────────────
    g.setColour (th (light, Col::hdrLine, LCol::hdrLine).withAlpha (0.45f));
    g.drawHorizontalLine (H - 34, 4.0f, (float)(W - 4));

    // ── Section labels (top of each zone) ─────────────────────────────────
    g.setFont (juce::Font (8.5f, juce::Font::bold));
    g.setColour (th (light, Col::textLabel, LCol::textLabel).withAlpha (0.75f));
    g.drawText ("TIMING",   0,      4, kSep1,          12, juce::Justification::centred);
    g.drawText ("ENVELOPE", kSep1,  4, 150 * 3,        12, juce::Justification::centred);
    g.drawText ("ROUTING",  kSep2,  4, W - kSep2,      12, juce::Justification::centred);

    // ── Title  (bottom strip, left-aligned, no version number) ───────────
    g.setFont (juce::Font (9.5f, juce::Font::bold));
    g.setColour (th (light, Col::accentBlue, LCol::accentBlue).withAlpha (0.45f));
    g.drawText ("ADelaySR",
                4, H - 26, 140, 20, juce::Justification::centredLeft);
}

// ════════════════════════════════════════════════════════════════════════════
//  resized()
// ════════════════════════════════════════════════════════════════════════════
void ADelaySREditor::resized()
{
    //  Slot 1 (TIMING) = 200 px; slots 2–5 = 150 px each  →  800 px total
    constexpr int kSlotW  = 150;   // width of slots 2–5
    constexpr int kSlot1W = 200;   // width of slot 1

    //  Knob sizing
    constexpr int kKW     = 76;    // knob (slider) width
    constexpr int kKH     = 80;    // rotary area height
    constexpr int kTBH    = 17;    // text-box height
    constexpr int kSH     = kKH + kTBH;  // total slider height = 97
    constexpr int kKnobY  = 50;    // top of all knobs
    constexpr int kLblY   = kKnobY + kSH + 2;  // = 149

    //  Buttons
    constexpr int kBtnW  = 54;
    constexpr int kBtnH  = 21;
    constexpr int kBtnY  = 25;

    //  Slot start X coordinates
    const int s1 = 0;
    const int s2 = kSlot1W;               // 200
    const int s3 = kSlot1W + kSlotW;      // 350
    const int s4 = kSlot1W + kSlotW * 2;  // 500
    const int s5 = kSlot1W + kSlotW * 3;  // 650

    //  Helper: centre a kKW-wide control within a 150-px slot
    auto kx = [&](int slot) { return slot + (kSlotW - kKW) / 2; };  // +37

    // ── Slot 1: Timing ──────────────────────────────────────────────────────
    // SYNC (left) and NOTE (right) buttons above the knob
    syncButton     .setBounds (s1 + 4,             kBtnY, kBtnW, kBtnH);
    noteDelayButton.setBounds (s1 + 4 + kBtnW + 6, kBtnY, kBtnW, kBtnH);

    // Free-running delay knob (centred in the first 150 px of slot 1)
    delayTimeSlider.setBounds (s1 + (kSlotW - kKW) / 2, kKnobY, kKW, kSH);
    delayTimeLabel .setBounds (s1 + (kSlotW - kKW) / 2, kLblY,  kKW, 12);

    // Note-delay knob (same position, different visibility)
    noteDelaySlider.setBounds (s1 + (kSlotW - kKW) / 2, kKnobY, kKW, kSH);
    noteDelayLabel .setBounds (s1 + (kSlotW - kKW) / 2, kLblY,  kKW, 12);

    // Sync division combo (centred in first 150 px of slot 1)
    constexpr int cW = 134;
    syncDivCombo.setBounds (s1 + (kSlotW - cW) / 2, kKnobY + 27, cW, 26);
    syncDivLabel.setBounds (s1 + (kSlotW - cW) / 2, kKnobY + 11, cW, 13);

    // Delay smoothing vertical slider (in the extra 50 px at the right of slot 1)
    constexpr int kSmW = 50;   // slider + textbox width
    constexpr int kSmX = kSlotW + (kSlot1W - kSlotW - kSmW) / 2;  // centred in extra 50 px = 150 + 0 = 150
    smoothingSlider.setBounds (kSmX, kKnobY,  kSmW, kSH);
    smoothingLabel .setBounds (kSmX, kLblY,   kSmW, 12);

    // ── Slot 2: Attack ──────────────────────────────────────────────────────
    attackSlider.setBounds (kx(s2), kKnobY, kKW, kSH);
    attackLabel .setBounds (kx(s2), kLblY,  kKW, 12);

    // ── Slot 3: Release ─────────────────────────────────────────────────────
    releaseSlider.setBounds (kx(s3), kKnobY, kKW, kSH);
    releaseLabel .setBounds (kx(s3), kLblY,  kKW, 12);

    // ── Slot 4: Tap Volume ───────────────────────────────────────────────────
    tapVolSlider.setBounds (kx(s4), kKnobY, kKW, kSH);
    tapVolLabel .setBounds (kx(s4), kLblY,  kKW, 12);

    // ── Slot 5: Routing ──────────────────────────────────────────────────────
    // TRIG button at top-left of slot, 1.2x button to its right
    trigButton       .setBounds (s5 + 4,             kBtnY, kBtnW, kBtnH);
    tapVolBoostButton.setBounds (s5 + 4 + kBtnW + 6, kBtnY, kBtnW, kBtnH);

    // Mode label + combo, centred in slot
    constexpr int mW = 140;
    const int     mx = s5 + (kSlotW - mW) / 2;     // horizontally centred
    modeLabel.setBounds (mx, kKnobY + 11, mW, 13);
    modeCombo.setBounds (mx, kKnobY + 27, mW, 26);

    // WET ONLY toggle, full slot width, below mode combo
    wetOnlyButton.setBounds (mx, kKnobY + 62, mW, 21);

    // CLEAR button, below WET ONLY
    clearButton.setBounds (mx, kKnobY + 87, mW, 21);

    // ── Bottom strip: Theme button + REC button (bottom-right) ───────────
    constexpr int kRecW   = 64;   // narrowed from 82 so it stays right of the separator
    constexpr int kRecH   = 22;
    constexpr int kThemeW = 58;
    const int recX   = getWidth() - kRecW - 6;
    const int recY   = getHeight() - kRecH - 6;
    recButton  .setBounds (recX,               recY, kRecW,   kRecH);
    themeButton.setBounds (recX - kThemeW - 6, recY, kThemeW, kRecH);
}

