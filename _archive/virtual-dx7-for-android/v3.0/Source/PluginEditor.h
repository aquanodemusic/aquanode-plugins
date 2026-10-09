/*
    PluginEditor.h  -  VDX7-Dexed UI.

    This single header contains the whole (original, Dexed-inspired) interface:
    the look-and-feel, the reusable captioned rotary (ParamSlider), the live
    LCD / 7-segment readout, the algorithm view, the per-operator and global
    parameter panels, and the editor itself.

    Accessibility: every knob carries a full descriptive title (e.g. "OP3 EG
    Rate 1 (Attack)") used by screen readers and shown as a tooltip, is
    keyboard-focusable and arrow-key adjustable, and its whole tile is grabbable.

    GPLv3.
*/
#pragma once
#include <JuceHeader.h>

#include <vector>
#include <memory>
#include <algorithm>
#include <array>
#include <atomic>
#include <complex>
#include <functional>
#include <limits>
#include "PluginProcessor.h"

namespace vdx7ui {

// ============================================================================
//  Palette
// ============================================================================
namespace col {
    const juce::Colour bg        (0xff211d18);   // deep espresso
    const juce::Colour panel     (0xff2f2a22);   // panel brown
    const juce::Colour panelHi   (0xff3a342a);
    const juce::Colour edge      (0xff100d0a);
    const juce::Colour text      (0xffe8dcc8);   // warm parchment
    const juce::Colour textDim   (0xff9a8f7c);
    const juce::Colour accent    (0xffe0912f);   // amber
    const juce::Colour accentDim (0xff7a5320);
    const juce::Colour lcdBg     (0xff1a462d);   // backlit green, at the glass's edges
    const juce::Colour lcdLit    (0xff23583a);   // ... and its brighter middle
    const juce::Colour lcdOn     (0xff7dffae);   // phosphor green
    const juce::Colour ledOn     (0xffff5a44);   // red 7-seg
}

// The green glass of the LCD and the scopes, lit from behind like a real
// backlit display: a little brighter in the middle, falling off to the edges.
inline void fillBacklitGlass (juce::Graphics& g, juce::Rectangle<float> r, float cornerRadius)
{
    const auto c = r.getCentre();
    juce::ColourGradient light (col::lcdLit, c.x, c.y,
                                col::lcdBg, r.getX(), r.getY(), true);
    g.setGradientFill (light);
    g.fillRoundedRectangle (r, cornerRadius);
}

// ============================================================================
//  Look and feel
// ============================================================================
class DXLookAndFeel : public juce::LookAndFeel_V4 {
public:
    DXLookAndFeel() {
        setColour (juce::Slider::textBoxTextColourId,    col::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,            col::text);
        setColour (juce::ComboBox::backgroundColourId,   col::panelHi);
        setColour (juce::ComboBox::textColourId,         col::text);
        setColour (juce::ComboBox::outlineColourId,      col::edge);
        setColour (juce::ComboBox::arrowColourId,        col::accent);
        setColour (juce::PopupMenu::backgroundColourId,  col::panel);
        setColour (juce::PopupMenu::textColourId,        col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accentDim);
        setColour (juce::TextButton::buttonColourId,     col::panelHi);
        setColour (juce::TextButton::textColourOnId,     col::text);
        setColour (juce::TextButton::textColourOffId,    col::text);
        setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
        setColour (juce::TooltipWindow::textColourId,       col::text);
        setColour (juce::TooltipWindow::outlineColourId,    col::accentDim);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        // Reserve the top/bottom text bands so the knob never sits under a label.
        auto b = juce::Rectangle<float> ((float)x,(float)y,(float)w,(float)h).reduced (6.0f, 14.0f);
        auto r  = juce::jmax (6.0f, juce::jmin (b.getWidth(), b.getHeight()) * 0.5f);
        auto cx = b.getCentreX(), cy = b.getCentreY();
        auto angle = startAngle + pos * (endAngle - startAngle);

        g.setColour (col::edge);
        g.fillEllipse (cx - r, cy - r, r*2, r*2);
        g.setColour (s.hasKeyboardFocus (false) ? col::panel.brighter (0.15f) : col::panelHi);
        g.fillEllipse (cx - r*0.86f, cy - r*0.86f, r*1.72f, r*1.72f);

        // focus ring for keyboard users
        if (s.hasKeyboardFocus (false)) {
            g.setColour (col::accent.withAlpha (0.6f));
            g.drawEllipse (cx - r, cy - r, r*2, r*2, 1.4f);
        }

        juce::Path arc;
        arc.addCentredArc (cx, cy, r*0.98f, r*0.98f, 0.0f, startAngle, angle, true);
        g.setColour (col::accent);
        g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        juce::Path p;
        p.addRoundedRectangle (-1.4f, -r*0.82f, 2.8f, r*0.5f, 1.4f);
        g.setColour (col::text);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (cx, cy));
    }

    // Labels get the panel's small type, except ones flagged to keep the font
    // they were given (the "VirtualDX7" title) - without that flag this
    // override would quietly shrink the title to 12 px as well.
    juce::Font getLabelFont (juce::Label& l) override {
        if (l.getProperties().contains (keepFontProperty()))
            return l.getFont();
        return juce::Font (juce::FontOptions (12.0f));
    }

    static juce::Identifier keepFontProperty() { return "vdx7KeepFont"; }
};

// ============================================================================
//  CompactLookAndFeel  -  for the operator selector row: the six small output
//  level knobs that sit beside the "Operator n" buttons, and the buttons
//  themselves. There is no room for a caption band above and a value band
//  below a knob here, so the value is printed inside the knob and the amber
//  arc alone shows where it sits; the buttons use slightly smaller type so
//  "Operator 1" still fits next to its knob.
// ============================================================================
class CompactLookAndFeel : public DXLookAndFeel {
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        auto b  = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.5f);
        auto r  = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        auto cx = b.getCentreX(), cy = b.getCentreY();
        auto angle = startAngle + pos * (endAngle - startAngle);
        const int value = (int) s.getValue();

        g.setColour (col::edge);
        g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
        g.setColour (s.hasKeyboardFocus (false) ? col::panel.brighter (0.15f) : col::panelHi);
        g.fillEllipse (cx - r * 0.80f, cy - r * 0.80f, r * 1.6f, r * 1.6f);

        juce::Path track, arc;
        track.addCentredArc (cx, cy, r * 0.9f, r * 0.9f, 0.0f, startAngle, endAngle, true);
        g.setColour (col::accentDim.withAlpha (0.45f));
        g.strokePath (track, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        if (value > 0) {
            arc.addCentredArc (cx, cy, r * 0.9f, r * 0.9f, 0.0f, startAngle, angle, true);
            g.setColour (col::accent);
            g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
        if (s.hasKeyboardFocus (false)) {
            g.setColour (col::accent.withAlpha (0.6f));
            g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.2f);
        }

        g.setColour (value > 0 || s.getProperties().contains (litAtZeroProperty()) ? col::text : col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (s.getTextFromValue (s.getValue()), b, juce::Justification::centred, false);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override {
        return juce::Font (juce::FontOptions (juce::jmin (13.5f, (float) buttonHeight * 0.5f)));
    }

    // Knobs whose lowest value is a real setting rather than "off" (algorithm 1,
    // feedback 0, transpose C1) keep their number bright at the bottom too.
    static juce::Identifier litAtZeroProperty() { return "vdx7LitAtZero"; }
};

// ============================================================================
//  HeaderLookAndFeel  -  the header's second row: the FUNCTION page settings.
//  They have to share one short strip, so each value is a flat "bar": a small
//  dark slot with its caption and value printed inside and an amber fill
//  showing where the value sits (from the middle for Master Tune, which runs
//  either side of A440). Drag sideways (or scroll) to change one, double-click
//  to return to its default. The toggles and the MIDI channel box get the same
//  small type so they fit alongside.
// ============================================================================
class HeaderLookAndFeel : public CompactLookAndFeel {
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearBar) {
            CompactLookAndFeel::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
            return;
        }
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.5f);
        g.setColour (col::edge);
        g.fillRoundedRectangle (b, 4.0f);
        auto in = b.reduced (1.5f);
        g.setColour (s.hasKeyboardFocus (false) ? col::panel.brighter (0.15f) : col::panelHi);
        g.fillRoundedRectangle (in, 3.0f);

        // The fill: from the left, or from the centre for a bipolar value.
        const float px = juce::jlimit (in.getX(), in.getRight(), sliderPos);
        const bool bipolar = s.getProperties().contains (bipolarProperty());
        const float from = bipolar ? in.getCentreX() : in.getX();
        auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, px), in.getY(),
                                                                juce::jmax (from, px), in.getBottom());
        g.setColour (col::accent.withAlpha (s.isEnabled() ? 0.35f : 0.15f));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (col::accent.withAlpha (s.isEnabled() ? 0.9f : 0.4f));
        g.fillRect (juce::Rectangle<float> (px - 1.0f, in.getY() + 2.0f, 2.0f, in.getHeight() - 4.0f));

        const juce::String cap = s.getProperties() [captionProperty()].toString();
        auto text = in.reduced (4.0f, 0.0f);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (col::textDim.withAlpha (s.isEnabled() ? 1.0f : 0.5f));
        g.drawText (cap, text, juce::Justification::centredLeft, false);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.setColour (col::text.withAlpha (s.isEnabled() ? 1.0f : 0.5f));
        g.drawText (s.getTextFromValue (s.getValue()), text, juce::Justification::centredRight, false);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override {
        return juce::Font (juce::FontOptions (10.5f, juce::Font::bold));
    }

    // Text centred in the whole button, with no side insets, so the one-letter
    // P / A / E assign switches fit in their narrow slots.
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                      : juce::TextButton::textColourOffId)
                      .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (1, 2),
                          juce::Justification::centred, 1, 0.8f);
    }

    // A slimmer combo box than the panel's: a small amber chevron, so a short
    // "CH 16" still has room.
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (col::edge);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.hasKeyboardFocus (true) ? col::panel.brighter (0.15f) : col::panelHi);
        g.fillRoundedRectangle (r.reduced (1.5f), 3.0f);
        juce::Path p;
        const float cx = (float) w - 9.0f, cy = (float) h * 0.5f;
        p.startNewSubPath (cx - 3.5f, cy - 1.5f);
        p.lineTo (cx, cy + 2.0f);
        p.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (col::accent);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override {
        label.setBounds (3, 1, box.getWidth() - 16, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override {
        return juce::Font (juce::FontOptions (11.0f, juce::Font::bold));
    }
    juce::Font getPopupMenuFont() override {
        return juce::Font (juce::FontOptions (13.0f));
    }

    static juce::Identifier captionProperty() { return "vdx7Caption"; }
    static juce::Identifier bipolarProperty() { return "vdx7Bipolar"; }
};

// Shared set-up for the small value-in-the-middle knobs and the green on/off
// buttons, so every one of them behaves like the operator level knobs and the
// operator selector buttons.
inline void setupCompactKnob (juce::Slider& k, const juce::String& name, int minV, int maxV,
                              juce::LookAndFeel& lnf)
{
    k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.setRange ((double) minV, (double) maxV, 1.0);
    k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.setDoubleClickReturnValue (true, (double) minV);   // until bound: then the INIT voice's value
    k.setWantsKeyboardFocus (true);
    k.setTitle (name);
    k.setName (name);
    k.setTooltip (name);
    k.setLookAndFeel (&lnf);
}

inline void setupToggleButton (juce::TextButton& b, const juce::String& text,
                               const juce::String& name, juce::LookAndFeel& lnf)
{
    b.setButtonText (text);
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
    b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    b.setTitle (name);
    b.setTooltip (name);
    b.setLookAndFeel (&lnf);
}

// Binds a control to the APVTS parameter at a VCED offset (the editor supplies
// these, since it owns the parameter attachments).
using SliderBinder = std::function<void(juce::Slider&, int /*vcedOffset*/)>;
using ButtonBinder = std::function<void(juce::Button&, int /*vcedOffset*/)>;

// ============================================================================
//  ParamSlider  -  a captioned rotary bound to one VCED parameter.
//  The Slider fills the whole tile (the entire tile is grabbable / focusable);
//  the caption and value are painted behind it, so nothing blocks the knob.
// ============================================================================
class ParamSlider : public juce::Component {
public:
    ParamSlider (const juce::String& caption, const juce::String& fullName,
                 int minV, int maxV, int vcedOffset)
        : offset (vcedOffset), caption_ (caption)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setRange ((double) minV, (double) maxV, 1.0);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setDoubleClickReturnValue (true, (double) minV);
        slider.setVelocityBasedMode (false);
        slider.setWantsKeyboardFocus (true);

        // Accessibility: descriptive name + tooltip + value announcement.
        slider.setTitle (fullName);
        slider.setName  (fullName);
        slider.setTooltip (fullName + "  (" + juce::String (minV) + "-" + juce::String (maxV) + ")");

        slider.onValueChange = [this]{
            if (! suppress && onChange) onChange ((int) slider.getValue());
            repaint();   // refresh the printed value
        };
        addAndMakeVisible (slider);

        // The tile forwards all mouse/focus to the slider child.
        setInterceptsMouseClicks (false, true);
    }

    void resized() override { slider.setBounds (getLocalBounds()); }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (14);
        auto bot = b.removeFromBottom (14);
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (caption_, top, juce::Justification::centred);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String ((int) slider.getValue()), bot, juce::Justification::centred);
    }

    void setValueNoCallback (int v) {
        suppress = true;
        slider.setValue ((double) v, juce::dontSendNotification);
        suppress = false;
        repaint();
    }

    int value() const { return (int) slider.getValue(); }

    // Exposed so the editor can bind this knob to its APVTS parameter with a
    // juce::SliderParameterAttachment (which handles host automation both ways).
    juce::Slider& getSlider() { return slider; }

    int offset = -1;
    std::function<void(int)> onChange;

private:
    juce::Slider slider;
    juce::String caption_;
    bool suppress = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamSlider)
};

// caption, full descriptive name, min, max, absolute VCED offset
using SliderFactory =
    std::function<ParamSlider*(const juce::String&, const juce::String&, int, int, int)>;

// ============================================================================
//  LcdComponent  -  live 2x16 LCD + two 7-segment voice-number digits.
// ============================================================================
class LcdComponent : public juce::Component, private juce::Timer {
public:
    std::function<void(char[17], char[17])> lcdProvider;
    std::function<int()>  ledNumberProvider;   // 1..32
    std::function<bool()> readyProvider;

    LcdComponent() { startTimerHz (20); setTitle ("DX7 display"); }
    ~LcdComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (col::edge);
        g.fillRoundedRectangle (b, 6.0f);

        auto ledArea = b.removeFromRight (b.getHeight() * 1.5f).reduced (6.0f);
        b.removeFromRight (4.0f);

        fillBacklitGlass (g, b.reduced (4.0f), 4.0f);

        const bool ready = readyProvider && readyProvider();
        char l1[17] = {0}, l2[17] = {0};
        if (ready && lcdProvider) lcdProvider (l1, l2);
        else { std::snprintf (l1, 17, "  VDX7  BOOT... "); std::snprintf (l2, 17, " loading  ROM   "); }

        auto glass = b.reduced (12.0f);
        g.setColour (col::lcdOn);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  glass.getHeight() * 0.40f, juce::Font::plain)));
        auto rowH = glass.getHeight() * 0.5f;
        g.drawText (juce::String (juce::CharPointer_ASCII (l1)),
                    glass.removeFromTop (rowH), juce::Justification::centredLeft, false);
        g.drawText (juce::String (juce::CharPointer_ASCII (l2)),
                    glass, juce::Justification::centredLeft, false);

        int num = ledNumberProvider ? ledNumberProvider() : 0;
        drawSevenSegNumber (g, ledArea, num);
    }

private:
    void timerCallback() override { repaint(); }

    void drawSevenSegNumber (juce::Graphics& g, juce::Rectangle<float> area, int num) {
        int tens = (num / 10) % 10, ones = num % 10;
        auto dw = area.getWidth() * 0.5f;
        drawDigit (g, area.removeFromLeft (dw).reduced (3.0f), num >= 10 ? tens : -1);
        drawDigit (g, area.reduced (3.0f), num > 0 ? ones : -1);
    }

    void drawDigit (juce::Graphics& g, juce::Rectangle<float> r, int d) {
        static const bool seg[11][7] = {
            {1,1,1,1,1,1,0},{0,1,1,0,0,0,0},{1,1,0,1,1,0,1},{1,1,1,1,0,0,1},
            {0,1,1,0,0,1,1},{1,0,1,1,0,1,1},{1,0,1,1,1,1,1},{1,1,1,0,0,0,0},
            {1,1,1,1,1,1,1},{1,1,1,1,0,1,1},{0,0,0,0,0,0,0}
        };
        int idx = (d < 0) ? 10 : juce::jlimit (0, 9, d);
        float t = r.getWidth() * 0.16f;
        auto on = col::ledOn, off = col::ledOn.withAlpha (0.10f);
        auto horiz = [&](float cx, float cy, float len, bool s){
            juce::Rectangle<float> seg (cx - len/2, cy - t/2, len, t);
            g.setColour (s?on:off); g.fillRoundedRectangle (seg, t*0.4f); };
        auto vert = [&](float cx, float cy, float len, bool s){
            juce::Rectangle<float> seg (cx - t/2, cy - len/2, t, len);
            g.setColour (s?on:off); g.fillRoundedRectangle (seg, t*0.4f); };
        float w = r.getWidth(), h = r.getHeight();
        float x0 = r.getX(), y0 = r.getY();
        float segH = h * 0.42f, segW = w * 0.7f;
        horiz (x0 + w*0.5f,  y0 + h*0.08f, segW, seg[idx][0]);
        vert  (x0 + w*0.85f, y0 + h*0.28f, segH, seg[idx][1]);
        vert  (x0 + w*0.85f, y0 + h*0.72f, segH, seg[idx][2]);
        horiz (x0 + w*0.5f,  y0 + h*0.92f, segW, seg[idx][3]);
        vert  (x0 + w*0.15f, y0 + h*0.72f, segH, seg[idx][4]);
        vert  (x0 + w*0.15f, y0 + h*0.28f, segH, seg[idx][5]);
        horiz (x0 + w*0.5f,  y0 + h*0.5f,  segW, seg[idx][6]);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdComponent)
};

// ============================================================================
//  AlgoComponent  -  algorithm number, feedback, and a routing diagram that
//  shows which operator modulates which (arrows), which are carriers (drawn on
//  the output baseline) and where the feedback loop runs.
//
//  The topology is read straight from the native engine's algorithm table
//  (vdx7native::kAlgorithms), which was itself decoded from the emulator's DX7
//  algorithm ROM (External/OPS.h) - so what is drawn is exactly what is heard.
//  Using that table (a modulation *mask* per operator) rather than a single
//  "feeds into" entry per operator matters: in algorithms 19-25 and 31 one
//  modulator drives two or three operators at once, and in algorithms 4 and 6
//  the feedback loop runs back across three or two operators rather than
//  around a single one.
// ============================================================================
class AlgoComponent : public juce::Component, private juce::Timer {
public:
    std::function<int()> algoProvider;      // 0..31
    std::function<int()> feedbackProvider;  // 0..7
    std::function<std::array<int,6>()> opLevelProvider; // OP1..OP6 output levels

    AlgoComponent() { setTitle ("Algorithm routing"); startTimerHz (15); }
    ~AlgoComponent() override { stopTimer(); }

    // Just the diagram: the ALGORITHM card around it draws the frame, the
    // title and the Algorithm / Feedback / Key Sync controls.
    void paint (juce::Graphics& g) override {
        const int algo = algoProvider ? algoProvider() : 0;
        const int fb   = feedbackProvider ? feedbackProvider() : 0;
        // A little headroom above the top row for the feedback loop that
        // climbs over the topmost operator.
        drawRouting (g, getLocalBounds().reduced (2, 2).withTrimmedTop (6), algo, fb);
    }

private:
    void timerCallback() override { repaint(); }

    void drawRouting (juce::Graphics& g, juce::Rectangle<int> area, int algo, int fb) {
        const auto& a = vdx7native::kAlgorithms[juce::jlimit (0, 31, algo)];

        // 1-based operator numbers throughout, to match the panel.
        auto modulates = [&a](int src, int dst) {           // does OP src feed OP dst?
            return (a.modMask[dst - 1] & (1u << (src - 1))) != 0;
        };
        auto isCarrier = [&a](int op) { return (a.carrierMask & (1u << (op - 1))) != 0; };

        std::array<int,6> lv { {0,0,0,0,0,0} };
        if (opLevelProvider) lv = opLevelProvider();

        // Assign each operator a "rank" = distance to the output along its
        // longest chain, so modulators sit above everything they feed.
        // Carriers = rank 0.
        int rank[7] = {0,0,0,0,0,0,0};
        for (int pass = 0; pass < 6; ++pass)
            for (int src = 1; src <= 6; ++src)
                for (int dst = 1; dst <= 6; ++dst)
                    if (modulates (src, dst)) rank[src] = juce::jmax (rank[src], rank[dst] + 1);
        int maxRank = 0;
        for (int op = 1; op <= 6; ++op) maxRank = juce::jmax (maxRank, rank[op]);

        // Column per operator (OP1..OP6 left to right), row per rank (0 at bottom).
        const float boxW = juce::jmin (34.0f, area.getWidth() / 6.4f);
        const float boxH = juce::jmin (24.0f, area.getHeight() / (float) (maxRank + 1) - 5.0f);
        const float colStep = area.getWidth() / 6.0f;
        const float rowStep = (maxRank > 0)
            ? (area.getHeight() - boxH - 6.0f) / (float) maxRank : 0.0f;

        auto boxFor = [&](int op) {
            float cx = area.getX() + colStep * (op - 0.5f);
            float cy = area.getBottom() - boxH * 0.5f - 3.0f - rowStep * rank[op];
            return juce::Rectangle<float> (cx - boxW*0.5f, cy - boxH*0.5f, boxW, boxH);
        };

        // Output baseline under the carriers.
        g.setColour (col::accentDim);
        g.drawLine ((float) area.getX(), (float) area.getBottom(),
                    (float) area.getRight(), (float) area.getBottom(), 1.2f);

        // Modulation arrows (from modulator box bottom to each target's top),
        // and carriers dropping to the output baseline.
        for (int op = 1; op <= 6; ++op) {
            auto from = boxFor (op);
            if (isCarrier (op))
                drawArrow (g, from.getCentreX(), from.getBottom(),
                           from.getCentreX(), (float) area.getBottom(), col::accentDim);

            for (int dst = 1; dst <= 6; ++dst)
                if (modulates (op, dst)) {
                    auto to = boxFor (dst);
                    drawArrow (g, from.getCentreX(), from.getBottom(),
                               to.getCentreX(), to.getY(), col::accent.withAlpha (0.85f));
                }
        }

        // The feedback loop: out of the source operator, round the right-hand
        // side and back into the top of the operator it modulates (the same
        // operator in most algorithms; three or two operators up in 4 and 6).
        drawFeedbackLoop (g, boxFor (a.feedbackSrc + 1), boxFor (a.feedbackDst + 1), fb);

        // Operator boxes on top of the wiring.
        for (int op = 1; op <= 6; ++op) {
            auto r = boxFor (op);
            const bool carrier = isCarrier (op);
            const bool active  = lv[(size_t)(op-1)] > 0;
            g.setColour (carrier ? col::accentDim : col::panelHi);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (active ? col::accent : col::edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, carrier ? 1.6f : 1.0f);
            g.setColour (active ? col::text : col::textDim);
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText (juce::String (op), r, juce::Justification::centred);
        }
    }

    static void drawArrow (juce::Graphics& g, float x1, float y1, float x2, float y2,
                           juce::Colour c) {
        g.setColour (c);
        g.drawLine (x1, y1, x2, y2, 1.4f);
        juce::Path head;
        const float ang = std::atan2 (y2 - y1, x2 - x1);
        const float s = 4.0f;
        head.addTriangle (x2, y2,
                          x2 - s*std::cos (ang - 0.5f), y2 - s*std::sin (ang - 0.5f),
                          x2 - s*std::cos (ang + 0.5f), y2 - s*std::sin (ang + 0.5f));
        g.fillPath (head);
    }

    // A closed, DX7-manual style loop: down out of the source operator's
    // output, across to its right, up past the destination and back down into
    // the destination's input, finished with an arrowhead so the direction of
    // the signal is unambiguous. Brighter and heavier with more feedback;
    // dashed at 0, where the loop exists in the algorithm but carries nothing.
    static void drawFeedbackLoop (juce::Graphics& g, juce::Rectangle<float> src,
                                  juce::Rectangle<float> dst, int fb) {
        const float right = juce::jmax (src.getRight(), dst.getRight()) + 6.0f;
        const float below = src.getBottom() + 5.0f;
        const float above = dst.getY() - 6.0f;
        const float tipY  = dst.getY() - 0.5f;

        juce::Path p;
        p.startNewSubPath (src.getCentreX(), src.getBottom());
        p.lineTo (src.getCentreX(), below);
        p.lineTo (right, below);
        p.lineTo (right, above);
        p.lineTo (dst.getCentreX(), above);
        p.lineTo (dst.getCentreX(), tipY - 3.0f);
        p = p.createPathWithRoundedCorners (3.0f);

        const float amount = (float) juce::jlimit (0, 7, fb) / 7.0f;
        const auto  colour = col::lcdOn.withAlpha (fb > 0 ? 0.55f + 0.45f * amount : 0.35f);
        const juce::PathStrokeType stroke (1.1f + 0.5f * amount,
                                           juce::PathStrokeType::mitered,
                                           juce::PathStrokeType::butt);
        g.setColour (colour);
        if (fb > 0) {
            g.strokePath (p, stroke);
        } else {
            juce::Path dashed;
            const float dashes[] = { 2.5f, 2.0f };
            stroke.createDashedStroke (dashed, p, dashes, 2);
            g.fillPath (dashed);
        }

        juce::Path head;
        head.addTriangle (dst.getCentreX(),        tipY,
                          dst.getCentreX() - 3.0f, tipY - 4.0f,
                          dst.getCentreX() + 3.0f, tipY - 4.0f);
        g.fillPath (head);
    }
};

// ============================================================================
//  EnvelopeView  -  the six operator envelopes on one phosphor-green scope.
//
//  A DX7 envelope is not an ADSR. It is four rate/level pairs: from wherever
//  it is, the EG travels to L1 at rate R1, then to L2 at R2, then to L3 at R3,
//  and it *holds at L3* for as long as the key is down (L3 is the sustain
//  level, and it can be anything - including 0, which is how plucked and
//  struck sounds are made). On key-off it heads for L4 at R4; a non-zero L4
//  never finishes, it drones on at that level. Rates are exponential (roughly
//  twice as fast every 6 steps) and levels are logarithmic (0.75 dB a step).
//
//  Rather than re-derive any of that, the view runs the native engine's own
//  vdx7native::OperatorEnv - the very code that shapes the sound - for a note
//  at middle C (so rate scaling is applied as it would be there), holds the
//  key until the slowest visible operator has settled on its sustain level,
//  then releases it. The vertical axis is the envelope in dB, so a
//  constant-rate segment is a straight line, as on the DX7's own EG charts;
//  the curved rise at the start of an attack is real (the hardware accelerates
//  attacks through the quiet part of the range).
//
//  Operators whose output level is 0 contribute nothing and are not drawn.
//  The selected operator gets a soft CRT-style glow. Clicking a curve (or one
//  of the numbers in the corner) selects that operator.
//
//  Some operators are done in milliseconds while others ring for many seconds,
//  so the view zooms and scrolls in time, with mouse or finger alike: drag
//  sideways to move, drag up / down to zoom in / out around the point you
//  grabbed, or use the wheel / a trackpad (pinch or scroll). Double-click
//  shows everything again. With keyboard focus, left/right move, up/down zoom
//  and Home resets. While zoomed, a thin amber strip along the bottom shows
//  which part of the whole envelope is on screen.
// ============================================================================
class EnvelopeView : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer {
public:
    std::function<vdx7::Voice()>     voiceProvider;
    std::function<void(int)>         onOperatorClicked;    // 0..5 = OP1..OP6

    EnvelopeView() {
        setTitle ("Operator envelopes");
        setDescription ("The six operator envelope generators, as they run for a note held at middle C");
        setTooltip ("Operator envelopes for a note at middle C, held until every operator has "
                    "settled, then released at the dashed line. Click a curve to select its "
                    "operator. Drag sideways to move in time, drag up/down or scroll to zoom, "
                    "click twice to see everything.");
        setWantsKeyboardFocus (true);
        startTimerHz (20);
    }
    ~EnvelopeView() override { stopTimer(); }

    void setSelectedOperator (int op) {
        op = juce::jlimit (0, 5, op);
        if (op != selected_) { selected_ = op; repaint(); }
    }

    // Visible window as fractions (0..1) of the whole timeline.
    void setViewRange (float lo, float hi) {
        lo = juce::jlimit (0.0f, 1.0f, lo);
        hi = juce::jlimit (0.0f, 1.0f, hi);
        if (hi - lo < kMinSpan) return;
        if (! juce::exactlyEqual (lo, viewLo_) || ! juce::exactlyEqual (hi, viewHi_)) { viewLo_ = lo; viewHi_ = hi; repaint(); }
    }

    float totalSeconds() const { return totalSecs_; }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::edge);
        g.fillRoundedRectangle (b, 6.0f);
        auto glass = b.reduced (3.0f);
        fillBacklitGlass (g, glass, 4.0f);

        const auto plot = plotArea();
        const float t0 = viewLo_ * totalSecs_, t1 = viewHi_ * totalSecs_;

        // Level grid: 24 dB apart (32 DX7 level steps).
        g.setColour (col::lcdOn.withAlpha (0.07f));
        for (float db : { -24.0f, -48.0f, -72.0f })
            g.drawHorizontalLine ((int) std::round (yForDb (db, plot)), plot.getX(), plot.getRight());

        // Time grid + labels along the bottom.
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        const float step = niceStep ((t1 - t0) / 4.0f);
        for (float t = std::ceil (t0 / step) * step; t <= t1 + 1.0e-6f; t += step) {
            const float x = xForTime (t, plot);
            g.setColour (col::lcdOn.withAlpha (0.07f));
            g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());
            g.setColour (col::lcdOn.withAlpha (0.45f));
            g.drawText (formatTime (t), juce::Rectangle<float> (x - 24.0f, plot.getBottom() + 1.0f, 48.0f, 10.0f),
                        juce::Justification::centred, false);
        }

        // Key-off marker.
        if (keyOffSecs_ >= t0 && keyOffSecs_ <= t1) {
            const float x = xForTime (keyOffSecs_, plot);
            juce::Path line, dashed;
            line.startNewSubPath (x, plot.getY() - 2.0f);
            line.lineTo (x, plot.getBottom());
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (col::lcdOn.withAlpha (0.35f));
            g.fillPath (dashed);

            // Label in the caption strip above the traces, kept clear of the
            // caption and the operator legend.
            const float labelW = 44.0f;
            const float minX = getLocalBounds().toFloat().getX() + 3.0f + 6.0f + kCaptionW + 4.0f;
            const float maxX = legendCell (0).getX() - labelW - 4.0f;
            const float lx = juce::jlimit (minX, juce::jmax (minX, maxX), x - labelW * 0.5f);
            g.setColour (col::lcdOn.withAlpha (0.5f));
            g.drawText ("KEY OFF", juce::Rectangle<float> (lx, getLocalBounds().toFloat().getY() + 5.0f, labelW, 10.0f),
                        juce::Justification::centred, false);
        }

        // The curves: every visible operator in plain phosphor, then the
        // selected one again on top with its glow.
        int visibleCount = 0;
        for (int i = 0; i < 6; ++i) {
            if (! curves_[(size_t) i].visible) continue;
            ++visibleCount;
            if (i == selected_) continue;
            g.setColour (col::lcdOn.withAlpha (0.62f));
            g.strokePath (curvePath (i, plot), juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                                                      juce::PathStrokeType::rounded));
        }
        if (curves_[(size_t) selected_].visible) {
            const auto p = curvePath (selected_, plot);
            juce::Path fill (p);                       // faint phosphor wash under the trace
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (col::lcdOn.withAlpha (0.10f), 0.0f, plot.getY(),
                                                     col::lcdOn.withAlpha (0.0f),  0.0f, plot.getBottom(), false));
            g.fillPath (fill);
            // Glow: wide, faint strokes under a crisp core.
            for (auto [w, alpha] : { std::pair<float,float> { 9.0f, 0.035f }, { 6.0f, 0.06f }, { 3.5f, 0.12f } }) {
                g.setColour (col::lcdOn.withAlpha (alpha));
                g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (col::lcdOn.brighter (0.25f));
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (visibleCount == 0) {
            g.setColour (col::lcdOn.withAlpha (0.4f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("every operator is at level 0", plot, juce::Justification::centred, false);
        }

        // Caption and the operator legend (click to select).
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.45f));
        g.drawText (kCaption, juce::Rectangle<float> (glass.getX() + 6.0f, glass.getY() + 2.0f, kCaptionW, 10.0f),
                    juce::Justification::centredLeft, false);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        for (int i = 0; i < 6; ++i) {
            const auto r = legendCell (i);
            const bool vis = curves_[(size_t) i].visible;
            if (i == selected_ && vis) {
                g.setColour (col::lcdOn.withAlpha (0.14f));
                g.fillRoundedRectangle (r.reduced (0.5f), 2.0f);
            }
            g.setColour (col::lcdOn.withAlpha (! vis ? 0.18f : (i == selected_ ? 1.0f : 0.6f)));
            g.drawText (juce::String (i + 1), r, juce::Justification::centred, false);
        }

        // Where the visible window sits within the whole envelope - only
        // while zoomed, as a thin strip along the bottom of the glass.
        if (isZoomed()) {
            const auto strip = juce::Rectangle<float> (plot.getX(), glass.getBottom() - 3.0f, plot.getWidth(), 1.6f);
            g.setColour (col::lcdOn.withAlpha (0.12f));
            g.fillRect (strip);
            g.setColour (col::accent.withAlpha (0.9f));
            g.fillRect (strip.withX (strip.getX() + viewLo_ * strip.getWidth())
                             .withWidth (juce::jmax (2.0f, (viewHi_ - viewLo_) * strip.getWidth())));
        }

        if (hasKeyboardFocus (false)) {
            g.setColour (col::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
        }
    }

    // ---- navigation / selection --------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override {
        dragging_ = false;
        pressedOnLegend_ = false;
        for (int i = 0; i < 6; ++i)
            if (legendCell (i).expanded (1.0f).contains (e.position)) {
                pressedOnLegend_ = true;
                select (i);
                return;
            }
        const auto plot = plotArea();
        grabFrac_ = juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        grabLo_ = viewLo_;
        grabSpan_ = viewHi_ - viewLo_;
    }

    void mouseDrag (const juce::MouseEvent& e) override {
        if (pressedOnLegend_) return;
        if (! dragging_ && e.getDistanceFromDragStart() < 4) return;   // still a click
        dragging_ = true;

        // Up zooms in, down zooms out, around the time that was grabbed;
        // sideways drags that same moment along with the pointer.
        const auto plot = plotArea();
        const float w = juce::jmax (1.0f, plot.getWidth());
        const float span  = juce::jlimit (kMinSpan, 1.0f, grabSpan_ * std::exp ((float) e.getDistanceFromDragStartY() * 0.012f));
        const float pivot = grabLo_ + grabFrac_ * grabSpan_;             // grabbed moment, 0..1 of the whole
        const float frac  = juce::jlimit (0.0f, 1.0f, grabFrac_ + (float) e.getDistanceFromDragStartX() / w);
        const float lo    = juce::jlimit (0.0f, 1.0f - span, pivot - frac * span);
        setViewRange (lo, lo + span);
    }

    void mouseUp (const juce::MouseEvent& e) override {
        if (pressedOnLegend_ || dragging_) return;

        // A click: select the nearest visible curve at this x, if close enough.
        const auto pos  = e.position;
        const auto plot = plotArea();
        const int px = juce::jlimit (0, juce::jmax (0, (int) plot.getWidth() - 1), (int) (pos.x - plot.getX()));
        int best = -1; float bestDist = 12.0f;
        for (int i = 0; i < 6; ++i) {
            if (! curves_[(size_t) i].visible) continue;
            const float y = yForDb (bucketMax (i, px, plot), plot);
            const float d = std::abs (y - pos.y);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        if (best >= 0) select (best);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override {
        for (int i = 0; i < 6; ++i)
            if (legendCell (i).expanded (1.0f).contains (e.position)) return;
        setViewRange (0.0f, 1.0f);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        // A mostly-sideways gesture (trackpad) scrolls; otherwise the wheel zooms.
        if (std::abs (w.deltaX) > std::abs (w.deltaY)) {
            const float span = viewHi_ - viewLo_;
            const float lo = juce::jlimit (0.0f, 1.0f - span, viewLo_ - w.deltaX * span * 0.5f);
            setViewRange (lo, lo + span);
            return;
        }
        zoomAround (e.position.x, std::pow (0.85f, w.deltaY * 4.0f));
    }

    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override {
        zoomAround (e.position.x, 1.0f / juce::jmax (0.01f, scaleFactor));
    }

    bool keyPressed (const juce::KeyPress& k) override {
        const float span = viewHi_ - viewLo_;
        if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::rightKey)) {
            const float dir = k.isKeyCode (juce::KeyPress::leftKey) ? -1.0f : 1.0f;
            const float lo = juce::jlimit (0.0f, 1.0f - span, viewLo_ + dir * span * 0.1f);
            setViewRange (lo, lo + span);
            return true;
        }
        if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::downKey)) {
            zoomAround (plotArea().getCentreX(), k.isKeyCode (juce::KeyPress::upKey) ? 0.8f : 1.25f);
            return true;
        }
        if (k.isKeyCode (juce::KeyPress::homeKey)) { setViewRange (0.0f, 1.0f); return true; }
        return false;
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost   (FocusChangeType) override { repaint(); }

private:
    static constexpr float kMinSpan = 0.01f;
    static constexpr const char* kCaption = "Envelope Generator";
    static constexpr float kCaptionW = 90.0f;               // room the caption takes at 9 px
    static constexpr int   kNote    = 60;                   // middle C, for rate scaling
    static constexpr float kTick    = 32.0f / 48000.0f;     // the engine's control block at 48 kHz
    static constexpr float kCapSecs = 20.0f;                // longest hold / release simulated

    struct Curve {
        bool visible = false;
        std::vector<float> db;    // envelope level, one value per kTick
    };

    void select (int op) {
        setSelectedOperator (op);
        if (onOperatorClicked) onOperatorClicked (op);
    }

    bool isZoomed() const { return viewLo_ > 0.0005f || viewHi_ < 0.9995f; }

    // Zoom by `factor` (< 1 zooms in) keeping the moment under x where it is.
    void zoomAround (float x, float factor) {
        const auto plot = plotArea();
        const float frac  = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        const float pivot = viewLo_ + frac * (viewHi_ - viewLo_);
        const float span  = juce::jlimit (kMinSpan, 1.0f, (viewHi_ - viewLo_) * factor);
        const float lo    = juce::jlimit (0.0f, 1.0f - span, pivot - frac * span);
        setViewRange (lo, lo + span);
    }

    void timerCallback() override {
        if (! voiceProvider) return;
        const vdx7::Voice v = voiceProvider();

        // Only the bytes that shape the picture: 4 rates, 4 levels, output
        // level and rate scaling for each operator.
        std::array<uint8_t, 60> sig {};
        for (int i = 0; i < 6; ++i) {
            const int blk = 5 - i;
            for (int k = 0; k < 8; ++k) sig[(size_t) (i * 10 + k)] = v.getOp (blk, vdx7::OP_R1 + k);
            sig[(size_t) (i * 10 + 8)] = v.getOp (blk, vdx7::OP_OL);
            sig[(size_t) (i * 10 + 9)] = v.getOp (blk, vdx7::OP_RS);
        }
        const std::string name = v.name();
        const bool newPatch = (name != lastName_);   // a program change, not a knob move
        if (haveSig_ && sig == sig_ && ! newPatch) return;

        sig_ = sig; haveSig_ = true; lastName_ = name;
        rebuild (v);
        if (newPatch) { viewLo_ = 0.0f; viewHi_ = 1.0f; }   // a new patch starts fully zoomed out
        repaint();
    }

    void rebuild (const vdx7::Voice& v) {
        using namespace vdx7native;

        struct OpEg { uint8_t r[4], l[4]; float rateOffset; bool visible; };
        OpEg ops[6];
        for (int i = 0; i < 6; ++i) {
            const int blk = 5 - i;                     // VCED block 5 is OP1
            for (int k = 0; k < 4; ++k) {
                ops[i].r[k] = v.getOp (blk, vdx7::OP_R1 + k);
                ops[i].l[k] = v.getOp (blk, vdx7::OP_L1 + k);
            }
            ops[i].rateOffset = rateScaleOffset (v.getOp (blk, vdx7::OP_RS), kNote);
            ops[i].visible    = v.getOp (blk, vdx7::OP_OL) > 0;
        }

        auto start = [] (const OpEg& o) {
            OperatorEnv e;
            e.configure (o.r, o.l, o.rateOffset);
            e.reset (-kEgRangeDb);                     // a fresh note starts from silence
            e.keyOn();
            return e;
        };
        const int cap = (int) (kCapSecs / kTick);

        // Pass 1: how long each visible operator takes to settle on L3. The
        // envelope is settled once it stops moving for a few ticks in a row.
        float settle = 0.0f;
        for (const auto& o : ops) {
            if (! o.visible) continue;
            auto e = start (o);
            float prev = 1.0e9f; int still = 0, n = 0;
            for (; n < cap; ++n) {
                const float d = e.tick (kTick);
                if (juce::exactlyEqual (d, prev)) { if (++still >= 3) break; } else still = 0;
                prev = d;
            }
            settle = juce::jmax (settle, (float) n * kTick);
        }

        // Hold the key a little past that, so the sustain reads as a plateau.
        const float hold = juce::jlimit (0.25f, kCapSecs, settle + juce::jmax (0.35f * settle, 0.25f));
        const int holdTicks = (int) std::ceil (hold / kTick);

        // Pass 2: the full curves, key held then released.
        float releaseLen = 0.0f;
        for (int i = 0; i < 6; ++i) {
            auto& c = curves_[(size_t) i];
            c.visible = ops[i].visible;
            c.db.clear();
            if (! c.visible) continue;

            auto e = start (ops[i]);
            c.db.reserve ((size_t) holdTicks + 4096);
            for (int n = 0; n < holdTicks; ++n) c.db.push_back (e.tick (kTick));
            e.keyOff();
            float prev = 1.0e9f; int still = 0;
            for (int n = 0; n < cap; ++n) {
                const float d = e.tick (kTick);
                c.db.push_back (d);
                if (e.finished()) break;
                if (juce::exactlyEqual (d, prev)) { if (++still >= 3) break; } else still = 0;   // parked on a non-zero L4
                prev = d;
            }
            releaseLen = juce::jmax (releaseLen, (float) (c.db.size() - (size_t) holdTicks) * kTick);
        }

        keyOffSecs_ = hold;
        totalSecs_  = niceCeil (hold + releaseLen * 1.08f + 0.02f);
    }

    // ---- geometry ---------------------------------------------------------
    juce::Rectangle<float> plotArea() const {
        return getLocalBounds().toFloat().reduced (3.0f).withTrimmedTop (13.0f)
                   .withTrimmedBottom (12.0f).reduced (7.0f, 2.0f);
    }

    juce::Rectangle<float> legendCell (int i) const {
        const auto glass = getLocalBounds().toFloat().reduced (3.0f);
        const float w = 11.0f;
        return { glass.getRight() - 6.0f - w * (float) (6 - i), glass.getY() + 2.0f, w, 11.0f };
    }

    float xForTime (float t, juce::Rectangle<float> plot) const {
        const float t0 = viewLo_ * totalSecs_, t1 = viewHi_ * totalSecs_;
        return plot.getX() + (t - t0) / juce::jmax (1.0e-6f, t1 - t0) * plot.getWidth();
    }

    static float yForDb (float db, juce::Rectangle<float> plot) {
        const float lo = vdx7native::silenceDb();
        const float n  = (juce::jlimit (lo, 0.0f, db) - lo) / (0.0f - lo);
        return plot.getBottom() - n * plot.getHeight();
    }

    // Loudest point of curve i within pixel column px - so a blip much shorter
    // than a pixel still shows at its full height when zoomed out.
    float bucketMax (int i, int px, juce::Rectangle<float> plot) const {
        const auto& c = curves_[(size_t) i];
        if (c.db.empty()) return -vdx7native::kEgRangeDb;
        const float w  = juce::jmax (1.0f, plot.getWidth());
        const float t0 = viewLo_ * totalSecs_, t1 = viewHi_ * totalSecs_;
        const float ta = t0 + (t1 - t0) * (float) px / w;
        const float tb = t0 + (t1 - t0) * (float) (px + 1) / w;
        const int ia = (int) (ta / kTick);
        const int ib = juce::jmax (ia + 1, (int) (tb / kTick));
        const int last = (int) c.db.size() - 1;
        float mx = -1.0e9f;
        for (int k = ia; k < ib; ++k)
            mx = juce::jmax (mx, c.db[(size_t) juce::jmin (k, last)]);   // past the end: holds its last value
        return mx;
    }

    juce::Path curvePath (int i, juce::Rectangle<float> plot) const {
        juce::Path p;
        const int w = juce::jmax (1, (int) plot.getWidth());
        // Every note starts from silence; show that when the window begins at
        // key-on, so the attack visibly rises out of nothing.
        const bool fromKeyOn = juce::exactlyEqual (viewLo_, 0.0f);
        if (fromKeyOn) p.startNewSubPath (plot.getX(), plot.getBottom());
        for (int px = 0; px <= w; ++px) {
            const float x = plot.getX() + (float) px;
            const float y = yForDb (bucketMax (i, px, plot), plot);
            if (px == 0 && ! fromKeyOn) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    }

    static float niceCeil (float secs) {
        static const float steps[] = { 0.1f, 0.2f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f,
                                       6.0f, 8.0f, 10.0f, 12.0f, 15.0f, 20.0f, 25.0f, 30.0f, 40.0f, 50.0f };
        for (float s : steps) if (s >= secs) return s;
        return 60.0f;
    }

    static float niceStep (float approx) {
        static const float steps[] = { 0.005f, 0.01f, 0.02f, 0.025f, 0.05f, 0.1f, 0.2f, 0.25f, 0.5f,
                                       1.0f, 2.0f, 2.5f, 5.0f, 10.0f };
        for (float s : steps) if (s >= approx) return s;
        return 20.0f;
    }

    static juce::String formatTime (float t) {
        if (t < 0.0005f) return "0";
        if (t < 0.9995f) return juce::String (juce::roundToInt (t * 1000.0f)) + "ms";
        return juce::String (t, (std::abs (t - std::round (t)) < 1.0e-3f) ? 0 : 2) + "s";
    }

    std::array<Curve, 6> curves_;
    std::array<uint8_t, 60> sig_ {};
    bool  haveSig_ = false;
    std::string lastName_;
    int   selected_ = 0;
    float totalSecs_ = 1.0f, keyOffSecs_ = 0.5f;
    float viewLo_ = 0.0f, viewHi_ = 1.0f;
    bool  dragging_ = false, pressedOnLegend_ = false;
    float grabFrac_ = 0.0f, grabLo_ = 0.0f, grabSpan_ = 1.0f;   // view state when the drag began

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeView)
};


// ============================================================================
//  CardFrame  -  the brown module card with its amber title band, as painted by
//  OperatorPanel / GlobalPanel / FxUnitPanel, for controls that are laid out
//  by the editor itself (the OPERATORS card behind the selector buttons and
//  level knobs). Purely decorative: it never takes a click.
// ============================================================================
class CardFrame : public juce::Component {
public:
    explicit CardFrame (const juce::String& titleText) : title_ (titleText) {
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto hdr = b.removeFromTop (22);
        g.setColour (col::accentDim);
        g.fillRoundedRectangle (hdr.toFloat().reduced (3, 3), 4.0f);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (title_, hdr, juce::Justification::centred);
    }

private:
    juce::String title_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CardFrame)
};

// ============================================================================
//  drawGlass  -  the phosphor-green "glass" the LCD, the scopes and the
//  readouts all share: a dark edge, the green-black glass, and a small dim
//  caption in its top-left corner. Returns the area left for content.
// ============================================================================
inline juce::Rectangle<float> drawGlass (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& caption)
{
    g.setColour (col::edge);
    g.fillRoundedRectangle (r, 5.0f);
    auto in = r.reduced (2.5f);
    fillBacklitGlass (g, in, 3.5f);
    g.setColour (col::lcdOn.withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText (caption, in.reduced (6.0f, 2.0f).removeFromTop (10.0f), juce::Justification::centredLeft, false);
    return in.reduced (6.0f, 2.0f).withTrimmedTop (11.0f);
}

// ============================================================================
//  AlgorithmCard  -  the ALGORITHM module: a top row with the Algorithm and
//  Feedback knobs (small, value in the middle, like the operator level knobs)
//  and the oscillator Key Sync switch, and the routing diagram underneath.
//  The algorithm knob shows 1..32, the numbers the DX7 prints and the diagram
//  uses, although the parameter itself runs 0..31.
// ============================================================================
class AlgorithmCard : public juce::Component {
    CompactLookAndFeel lnf_;   // declared first: destroyed after the controls using it

public:
    AlgoComponent diagram;     // the editor wires its providers

    AlgorithmCard() {
        setupCompactKnob (algoKnob, "Algorithm (1-32)", 0, 31, lnf_);
        algoKnob.getProperties().set (CompactLookAndFeel::litAtZeroProperty(), true);
        setupCompactKnob (fbKnob, "Feedback (0-7)", 0, 7, lnf_);
        setupToggleButton (keySync, "Key Sync", "Oscillator Key Sync", lnf_);
        keySync.setTooltip ("Oscillator Key Sync: when on, every note starts its operators from the "
                            "same point in their cycle, so each note attacks the same way");
        addAndMakeVisible (diagram);
        addAndMakeVisible (algoKnob);
        addAndMakeVisible (fbKnob);
        addAndMakeVisible (keySync);
    }

    ~AlgorithmCard() override {
        algoKnob.setLookAndFeel (nullptr);
        fbKnob.setLookAndFeel (nullptr);
        keySync.setLookAndFeel (nullptr);
    }

    void bind (const SliderBinder& bindSlider, const ButtonBinder& bindButton) {
        bindSlider (algoKnob, vdx7::G_ALG);
        bindSlider (fbKnob,   vdx7::G_FB);
        bindButton (keySync,  vdx7::G_OKS);
        // After binding: the parameter attachment installs its own number text.
        algoKnob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v) + 1); };
        algoKnob.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() - 1.0; };
    }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto hdr = b.removeFromTop (22);
        g.setColour (col::accentDim);
        g.fillRoundedRectangle (hdr.toFloat().reduced (3, 3), 4.0f);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText ("GLOBAL  |  ALGORITHM", hdr, juce::Justification::centred);

        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Algorithm", algoLabel_, juce::Justification::centredLeft);
        g.drawText ("Feedback",  fbLabel_,   juce::Justification::centredLeft);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromTop (24);
        b = b.reduced (8, 4);

        auto row = b.removeFromTop (30);
        algoLabel_ = row.removeFromLeft (60);
        algoKnob.setBounds (row.removeFromLeft (30));
        row.removeFromLeft (14);
        fbLabel_ = row.removeFromLeft (56);
        fbKnob.setBounds (row.removeFromLeft (30));
        row.removeFromLeft (14);
        keySync.setBounds (row);

        b.removeFromTop (4);
        diagram.setBounds (b);
    }

private:
    juce::Slider     algoKnob, fbKnob;
    juce::TextButton keySync;
    juce::Rectangle<int> algoLabel_, fbLabel_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlgorithmCard)
};

// ============================================================================
//  OpReadouts  -  the bottom row of an operator card: what the knobs above it
//  mean in real units, on the same phosphor-green glass as the LCD and the
//  envelope scope. One box under each knob group:
//
//    FREQUENCY    the ratio (or fixed frequency) the coarse/fine pair really
//                 produces - coarse 3 + fine 50 is x4.50, not x3.50 - the
//                 pitch that gives at C3, and the detune in cents;
//    KEY SCALING  this operator's level across the keyboard, from the break
//                 point, depths and curves, including the clamp at full scale
//                 that makes a boosting curve do nothing on a level-99 operator;
//    ENVELOPE     how long each of the four EG segments takes at C3 (rate
//                 scaling included) and the level it heads for, one column
//                 under each rate/level knob pair.
//
//  Everything is computed with the native engine's own functions, so the
//  numbers are the ones the sound follows.
// ============================================================================
class OpReadouts : public juce::Component, private juce::Timer {
public:
    std::function<int(int)> param;        // vdx7::OP_* -> this operator's current value
    std::function<int()>    outputLevel;  // this operator's output level (0..99)

    OpReadouts() {
        setInterceptsMouseClicks (false, false);
        setTitle ("Operator readouts");
        startTimerHz (12);
    }
    ~OpReadouts() override { stopTimer(); }

    // x of each knob group's left edge plus the right edge of the last, and
    // the knob column width, in this component's coordinates.
    void setColumns (const std::array<int,4>& groupX, int columnWidth) {
        groupX_ = groupX; cw_ = columnWidth; repaint();
    }

    void paint (juce::Graphics& g) override {
        if (! param) return;
        drawFrequency  (g, box (0));
        drawKeyScaling (g, box (1));
        drawEnvelope   (g, box (2));
    }

private:
    static constexpr int kRefNote = 60;   // C3 on the DX7 (and on the on-screen keyboard)

    juce::Rectangle<float> box (int group) const {
        return juce::Rectangle<float> ((float) groupX_[(size_t) group], 0.0f,
                                       (float) (groupX_[(size_t) group + 1] - groupX_[(size_t) group]),
                                       (float) getHeight()).reduced (4.0f, 3.0f);
    }

    // Glass box with a small caption; returns the area left for content.
    static juce::Rectangle<float> glass (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& caption) {
        return drawGlass (g, r, caption);
    }

    static juce::Font mono (float size) {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
    }

    void drawFrequency (juce::Graphics& g, juce::Rectangle<float> r) {
        using namespace vdx7native;
        auto in = glass (g, r, "FREQUENCY");
        const bool fixed = param (vdx7::OP_MODE) != 0;
        const int  crs = param (vdx7::OP_FC), fine = param (vdx7::OP_FF), det = param (vdx7::OP_DET);
        const float cents = detuneCents (det);
        const float mul   = std::exp2 (cents / 1200.0f);

        juce::String l1, l2;
        if (fixed) {
            l1 = "FIXED  " + hz (fixedHzFromCoarseFine (crs, fine) * mul);
            l2 = "on every key";
        } else {
            const float ratio = ratioFromCoarseFine (crs, fine);
            const float c3 = 440.0f * std::exp2 ((float) (kRefNote - 69) / 12.0f);
            l1 = "RATIO  x" + juce::String (ratio, 2);
            l2 = "at C3  " + hz (c3 * ratio * mul);
        }
        const juce::String l3 = (det == 7) ? juce::String ("DETUNE centre")
                                           : "DETUNE " + juce::String (cents >= 0.0f ? "+" : "") + juce::String (cents, 1) + " ct";
        lines (g, in, { l1, l2, l3 });
    }

    void drawKeyScaling (juce::Graphics& g, juce::Rectangle<float> r) {
        using namespace vdx7native;
        auto in = glass (g, r, "KEY SCALING");
        const int bp = param (vdx7::OP_BP), ld = param (vdx7::OP_LD), rd = param (vdx7::OP_RD);
        const int lc = param (vdx7::OP_LC), rc = param (vdx7::OP_RC);
        const int olUnits = scaleOutLevel (outputLevel ? outputLevel() : 99);

        // The level change, in dB, the scaling makes at `note` - summed in the
        // hardware's level units and clamped at full scale, as the engine does.
        auto deltaDb = [&] (int note) {
            const int scaled = juce::jlimit (0, 127, olUnits + keyScaleUnits (note, bp, ld, rd, lc, rc));
            return unitsToDb (scaled) - unitsToDb (olUnits);
        };

        const int lo = 24, hi = 108;                  // C1..C8 on this keyboard's naming (C3 = 60)
        const float topDb = 12.0f, botDb = -48.0f;
        auto plot = in.withTrimmedBottom (9.0f).withTrimmedTop (1.0f);
        auto xFor = [&] (float note) { return plot.getX() + (note - (float) lo) / (float) (hi - lo) * plot.getWidth(); };
        auto yFor = [&] (float db)   { return plot.getY() + (topDb - juce::jlimit (botDb, topDb, db)) / (topDb - botDb) * plot.getHeight(); };

        // 0 dB line, an octave tick at every C, labels at C1 / C3 / C5 / C7.
        g.setColour (col::lcdOn.withAlpha (0.18f));
        g.drawHorizontalLine ((int) std::round (yFor (0.0f)), plot.getX(), plot.getRight());
        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        for (int n = 24; n <= hi; n += 12) {
            const float x = xFor ((float) n);
            g.setColour (col::lcdOn.withAlpha (0.10f));
            g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());
            if ((n / 12) % 2 == 1) {                  // C1, C3, C5, C7
                g.setColour (col::lcdOn.withAlpha (0.4f));
                g.drawText ("C" + juce::String (n / 12 - 2), juce::Rectangle<float> (x - 10.0f, plot.getBottom() + 0.5f, 20.0f, 8.0f),
                            juce::Justification::centred, false);
            }
        }

        // Where the curve bends: the break point as the engine applies it.
        const float bx = juce::jlimit (plot.getX(), plot.getRight(), xFor ((float) (bp + 17)));
        g.setColour (col::lcdOn.withAlpha (0.35f));
        g.drawVerticalLine ((int) std::round (bx), plot.getY(), plot.getBottom());

        juce::Path p;
        for (int n = lo; n <= hi; ++n) {
            const float x = xFor ((float) n), y = yFor (deltaDb (n));
            if (n == lo) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col::lcdOn.withAlpha (0.12f));
        g.strokePath (p, juce::PathStrokeType (4.0f));
        g.setColour (col::lcdOn);
        g.strokePath (p, juce::PathStrokeType (1.4f));

        // A small marker on top of the break-point line, keyed in the caption row.
        juce::Path tri;
        tri.addTriangle (bx - 3.0f, plot.getY() - 4.0f, bx + 3.0f, plot.getY() - 4.0f, bx, plot.getY() + 1.0f);
        g.setColour (col::lcdOn.withAlpha (0.7f));
        g.fillPath (tri);
        g.setColour (col::lcdOn.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc break point")),
                    juce::Rectangle<float> (r.getX() + 6.0f, r.getY() + 4.5f, r.getWidth() - 12.0f, 10.0f),
                    juce::Justification::centredRight, false);
    }

    void drawEnvelope (juce::Graphics& g, juce::Rectangle<float> r) {
        using namespace vdx7native;
        auto in = glass (g, r, "ENVELOPE @ C3");
        const float rateOffset = rateScaleOffset (param (vdx7::OP_RS), kRefNote);

        auto rate  = [&] (int k) { return rateToDbPerSecond ((float) param (vdx7::OP_R1 + k) + rateOffset); };
        auto level = [&] (int k) { return levelToDb (param (vdx7::OP_L1 + k)); };

        // A segment's duration, exactly as OperatorEnv runs it: a falling
        // segment is a straight line in dB; a rising one starts from the
        // attack floor (never lower) and is sped up through the quiet part of
        // the range, which integrates to the logarithm below.
        auto seg = [] (float fromDb, float toDb, float dbPerSec) -> float {
            if (std::abs (toDb - fromDb) < 0.01f) return -1.0f;         // nothing to travel
            if (toDb < fromDb) return (fromDb - toDb) / dbPerSec;
            constexpr float floorDb = -kEgRangeDb * (1.0f - 1716.0f / 3840.0f);
            fromDb = juce::jmax (fromDb, floorDb);
            if (toDb <= fromDb) return 0.0f;
            const float k = 15.0f / kEgRangeDb;
            return std::log ((2.0f - k * fromDb) / (2.0f - k * toDb)) / (k * dbPerSec);
        };

        const float start = -kEgRangeDb;                       // a new note starts from silence
        const float t[4] = { seg (start,    level (0), rate (0)),
                             seg (level (0), level (1), rate (1)),
                             seg (level (1), level (2), rate (2)),
                             seg (level (2), level (3), rate (3)) };
        static const char* names[4] = { "Attack", "Decay 1", "Decay 2", "Release" };

        // One column under each rate/level knob pair above.
        for (int k = 0; k < 4; ++k) {
            const float x0 = (float) (groupX_[2] + k * cw_);
            auto cell = juce::Rectangle<float> (x0, in.getY(), (float) cw_, in.getHeight())
                            .getIntersection (in);
            if (k > 0) {
                g.setColour (col::lcdOn.withAlpha (0.10f));
                g.drawVerticalLine ((int) std::round (x0), in.getY() + 1.0f, in.getBottom() - 1.0f);
            }
            const juce::String lv = (param (vdx7::OP_L1 + k) == 0) ? juce::String ("to off")
                                  : "to " + juce::String (juce::roundToInt (level (k))) + " dB";
            lines (g, cell, { names[k], time (t[k]), lv }, true);
        }
    }

    // Three lines of readout text: the first dim (a label), the rest bright,
    // or all bright when `firstIsLabel` is false.
    static void lines (juce::Graphics& g, juce::Rectangle<float> r,
                       std::initializer_list<juce::String> text, bool centred = false) {
        const float lh = r.getHeight() / 3.0f;
        int i = 0;
        for (const auto& s : text) {
            const bool label = centred && i == 0;
            g.setColour (col::lcdOn.withAlpha (label ? 0.5f : 1.0f));
            g.setFont (label ? juce::Font (juce::FontOptions (9.0f)) : mono (11.0f));
            g.drawText (s, r.withY (r.getY() + lh * (float) i).withHeight (lh),
                        centred ? juce::Justification::centred : juce::Justification::centredLeft, false);
            ++i;
        }
    }

    static juce::String hz (float f) {
        return f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (f, 1) + " Hz";
    }

    static juce::String time (float s) {
        if (s < 0.0f)    return "--";          // segment does not move
        if (s < 0.0005f) return "instant";
        if (s < 1.0f)    return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
        if (s < 10.0f)   return juce::String (s, 2) + " s";
        if (s < 60.0f)   return juce::String (s, 1) + " s";
        return juce::String (s / 60.0f, 1) + " min";
    }

    void timerCallback() override {
        if (! isShowing() || ! param) return;
        std::array<int, 22> sig {};
        for (int k = 0; k < 21; ++k) sig[(size_t) k] = param (k);
        sig[21] = outputLevel ? outputLevel() : 99;
        if (sig != sig_) { sig_ = sig; repaint(); }
    }

    std::array<int,4>  groupX_ {};
    int                cw_ = 1;
    std::array<int,22> sig_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpReadouts)
};

// ============================================================================
//  OperatorPanel  -  one DX7 operator's 21 editable parameters.
// ============================================================================
class OperatorPanel : public juce::Component {
public:
    // displayIndex: 1..6 (OP1..OP6).  vcedOp: 0..5 block index in the VCED.
    //
    // 20 of the operator's 21 knobs, laid out in 3 column-groups, each its own
    // little grid, with a light vertical divider between groups so related
    // controls read as a block instead of one undifferentiated row:
    //   FREQ/SCALE (3 cols): CRS FINE DET / BKPT LDEP RDEP
    //   MODE/SENS  (3 cols): MODE AMS KVS / LCRV RCRV RS
    //   ENVELOPE   (4 cols): R1 R2 R3 R4  / L1 L2 L3 L4
    // The output level is not here: all six operators' levels sit beside the
    // operator selector buttons instead, so they can be balanced at a glance
    // without flipping between operators. The 3+3+4 = 10 column-unit total
    // matches the original single 10-column grid, so knob size is unchanged.
    OperatorPanel (int displayIndex, int vcedOp, SliderFactory make)
        : opDisplay (displayIndex)
    {
        using namespace vdx7;
        const int base = vcedOp * kOpVcedStride;
        const juce::String opTag = "OP" + juce::String (displayIndex) + " ";
        auto add = [&](int group, const juce::String& cap, const juce::String& full,
                       int mn, int mx, int off){
            auto* s = make (cap, opTag + full, mn, mx, base + off);
            owned.emplace_back (s);
            addAndMakeVisible (s);
            groups[(size_t) group].push_back (s);
            byParam[(size_t) off] = s;
        };
        // Group 0 - FREQUENCY (the output level lives beside the selector buttons)
        add (0, "Coarse", "Frequency Coarse",          0, 31, OP_FC);
        add (0, "Fine Tune", "Frequency Fine",            0, 99, OP_FF);
        add (0, "Detune", "Detune (7 = centre)",       0, 14, OP_DET);
        // Group 1 - MODE / SENS
        add (1, "Osc Mode", "Oscillator Mode (0 ratio / 1 fixed)", 0, 1, OP_MODE);
        add (1, "Amp Mod Sens", "Amplitude Mod Sensitivity",    0, 3,  OP_AMS);
        add (1, "Vel Sens", "Key Velocity Sensitivity",     0, 7,  OP_KVS);
        // Group 2 - ENVELOPE (rates)
        add (2, "Attack Rate", "EG Rate 1 (attack)",        0, 99, OP_R1);
        add (2, "Decay1 Rate", "EG Rate 2 (decay 1)",       0, 99, OP_R2);
        add (2, "Decay2 Rate", "EG Rate 3 (decay 2)",       0, 99, OP_R3);
        add (2, "Release Rate", "EG Rate 4 (release)",       0, 99, OP_R4);
        // Group 0 - row 2 (keyboard scaling break point + depth)
        add (0, "Break Point", "Keyboard Scaling Break Point", 0, 99, OP_BP);
        add (0, "Left Depth", "Keyboard Scaling Left Depth",  0, 99, OP_LD);
        add (0, "Right Depth", "Keyboard Scaling Right Depth", 0, 99, OP_RD);
        // Group 1 - MODE / SENS, row 2 (keyboard scaling curves + rate scaling)
        add (1, "Left Curve", "Keyboard Scaling Left Curve",  0, 3,  OP_LC);
        add (1, "Right Curve", "Keyboard Scaling Right Curve", 0, 3,  OP_RC);
        add (1, "Rate Scale", "Keyboard Rate Scaling",        0, 7,  OP_RS);
        // Group 2 - ENVELOPE (levels)
        add (2, "Attack Level", "EG Level 1 (attack)",       0, 99, OP_L1);
        add (2, "Decay1 Level", "EG Level 2 (decay 1)",      0, 99, OP_L2);
        add (2, "Sustain Lvl", "EG Level 3 (sustain)",      0, 99, OP_L3);
        add (2, "Release Lvl", "EG Level 4 (release)",      0, 99, OP_L4);

        // Third row: what those knobs mean in real units (see OpReadouts).
        readouts.param = [this] (int opParam) {
            auto* k = (opParam >= 0 && opParam < vdx7::kOpVcedStride) ? byParam[(size_t) opParam] : nullptr;
            return k != nullptr ? k->value() : 0;
        };
        addAndMakeVisible (readouts);
    }

    // The output level knob lives in the editor (beside the selector buttons);
    // the key-scaling readout needs it for the full-scale clamp.
    void setOutputLevelProvider (std::function<int()> fn) { readouts.outputLevel = std::move (fn); }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto hdr = b.removeFromTop (22);
        g.setColour (col::accentDim);
        g.fillRoundedRectangle (hdr.toFloat().reduced (3, 3), 4.0f);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText ("OPERATOR " + juce::String (opDisplay), hdr, juce::Justification::centred);

        // Light vertical separators between the three knob groups, spanning
        // the two knob rows.
        g.setColour (col::edge.withAlpha (0.6f));
        for (int i = 1; i < 3; ++i)
            g.drawLine ((float) groupX[(size_t) i], (float) knobRowsTop,
                        (float) groupX[(size_t) i], (float) (knobRowsTop + knobRowsHeight), 1.0f);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromTop (24);
        b = b.reduced (4);

        // 3 column-units for group 0, 3 for group 1, 4 for group 2: 10 total,
        // matching the knob width of the original flat 10-column grid.
        static const int groupCols[3] = { 3, 3, 4 };
        const int cw = b.getWidth() / 10;
        const int rows = 3;   // knob size as before: 2 knob rows in a 3-row pitch
        const int ch = b.getHeight() / rows;

        int x = b.getX();
        for (int g = 0; g < 3; ++g) {
            groupX[(size_t) g] = x;
            auto& items = groups[(size_t) g];
            const int cols = groupCols[g];
            for (int i = 0; i < (int) items.size(); ++i) {
                const int r = i / cols, c = i % cols;
                items[(size_t) i]->setBounds (x + c * cw, b.getY() + r * ch, cw, ch);
            }
            x += groupCols[g] * cw;
        }
        groupX[3] = x;

        knobRowsTop = b.getY();
        knobRowsHeight = 2 * ch;

        readouts.setBounds (b.getX(), b.getY() + 2 * ch, groupX[3] - b.getX(), b.getBottom() - (b.getY() + 2 * ch));
        readouts.setColumns ({ 0, groupX[1] - b.getX(), groupX[2] - b.getX(), groupX[3] - b.getX() }, cw);
    }

private:
    int opDisplay;
    std::array<ParamSlider*, vdx7::kOpVcedStride> byParam {};   // by vdx7::OP_* (output level stays null)
    OpReadouts readouts;
    std::vector<std::unique_ptr<ParamSlider>> owned;
    std::array<std::vector<ParamSlider*>, 3> groups;
    int groupX[4] {};             // x of each group's left edge, plus the right edge of the last
    int knobRowsTop = 0, knobRowsHeight = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OperatorPanel)
};

// ============================================================================
//  LfoScope  -  what the LFO does to a note, on a small green glass. Left, the
//  LFO Delay: flat while it holds the LFO off, then a wedge opening up as the
//  wave fades in (wider for a longer delay, but never more than half the
//  width). Right, three cycles of the wave at full swing - always three, so a
//  fast LFO does not turn into a solid band and a long delay does not squeeze
//  the wave. The caption names the wave and its rate; the bottom line gives
//  the vibrato depth in cents (Pitch Depth x Pitch Sens), the amplitude depth
//  and the delay time. With LFO Key Sync off, two faint extra traces show that
//  a note can start anywhere in the cycle.
//
//  Rates, delay times and depths come from the native engine's own tables.
// ============================================================================
class LfoScope : public juce::Component, private juce::Timer {
public:
    std::function<int(int)> param;     // VCED offset (vdx7::G_*) -> current value
    std::function<bool()>   keySync;   // the LFO Key Sync switch

    LfoScope() {
        setInterceptsMouseClicks (false, false);
        setTitle ("LFO shape");
        startTimerHz (12);
    }
    ~LfoScope() override { stopTimer(); }

    void paint (juce::Graphics& g) override {
        if (! param) return;
        using namespace vdx7native;
        const int   wave = juce::jlimit (0, 5, param (vdx7::G_LFW));
        const float hz   = lfoSpeedToHz (param (vdx7::G_LFS));
        float hold = 0.0f, fade = 0.0f;
        lfoDelayTimes (param (vdx7::G_LFD), hold, fade);
        const int  pmd = param (vdx7::G_LPMD), amd = param (vdx7::G_LAMD), pms = param (vdx7::G_LPMS);
        const bool sync = keySync && keySync();

        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "LFO");
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.8f));
        g.drawText (juce::String (kWaveNames[wave]) + "  " + rate (hz),
                    r.reduced (2.5f).reduced (6.0f, 2.0f).removeFromTop (10.0f), juce::Justification::centredRight, false);

        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);
        auto yFor = [&] (float v) { return plot.getCentreY() - v * plot.getHeight() * 0.46f; };

        // Dimmed when neither depth is up: the LFO runs but touches nothing.
        const float cents = (float) pmd / 99.0f * pmsSemitones (pms) * 100.0f;
        const bool  inUse = cents >= 0.5f || amd > 0;

        g.setColour (col::lcdOn.withAlpha (0.14f));
        g.drawHorizontalLine ((int) std::round (plot.getCentreY()), plot.getX(), plot.getRight());

        // Two parts, so neither a long delay nor a fast LFO can crush the
        // picture. Left: the delay - flat while it holds the LFO off, then a
        // wedge opening up as the wave fades in. It gets wider with a longer
        // delay, but never takes more than half the width. Right: always the
        // same three cycles of the wave at full swing, so its shape reads the
        // same at 0.1 Hz and at 49 Hz.
        const float d      = hold + fade;
        const float delayW = d > 0.001f ? plot.getWidth() * 0.5f * d / (d + 0.8f) : 0.0f;
        const float xRun   = plot.getX() + delayW;
        const float runW   = plot.getRight() - xRun;
        const juce::Colour traceCol = col::lcdOn.withAlpha (inUse ? 1.0f : 0.4f);

        if (delayW > 0.0f) {
            const float xFade = plot.getX() + delayW * (hold / d);
            g.setColour (traceCol);
            g.drawLine (plot.getX(), plot.getCentreY(), xFade, plot.getCentreY(), 1.3f);

            juce::Path wedge;
            wedge.startNewSubPath (xFade, plot.getCentreY());
            wedge.lineTo (xRun, yFor (1.0f));
            wedge.lineTo (xRun, yFor (-1.0f));
            wedge.closeSubPath();
            g.setColour (col::lcdOn.withAlpha (inUse ? 0.22f : 0.08f));
            g.fillPath (wedge);
            g.setColour (col::lcdOn.withAlpha (inUse ? 0.5f : 0.2f));
            g.strokePath (wedge, juce::PathStrokeType (1.0f));

            juce::Path sep, dotted;                   // where the delay ends
            sep.startNewSubPath (xRun, plot.getY());
            sep.lineTo (xRun, plot.getBottom());
            const float dots[] = { 1.5f, 2.5f };
            juce::PathStrokeType (1.0f).createDashedStroke (dotted, sep, dots, 2);
            g.setColour (col::lcdOn.withAlpha (0.3f));
            g.fillPath (dotted);
        }

        constexpr float kCycles = 3.0f;
        const int n = juce::jmax (2, (int) runW * 2);
        auto trace = [&] (float phaseOffset) {
            juce::Path p;
            for (int i = 0; i <= n; ++i) {
                const float u = (float) i / (float) n;
                const float cycles = d * hz + u * kCycles + phaseOffset;   // the phase the LFO really has there
                const float v = shape (wave, cycles - std::floor (cycles), (int) std::floor (cycles));
                const float x = xRun + runW * u;
                if (i == 0) p.startNewSubPath (x, yFor (v)); else p.lineTo (x, yFor (v));
            }
            return p;
        };
        if (! sync) {                                 // free-running: a note may start anywhere
            g.setColour (col::lcdOn.withAlpha (inUse ? 0.18f : 0.08f));
            for (float off : { 0.33f, 0.67f })
                g.strokePath (trace (off), juce::PathStrokeType (1.0f));
        }
        const auto main = trace (0.0f);
        g.setColour (col::lcdOn.withAlpha (inUse ? 0.12f : 0.05f));
        g.strokePath (main, juce::PathStrokeType (3.5f));
        g.setColour (traceCol);
        g.strokePath (main, juce::PathStrokeType (1.3f));

        // Bottom line: depths and delay.
        const juce::String pm (juce::CharPointer_UTF8 ("\xc2\xb1"));
        const juce::String vib = cents < 0.5f ? juce::String ("Vib off")
                               : cents < 100.0f ? "Vib " + pm + juce::String (juce::roundToInt (cents)) + " ct"
                                                : "Vib " + pm + juce::String (cents / 100.0f, 1) + " st";
        const juce::String amp = amd == 0 ? juce::String ("Amp off")
                               : "Amp " + juce::String (juce::roundToInt (std::pow ((float) amd / 99.0f, 2.9f) * 100.0f)) + "%";
        const juce::String dly = hold + fade <= 0.001f ? juce::String ("no delay")
                               : "Delay " + juce::String (hold, hold < 10.0f ? 2 : 1) + " s";
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText (vib + "  " + amp, bottom, juce::Justification::centredLeft, false);
        g.drawText (dly, bottom, juce::Justification::centredRight, false);
    }

private:
    static constexpr const char* kWaveNames[6] = { "Triangle", "Saw Down", "Saw Up", "Square", "Sine", "S/H" };

    // The LFO's six shapes, as the native engine generates them; `cycle`
    // picks the held value for sample-and-hold (fixed, so the picture is
    // stable).
    static float shape (int wave, float p, int cycle) {
        switch (wave) {
            case 0:  return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
            case 1:  return 1.0f - 2.0f * p;
            case 2:  return 2.0f * p - 1.0f;
            case 3:  return (p < 0.5f) ? 1.0f : -1.0f;
            case 4:  return std::sin (vdx7native::kTwoPi * p);
            default: {
                uint32_t x = (uint32_t) (cycle + 1) * 2654435761u;
                x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
                return (float) (x & 0xFFFF) / 32767.5f - 1.0f;
            }
        }
    }

    static juce::String rate (float hz) {
        return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz";
    }

    void timerCallback() override {
        if (! isShowing() || ! param) return;
        const std::array<int, 7> sig { param (vdx7::G_LFW), param (vdx7::G_LFS), param (vdx7::G_LFD),
                                       param (vdx7::G_LPMD), param (vdx7::G_LAMD), param (vdx7::G_LPMS),
                                       keySync && keySync() ? 1 : 0 };
        if (sig != sig_) { sig_ = sig; repaint(); }
    }

    std::array<int, 7> sig_ {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoScope)
};

// ============================================================================
//  PitchEgScope  -  the pitch envelope on a small green glass, in semitones
//  around the note played. The DX7 pitch EG is shaped like the operator EGs
//  (four rates, four levels, holding at Level 3 while the key is down) with
//  one twist: a note *starts* at Level 4 as well as ending there, so Level 4
//  is where a pitch swoop begins. Level 50 is no change.
//
//  Runs the native engine's own vdx7native::PitchEnv, the code that bends the
//  sound, holding the key until the envelope has settled and then releasing
//  it at the dashed line. The caption gives the vertical scale.
// ============================================================================
class PitchEgScope : public juce::Component, private juce::Timer {
public:
    std::function<int(int)> param;     // VCED offset (vdx7::G_*) -> current value

    PitchEgScope() {
        setInterceptsMouseClicks (false, false);
        setTitle ("Pitch envelope");
        startTimerHz (12);
    }
    ~PitchEgScope() override { stopTimer(); }

    void paint (juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Pitch EG");
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);
        const juce::String pm (juce::CharPointer_UTF8 ("\xc2\xb1"));

        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.8f));
        g.drawText (idle_ ? juce::String ("flat") : pm + juce::String (range_, range_ < 10.0f ? 1 : 0) + " st",
                    r.reduced (2.5f).reduced (6.0f, 2.0f).removeFromTop (10.0f), juce::Justification::centredRight, false);

        auto yFor = [&] (float st) { return plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, st / range_) * plot.getHeight() * 0.46f; };
        auto xFor = [&] (float t)  { return plot.getX() + t / juce::jmax (1.0e-6f, total_) * plot.getWidth(); };

        g.setColour (col::lcdOn.withAlpha (0.14f));
        g.drawHorizontalLine ((int) std::round (plot.getCentreY()), plot.getX(), plot.getRight());

        if (idle_ || semis_.empty()) {
            g.setColour (col::lcdOn);
            g.drawHorizontalLine ((int) std::round (plot.getCentreY()), plot.getX(), plot.getRight());
            g.setColour (col::lcdOn.withAlpha (0.5f));
            g.drawText ("no pitch movement", bottom, juce::Justification::centredLeft, false);
            return;
        }

        const float xOff = xFor (keyOff_);
        {
            juce::Path line, dashed;
            line.startNewSubPath (xOff, plot.getY());
            line.lineTo (xOff, plot.getBottom());
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (col::lcdOn.withAlpha (0.35f));
            g.fillPath (dashed);
        }

        juce::Path p;
        const int w = juce::jmax (1, (int) plot.getWidth());
        for (int px = 0; px <= w; ++px) {
            const float t = total_ * (float) px / (float) w;
            const size_t k = juce::jmin (semis_.size() - 1, (size_t) (t / kTick));
            const float x = plot.getX() + (float) px, y = yFor (semis_[k]);
            if (px == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col::lcdOn.withAlpha (0.12f));
        g.strokePath (p, juce::PathStrokeType (3.5f));
        g.setColour (col::lcdOn);
        g.strokePath (p, juce::PathStrokeType (1.3f));

        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText ("0", bottom, juce::Justification::centredLeft, false);
        g.drawText (time (total_), bottom, juce::Justification::centredRight, false);
        g.setColour (col::lcdOn.withAlpha (0.5f));
        g.drawText ("KEY OFF", juce::Rectangle<float> (xOff - 22.0f, bottom.getY(), 44.0f, bottom.getHeight())
                                   .constrainedWithin (bottom.withTrimmedLeft (10.0f).withTrimmedRight (26.0f)),
                    juce::Justification::centred, false);
    }

private:
    static constexpr float kTick = 32.0f / 48000.0f;     // the engine's control block at 48 kHz
    static constexpr float kCap  = 20.0f;

    void timerCallback() override {
        if (! isShowing() || ! param) return;
        std::array<int, 8> sig {};
        for (int k = 0; k < 8; ++k) sig[(size_t) k] = param (vdx7::G_PR1 + k);
        if (sig == sig_ && built_) return;
        sig_ = sig; built_ = true;
        rebuild();
        repaint();
    }

    void rebuild() {
        uint8_t rates[4], levels[4];
        for (int k = 0; k < 4; ++k) {
            rates[k]  = (uint8_t) sig_[(size_t) k];
            levels[k] = (uint8_t) sig_[(size_t) (4 + k)];
        }
        semis_.clear();
        vdx7native::PitchEnv probe;
        probe.configure (rates, levels);
        idle_ = probe.isIdle();
        if (idle_) return;

        const int cap = (int) (kCap / kTick);
        auto settleTicks = [cap] (vdx7native::PitchEnv& e, std::vector<float>* out) {
            float prev = 1.0e9f; int still = 0, n = 0;
            for (; n < cap; ++n) {
                const float v = e.tick (kTick);
                if (out != nullptr) out->push_back (v);
                if (juce::exactlyEqual (v, prev)) { if (++still >= 3) break; } else still = 0;
                prev = v;
            }
            return n;
        };

        vdx7native::PitchEnv e;
        e.configure (rates, levels);
        e.keyOn();
        const float settle = (float) settleTicks (e, nullptr) * kTick;
        keyOff_ = juce::jlimit (0.2f, kCap, settle + juce::jmax (0.35f * settle, 0.2f));

        e.configure (rates, levels);
        e.keyOn();
        const int holdTicks = (int) std::ceil (keyOff_ / kTick);
        for (int n = 0; n < holdTicks; ++n) semis_.push_back (e.tick (kTick));
        e.keyOff();
        const float rel = (float) settleTicks (e, &semis_) * kTick;
        total_ = niceCeil (keyOff_ + rel * 1.1f + 0.02f);

        float m = 0.0f;
        for (float v : semis_) m = juce::jmax (m, std::abs (v));
        static const float scales[] = { 1.0f, 2.0f, 3.0f, 6.0f, 12.0f, 24.0f, 48.0f };
        range_ = 48.0f;
        for (float sc : scales) if (sc >= m * 1.05f) { range_ = sc; break; }
    }

    static float niceCeil (float secs) {
        static const float steps[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f,
                                       6.0f, 8.0f, 10.0f, 15.0f, 20.0f, 30.0f, 40.0f };
        for (float s : steps) if (s >= secs) return s;
        return 60.0f;
    }

    static juce::String time (float s) {
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms"
                        : juce::String (s, juce::exactlyEqual (s, std::round (s)) ? 0 : 2) + " s";
    }

    std::array<int, 8> sig_ {};
    bool  built_ = false, idle_ = true;
    std::vector<float> semis_;
    float keyOff_ = 0.5f, total_ = 1.0f, range_ = 1.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchEgScope)
};

// ============================================================================
//  GlobalPanel  -  LFO and pitch EG, each with a small scope, plus Transpose.
//
//    [ LFO scope  ] [Wave ] [Speed] [Delay]  |  [Rate 1 ] ... [Rate 4 ] [ Pitch EG scope ]
//    [LFO Key Sync] [P.Dep] [P.Sns] [A.Dep]  |  [Level 1] ... [Level 4] [ Transpose (st) ]
//
//  LFO Key Sync is a button rather than a two-position knob, and Transpose
//  is a small knob showing the shift in semitones (-24..+24, 0 = none).
//  Algorithm, Feedback and oscillator Key Sync live on the GLOBAL | ALGORITHM
//  card.
// ============================================================================
class GlobalPanel : public juce::Component {
    CompactLookAndFeel lnf_;   // declared first: destroyed after the controls using it

public:
    GlobalPanel (SliderFactory make, const SliderBinder& bindSlider, const ButtonBinder& bindButton) {
        using namespace vdx7;
        auto knob = [&] (std::vector<ParamSlider*>& grp, const juce::String& cap, const juce::String& full,
                         int mn, int mx, int off) {
            auto* k = make (cap, full, mn, mx, off);
            owned.emplace_back (k);
            addAndMakeVisible (k);
            grp.push_back (k);
            byOffset[(size_t) off] = k;
        };
        // LFO, 3 x 2
        knob (lfoKnobs, "Wave",        "LFO Waveform",               0, 5,  G_LFW);
        knob (lfoKnobs, "Speed",       "LFO Speed",                  0, 99, G_LFS);
        knob (lfoKnobs, "Delay",       "LFO Delay",                  0, 99, G_LFD);
        knob (lfoKnobs, "Pitch Depth", "LFO Pitch Mod Depth",        0, 99, G_LPMD);
        knob (lfoKnobs, "Pitch Sens",  "LFO Pitch Mod Sensitivity",  0, 7,  G_LPMS);
        knob (lfoKnobs, "Amp Depth",   "LFO Amplitude Mod Depth",    0, 99, G_LAMD);
        // Pitch EG, 4 x 2 (rates over levels, like the operator envelopes)
        knob (pegKnobs, "Rate 1",  "Pitch EG Rate 1",                                 0, 99, G_PR1);
        knob (pegKnobs, "Rate 2",  "Pitch EG Rate 2",                                 0, 99, G_PR2);
        knob (pegKnobs, "Rate 3",  "Pitch EG Rate 3",                                 0, 99, G_PR3);
        knob (pegKnobs, "Rate 4",  "Pitch EG Rate 4 (release)",                       0, 99, G_PR4);
        knob (pegKnobs, "Level 1", "Pitch EG Level 1 (50 = no change)",               0, 99, G_PL1);
        knob (pegKnobs, "Level 2", "Pitch EG Level 2 (50 = no change)",               0, 99, G_PL2);
        knob (pegKnobs, "Level 3", "Pitch EG Level 3, while the key is down",    0, 99, G_PL3);
        knob (pegKnobs, "Level 4", "Pitch EG Level 4, where a note starts and ends",  0, 99, G_PL4);

        setupToggleButton (lfoSync, "LFO Key Sync", "LFO Key Sync", lnf_);
        lfoSync.setTooltip ("LFO Key Sync: when on, every key restarts the LFO wave from the top of its cycle");
        bindButton (lfoSync, G_LFKS);
        addAndMakeVisible (lfoSync);

        setupCompactKnob (transpose, "Transpose in semitones (-24 to +24, 0 = none)", 0, 48, lnf_);
        bindSlider (transpose, G_TRANSPOSE);
        // After binding: the parameter attachment installs its own number text.
        // The patch stores 0..48 with 24 = no shift; the knob shows the shift.
        transpose.textFromValueFunction = [] (double v) {
            const int st = juce::roundToInt (v) - 24;
            return (st > 0 ? "+" : "") + juce::String (st);
        };
        transpose.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() + 24.0; };
        transpose.getProperties().set (CompactLookAndFeel::litAtZeroProperty(), true);
        addAndMakeVisible (transpose);

        lfoScope.param   = [this] (int off) { return valueAt (off); };
        lfoScope.keySync = [this] { return lfoSync.getToggleState(); };
        pegScope.param   = [this] (int off) { return valueAt (off); };
        addAndMakeVisible (lfoScope);
        addAndMakeVisible (pegScope);
    }

    ~GlobalPanel() override {
        lfoSync.setLookAndFeel (nullptr);
        transpose.setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto hdr = b.removeFromTop (22);
        g.setColour (col::accentDim);
        g.fillRoundedRectangle (hdr.toFloat().reduced (3, 3), 4.0f);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText ("LFO  |  PITCH EG", hdr, juce::Justification::centred);

        g.setColour (col::edge.withAlpha (0.6f));
        g.drawLine ((float) dividerX_, (float) rowsTop_, (float) dividerX_, (float) rowsBottom_, 1.0f);

        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText ("Transpose (semitones)", transposeLabel_, juce::Justification::centredLeft, 1, 0.8f);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromTop (24);
        b = b.reduced (4);
        rowsTop_ = b.getY();
        rowsBottom_ = b.getBottom();

        const int cw = 64, ch = b.getHeight() / 2;         // knob tiles: same knob size as elsewhere
        const int belowH = 28, transposeH = 34, scopeGap = 4, sideGap = 8, divGap = 10;
        const int scopeW = (b.getWidth() - 7 * cw - 2 * sideGap - 2 * divGap) / 2;

        auto grid = [&] (std::vector<ParamSlider*>& items, int x, int cols) {
            for (int i = 0; i < (int) items.size(); ++i)
                items[(size_t) i]->setBounds (x + (i % cols) * cw, b.getY() + (i / cols) * ch, cw, ch);
        };

        // LFO: scope over its Key Sync switch, then the 3 x 2 knobs.
        auto lfoCol = b.removeFromLeft (scopeW);
        lfoSync.setBounds (lfoCol.removeFromBottom (belowH));
        lfoCol.removeFromBottom (scopeGap);
        lfoScope.setBounds (lfoCol);
        b.removeFromLeft (sideGap);
        grid (lfoKnobs, b.getX(), 3);
        b.removeFromLeft (3 * cw);

        b.removeFromLeft (divGap);
        dividerX_ = b.getX();
        b.removeFromLeft (divGap);

        // Pitch EG: the 4 x 2 knobs, then its scope over Transpose.
        grid (pegKnobs, b.getX(), 4);
        b.removeFromLeft (4 * cw);
        b.removeFromLeft (sideGap);
        auto pegCol = b;
        auto row = pegCol.removeFromBottom (transposeH);
        pegCol.removeFromBottom (scopeGap - 2);
        pegScope.setBounds (pegCol);
        row.removeFromRight (8);                          // a little air before the card's edge
        transpose.setBounds (row.removeFromRight (transposeH));
        row.removeFromRight (2);
        transposeLabel_ = row.withTrimmedLeft (16);       // drawn in from the scope's edge
    }

private:
    int valueAt (int off) const {
        if (off < 0 || off >= (int) byOffset.size()) return 0;
        auto* k = byOffset[(size_t) off];
        return k != nullptr ? k->value() : 0;
    }

    std::vector<std::unique_ptr<ParamSlider>> owned;
    std::vector<ParamSlider*> lfoKnobs, pegKnobs;
    std::array<ParamSlider*, vdx7::kVcedSize> byOffset {};
    juce::TextButton lfoSync;
    juce::Slider     transpose;
    LfoScope         lfoScope;
    PitchEgScope     pegScope;
    int dividerX_ = 0, rowsTop_ = 0, rowsBottom_ = 0;
    juce::Rectangle<int> transposeLabel_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalPanel)
};

// ============================================================================
//  FxKnob  -  ParamSlider's continuous sibling.
//
//  ParamSlider is integer-only: it steps by 1 and prints (int) getValue(),
//  which is right for VCED bytes but wrong for 0.5 Hz or 137.4 ms. This one
//  takes its range straight from the APVTS parameter, so a log-skewed
//  frequency knob moves correctly without the UI knowing which units it is
//  showing. Same tile geometry and same painted caption/value bands as
//  ParamSlider, so the two look identical side by side.
// ============================================================================
class FxKnob : public juce::Component {
public:
    FxKnob (const juce::String& caption, juce::RangedAudioParameter& p)
        : param (p), caption_ (caption)
    {
        const auto range = p.getNormalisableRange();
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setNormalisableRange ({ (double) range.start, (double) range.end,
                                       (double) range.interval, (double) range.skew,
                                       range.symmetricSkew });
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setDoubleClickReturnValue (true, (double) range.convertFrom0to1 (p.getDefaultValue()));
        slider.setWantsKeyboardFocus (true);

        slider.setTitle (p.getName (64));
        slider.setName  (p.getName (64));
        slider.setTooltip (p.getName (64));

        slider.onValueChange = [this] { repaint(); };   // refresh the printed value
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::SliderParameterAttachment> (p, slider);
        setInterceptsMouseClicks (false, true);
    }

    // Optional: the value the effect really uses, when something else caps
    // it (the delay's Feedback without Self-Feedback). The knob still turns
    // through its whole range; the printed value is the one that is heard.
    std::function<float (float)> shownValue;

    void resized() override { slider.setBounds (getLocalBounds()); }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (14);
        auto bot = b.removeFromBottom (14);
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (caption_, top, juce::Justification::centred);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (valueText(), bot, juce::Justification::centred);
    }

    // Rounded for reading, not for storing: the parameter keeps its full
    // precision. Two decimals below 10, one below 100, none above - never
    // finer than the knob's own step (a 0.1 ms knob reads "7.0 ms"); percent
    // as whole percents; kHz and seconds once Hz and ms reach four digits.
    static juce::String formatted (float v, const juce::String& unit, float step = 0.0f) {
        if (unit == "%")                  return juce::String (juce::roundToInt (v)) + " %";
        if (unit == "Hz" && v >= 999.5f)  return formatted (v * 0.001f, "kHz", step * 0.001f);
        if (unit == "ms" && v >= 999.5f)  return formatted (v * 0.001f, "s",   step * 0.001f);
        const float a = std::abs (v);
        int decimals = a >= 99.95f ? 0 : a >= 9.995f ? 1 : 2;   // judged after rounding: 99.99 reads "100"
        if (step > 0.0f)
            decimals = juce::jmin (decimals, juce::jmax (0, (int) std::ceil (-std::log10 (step) - 1.0e-4f)));
        const auto num = decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        return unit.isEmpty() ? num : num + " " + unit;
    }

private:
    juce::String valueText() const {
        const auto  unit = param.getLabel();
        const float raw  = param.convertFrom0to1 (param.getValue());
        const float v    = shownValue ? shownValue (raw) : raw;
        if (dynamic_cast<const juce::AudioParameterFloat*> (&param) != nullptr)
            return formatted (v, unit, param.getNormalisableRange().interval);

        // Chord intervals: signed semitones, and 0 reads "off", which is what
        // a 0 does in that section.
        if (unit == "st") {
            const int st = juce::roundToInt (v);
            return st == 0 ? juce::String ("off")
                           : (st > 0 ? juce::String ("+") : juce::String()) + juce::String (st) + " st";
        }
        const auto txt = param.getCurrentValueAsText();   // whole numbers and choices
        return unit.isEmpty() ? txt : txt + " " + unit;
    }

    juce::RangedAudioParameter& param;
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::String caption_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxKnob)
};

// ============================================================================
//  FX scopes  -  the green glass under each effect's knobs.
//
//  Each one watches its unit's parameters (read from the APVTS atomics on a
//  slow timer, repainting only when something moved) and dims while its
//  unit is switched off, like the LFO scope does when the LFO touches
//  nothing. Chorus and Phaser are drawn from the effect's formulas; Delay and
//  Reverb actually run a private copy of the effect (vdx7fx::Delay /
//  vdx7fx::Reverb, the same classes the audio thread uses) on a single click
//  and draw what comes out, on a background thread so a turning knob never
//  waits for it.
// ============================================================================
inline juce::String fxNumber (float v, int decimals) {
    return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
}
inline juce::String fxHz (float hz) {
    if (hz >= 1000.0f) return fxNumber (hz * 0.001f, hz < 10000.0f ? 2 : 1) + " kHz";
    return fxNumber (hz, hz < 10.0f ? 2 : hz < 100.0f ? 1 : 0) + " Hz";
}
inline juce::String fxSeconds (float s) {
    if (s < 1.0f) return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
    return fxNumber (s, s < 10.0f ? 2 : 1) + " s";
}
// Text in a glass's top-right corner, opposite its caption.
inline void drawGlassCorner (juce::Graphics& g, juce::Rectangle<float> glass, const juce::String& text) {
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.setColour (col::lcdOn.withAlpha (0.8f));
    g.drawText (text, glass.reduced (2.5f).reduced (6.0f, 2.0f).removeFromTop (10.0f),
                juce::Justification::centredRight, false);
}

class FxScope : public juce::Component, private juce::Timer {
public:
    FxScope (juce::AudioProcessorValueTreeState& apvts, const char* enableId,
             std::initializer_list<const char*> paramIds)
        : processor_ (apvts.processor), enable_ (apvts.getRawParameterValue (enableId))
    {
        for (auto* id : paramIds)
            params_.push_back (apvts.getRawParameterValue (id));
        values_.assign (params_.size(), 0.0f);
        readValues();                     // real values from the start: paint may come before the first tick
        setInterceptsMouseClicks (false, false);
        startTimerHz (15);
    }
    ~FxScope() override { stopTimer(); }

protected:
    float value (size_t i) const { return i < values_.size() ? values_[i] : 0.0f; }
    bool  unitOn() const { return on_; }
    float lit (float alpha) const { return on_ ? alpha : alpha * 0.4f; }   // dim while bypassed
    double hostRate() const { return rate_; }       // the rate the effects really run at

    virtual void valuesChanged() {}               // message thread, just before the repaint
    virtual bool collectResults() { return false; }   // true: something new to draw

private:
    // Copies the parameters in; true if any of them moved. A value that
    // isn't a number (never expected) is taken as 0, so nothing downstream
    // ever draws at a NaN coordinate.
    bool readValues() {
        bool changed = false;
        const double sr = processor_.getSampleRate();
        const double rate = sr > 0.0 ? sr : 48000.0;   // before the host has prepared us
        if (! juce::exactlyEqual (rate, rate_)) { rate_ = rate; changed = true; }
        const bool on = enable_ != nullptr && enable_->load (std::memory_order_relaxed) >= 0.5f;
        if (on != on_) { on_ = on; changed = true; }
        for (size_t i = 0; i < params_.size(); ++i) {
            float v = params_[i] != nullptr ? params_[i]->load (std::memory_order_relaxed) : 0.0f;
            if (! std::isfinite (v)) v = 0.0f;
            if (! juce::exactlyEqual (v, values_[i])) { values_[i] = v; changed = true; }
        }
        return changed;
    }

    void timerCallback() override {
        if (! isShowing()) return;
        const bool changed = readValues() || first_;
        first_ = false;
        if (changed) valuesChanged();
        if (collectResults() || changed) repaint();
    }

    juce::AudioProcessor& processor_;
    std::atomic<float>* enable_ = nullptr;
    std::vector<std::atomic<float>*> params_;
    std::vector<float> values_;
    double rate_ = 0.0;
    bool on_ = false, first_ = true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxScope)
};

// A small background thread for the scopes that render audio to draw
// themselves. Only the newest request matters: asking again replaces the
// queued job, and a running job polls `stale()` so it can give up as soon as
// a newer one is waiting.
class ScopeWorker : private juce::Thread {
public:
    using Job = std::function<void (const std::function<bool()>& stale)>;

    explicit ScopeWorker (const juce::String& name) : juce::Thread (name) {
        startThread (juce::Thread::Priority::low);
    }
    ~ScopeWorker() override {
        signalThreadShouldExit();
        notify();
        stopThread (4000);
    }

    void request (Job job) {
        {
            const juce::ScopedLock sl (lock_);
            pending_ = std::move (job);
            ++generation_;
        }
        notify();
    }

private:
    void run() override {
        while (! threadShouldExit()) {
            Job job;
            int gen = 0;
            {
                const juce::ScopedLock sl (lock_);
                std::swap (job, pending_);
                gen = generation_.load();
            }
            if (! job) { wait (-1); continue; }
            job ([this, gen] { return threadShouldExit() || generation_.load() != gen; });
        }
    }

    juce::CriticalSection lock_;
    Job pending_;
    std::atomic<int> generation_ { 0 };
};

// ---------------------------------------------------------------------------
//  Chorus: the delay time each channel's LFO sweeps, over two LFO cycles, on
//  a log scale of 0.5..40 ms so Delay moves the band, Depth widens it and
//  Spread slides the right channel (the dimmer trace) out of phase. With
//  Unison, each voice gets its own trace, spread evenly round the cycle. The
//  bottom line gives the swept range and the detune it causes, which is what
//  the ear hears: a delay that changes over time bends the pitch.
// ---------------------------------------------------------------------------
class ChorusScope : public FxScope {
public:
    explicit ChorusScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, vdx7fx::ids::chorusOn, { vdx7fx::ids::chorusRate, vdx7fx::ids::chorusDepth,
                                                vdx7fx::ids::chorusDelay, vdx7fx::ids::chorusSpread,
                                                vdx7fx::ids::chorusVoices })
    {
        setTitle ("Chorus delay sweep");
    }

    void paint (juce::Graphics& g) override {
        const float rate   = juce::jmax (0.001f, value (0));
        const float depth  = value (1) * 0.01f;
        const float baseMs = value (2);
        const float spread = value (3) * 0.01f;
        const int   voices = juce::jlimit (1, vdx7fx::Chorus::maxVoices, juce::roundToInt (value (4)));

        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Chorus  L / R");
        drawGlassCorner (g, r, (voices > 1 ? juce::String (voices) + " voices  " : juce::String()) + fxHz (rate));
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);

        constexpr float lo = 0.5f, hi = 40.0f;
        auto yFor = [&] (float ms) {
            const float u = std::log (juce::jlimit (lo, hi, ms) / lo) / std::log (hi / lo);
            return plot.getBottom() - u * plot.getHeight();
        };

        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        for (int ms : { 1, 2, 5, 10, 20 }) {
            const float y = yFor ((float) ms);
            g.setColour (col::lcdOn.withAlpha (0.07f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (col::lcdOn.withAlpha (0.32f));
            g.drawText (juce::String (ms) + (ms == 20 ? " ms" : ""),
                        juce::Rectangle<float> (plot.getX(), y - 9.0f, 30.0f, 9.0f),
                        juce::Justification::centredLeft, false);
        }

        {   // the base delay, which the sweep is centred on
            juce::Path line, dashed;
            line.startNewSubPath (plot.getX(), yFor (baseMs));
            line.lineTo (plot.getRight(), yFor (baseMs));
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (col::lcdOn.withAlpha (lit (0.25f)));
            g.fillPath (dashed);
        }

        auto delayAt = [&] (double cycles) {
            const float lfo = (float) std::sin (cycles * juce::MathConstants<double>::twoPi);
            return juce::jmax (0.5f, baseMs * (1.0f + 0.9f * depth * lfo));   // FxChain's Chorus
        };
        constexpr float kCycles = 2.0f;
        const int n = juce::jmax (2, (int) plot.getWidth() * 2);
        auto trace = [&] (double phase) {
            juce::Path p;
            for (int i = 0; i <= n; ++i) {
                const float u = (float) i / (float) n;
                const float x = plot.getX() + u * plot.getWidth(), y = yFor (delayAt (u * kCycles + phase));
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };
        // Right channel first (dimmer, up to 180 degrees behind), then the left
        // on top; each unison voice a fraction of a cycle further on.
        for (int v = voices - 1; v >= 0; --v) {
            const double at = (double) v / (double) voices;
            g.setColour (col::lcdOn.withAlpha (lit (v == 0 ? 0.45f : 0.3f)));
            g.strokePath (trace (at + spread * 0.5), juce::PathStrokeType (1.1f));
        }
        for (int v = voices - 1; v >= 0; --v) {
            const auto left = trace ((double) v / (double) voices);
            g.setColour (col::lcdOn.withAlpha (lit (0.12f)));
            g.strokePath (left, juce::PathStrokeType (3.5f));
            g.setColour (col::lcdOn.withAlpha (lit (v == 0 ? 1.0f : 0.7f)));
            g.strokePath (left, juce::PathStrokeType (1.3f));
        }

        // Swept range, and the pitch wobble: the fastest the delay changes is
        // base * 0.9 * depth * 2 pi * rate (seconds per second), and a delay
        // shrinking at s seconds per second plays back 1 + s times faster.
        const float minMs  = juce::jmax (0.5f, baseMs * (1.0f - 0.9f * depth));
        const float maxMs  = baseMs * (1.0f + 0.9f * depth);
        const float slope  = baseMs * 0.001f * 0.9f * depth * juce::MathConstants<float>::twoPi * rate;
        const float cents  = 1200.0f * std::log2 (1.0f + slope);
        const juce::String pm (juce::CharPointer_UTF8 ("\xc2\xb1"));
        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText (depth <= 0.0f ? fxNumber (baseMs, 1) + " ms"
                                  : fxNumber (minMs, 1) + dash + fxNumber (maxMs, 1) + " ms",
                    bottom, juce::Justification::centredLeft, false);
        g.drawText (cents < 0.5f ? juce::String ("no detune")
                                 : cents < 100.0f ? "detune " + pm + juce::String (juce::roundToInt (cents)) + " ct"
                                                  : "detune " + pm + fxNumber (cents / 100.0f, 1) + " st",
                    bottom, juce::Justification::centredRight, false);
    }
};

// ---------------------------------------------------------------------------
//  Delay: the echoes of a single click, left channel above the line and
//  right below, on a 48 dB scale. Rendered by a private vdx7fx::Delay at the
//  host's sample rate, so feedback, the high-pass in the loop (each repeat a
//  little thinner) and a runaway look exactly like they sound. The first bar
//  is the dry click, so Mix is the balance between it and the echoes. The
//  corner counts the repeats until the echoes are 60 dB down, worked out
//  from the loop's gain at its least damped frequency - "runaway" when that
//  gain is 1 or more. That takes Allow Self-Feedback (without it Feedback
//  stops at 1.00, where the high-pass keeps the loop just below 1), and then
//  happens a little above a Feedback of 1 rather than exactly at it.
// ---------------------------------------------------------------------------
class DelayScope : public FxScope {
public:
    explicit DelayScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, vdx7fx::ids::delayOn, { vdx7fx::ids::delayTimeL, vdx7fx::ids::delayTimeR,
                                               vdx7fx::ids::delayFb, vdx7fx::ids::delayHp,
                                               vdx7fx::ids::delayMix, vdx7fx::ids::delaySelfFb })
    {
        setTitle ("Delay echoes");
    }

    void paint (juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Delay  echoes");
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);
        const float cy = std::round (plot.getCentreY()) + 0.5f, half = plot.getHeight() * 0.5f - 1.0f;

        g.setColour (col::lcdOn.withAlpha (0.14f));
        g.drawHorizontalLine ((int) cy, plot.getX(), plot.getRight());
        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        g.setColour (col::lcdOn.withAlpha (0.32f));
        g.drawText ("L", plot.withHeight (9.0f), juce::Justification::centredRight, false);
        g.drawText ("R", plot.withTrimmedTop (plot.getHeight() - 9.0f), juce::Justification::centredRight, false);

        if (shown_.window <= 0.0f)
            return;

        drawGlassCorner (g, r, shown_.echoes < 0   ? juce::String ("runaway")
                             : shown_.echoes == 1  ? juce::String ("1 echo")
                             : shown_.echoes > 999 ? juce::String ("999+ echoes")
                                                   : juce::String (shown_.echoes) + " echoes");

        const float mix = value (4) * 0.01f, dry = 1.0f - mix;
        float peak = dry;
        for (auto& e : shown_.left)  peak = juce::jmax (peak, e.level * mix);
        for (auto& e : shown_.right) peak = juce::jmax (peak, e.level * mix);
        if (peak <= 0.0f)
            return;

        constexpr float kRangeDb = 48.0f;
        auto heightFor = [&] (float amp) {
            if (amp <= 0.0f) return 0.0f;
            return juce::jlimit (0.0f, 1.0f, 1.0f + 20.0f * std::log10 (amp / peak) / kRangeDb) * half;
        };
        auto bar = [&] (float x, float h, bool up, float alpha) {
            if (h < 0.5f) return;
            const float y = up ? cy - h : cy;
            g.setColour (col::lcdOn.withAlpha (lit (alpha * 0.15f)));
            g.fillRect (x - 1.5f, y, 3.0f, h);
            g.setColour (col::lcdOn.withAlpha (lit (alpha)));
            g.fillRect (x - 0.6f, y, 1.2f, h);
        };

        if (dry > 0.0f) {                              // the click itself, dimmer than its echoes
            const float h = heightFor (dry);
            g.setColour (col::lcdOn.withAlpha (lit (0.4f)));
            g.fillRect (juce::Rectangle<float> (plot.getX() + 0.5f, cy - h, 1.5f, 2.0f * h));
            g.setColour (col::lcdOn.withAlpha (0.32f));
            g.setFont (juce::Font (juce::FontOptions (8.0f)));
            g.drawText ("dry", juce::Rectangle<float> (plot.getX() + 4.0f, cy - h, 24.0f, 9.0f),
                        juce::Justification::centredLeft, false);
        }

        // One bar per echo, at the time it lands; where a short delay puts
        // several echoes into one pixel, the loudest of them.
        const int w = juce::jmax (1, (int) plot.getWidth());
        std::vector<float> up ((size_t) w, 0.0f), down ((size_t) w, 0.0f);
        auto place = [&] (const std::vector<Echo>& echoes, std::vector<float>& cols) {
            for (auto& e : echoes) {
                const int px = juce::roundToInt (e.time / shown_.window * (float) w);
                if (px >= 1 && px < w) cols[(size_t) px] = juce::jmax (cols[(size_t) px], e.level);
            }
        };
        place (shown_.left, up);
        place (shown_.right, down);
        for (int px = 1; px < w; ++px) {
            const float x = plot.getX() + (float) px + 0.5f;
            bar (x, heightFor (up[(size_t) px] * mix),   true,  1.0f);
            bar (x, heightFor (down[(size_t) px] * mix), false, 0.75f);
        }

        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText ("0", bottom, juce::Justification::centredLeft, false);
        g.drawText (fxSeconds (shown_.window), bottom, juce::Justification::centredRight, false);
    }

protected:
    void valuesChanged() override {
        // The feedback the delay really gets: without Self-Feedback it stops at 1.00.
        const float fb = value (5) >= 0.5f ? value (2) : juce::jmin (1.0f, value (2));
        const Settings s { value (0), value (1), fb, value (3), hostRate() };
        if (s == requested_) return;                   // Mix only changes the drawing
        requested_ = s;
        worker_.request ([this, s] (const std::function<bool()>& stale) {
            Echoes e;
            if (! render (s, e, stale)) return;
            const juce::SpinLock::ScopedLockType sl (lock_);
            ready_ = std::move (e);
            fresh_ = true;
        });
    }

    bool collectResults() override {
        if (! fresh_.exchange (false)) return false;
        const juce::SpinLock::ScopedLockType sl (lock_);
        shown_ = std::move (ready_);
        return true;
    }

private:
    struct Settings {
        float timeL = -1, timeR = -1, feedback = 0, highPass = 0;
        double rate = 0;
        bool operator== (const Settings& o) const {
            return juce::exactlyEqual (timeL, o.timeL) && juce::exactlyEqual (timeR, o.timeR)
                && juce::exactlyEqual (feedback, o.feedback) && juce::exactlyEqual (highPass, o.highPass)
                && juce::exactlyEqual (rate, o.rate);
        }
    };
    struct Echo { float time, level; };   // seconds after the click, level as a click amplitude
    struct Echoes {
        float window = 0.0f;
        int   echoes = 0;                  // repeats until 60 dB down; -1 = runaway
        std::vector<Echo> left, right;
    };

    // The feedback loop's gain at its least damped frequency: Feedback, times
    // the one-pole high-pass, times the linear interpolation that reads the
    // line between two samples (which dulls the top end unless the delay
    // time lands on a whole sample). At or above 1 the echoes never die away
    // - some band grows on every pass until the line's clamp holds it.
    static float loopGain (float feedback, float highPass, float timeMs, double sr) {
        const double d    = timeMs * 0.001 * sr;
        const double frac = d - std::floor (d);
        const double c    = std::exp (-juce::MathConstants<double>::twoPi * highPass / sr);
        double best = 0.0;
        constexpr int kSteps = 512;
        for (int i = 1; i <= kSteps; ++i) {
            const auto zi = std::polar (1.0, -juce::MathConstants<double>::pi * i / kSteps);   // z^-1
            const double hp  = std::abs (c * (1.0 - zi) / (1.0 - c * zi));
            const double lin = std::abs ((1.0 - frac) + frac * zi);
            best = juce::jmax (best, hp * lin);
        }
        return (float) (std::abs (feedback) * best);
    }

    static bool render (const Settings& s, Echoes& out, const std::function<bool()>& stale) {
        const double sr = s.rate;
        constexpr int kBlock = 512;
        constexpr float kClamp = 4.0f;              // FxChain's Delay clamps its line to +-4

        // How many repeats it takes to fall 60 dB, from the loop gain of
        // the slower-dying side.
        const float loop = juce::jmax (loopGain (s.feedback, s.highPass, s.timeL, sr),
                                       loopGain (s.feedback, s.highPass, s.timeR, sr));
        if (loop >= 1.0f)        out.echoes = -1;
        else if (loop < 1.0e-4f) out.echoes = 1;
        else out.echoes = 1 + (int) juce::jmin (1.0e6f, std::floor (std::log (0.001f) / std::log (loop)));

        // Look at ten repeats of the longer side at most (fewer if it dies
        // sooner), and never more than eight seconds.
        const float longest = juce::jmax (s.timeL, s.timeR) * 0.001f;
        const int repeats = out.echoes < 0 ? 10 : juce::jlimit (1, 10, out.echoes);
        out.window = juce::jlimit (0.3f, 8.0f, longest * ((float) repeats + 0.35f));

        vdx7fx::Delay fx;
        fx.setEnabled (true);
        fx.setTimeL (s.timeL);
        fx.setTimeR (s.timeR);
        fx.setFeedback (s.feedback);
        fx.setHighPass (s.highPass);
        fx.setDryWet (100.0f);
        fx.prepare (sr, kBlock);

        // A second of silence first: the delay time glides to its setting and
        // the mix ramps fully wet, as they would have long before anyone listens.
        std::array<float, kBlock> l {}, rr {};
        for (int done = 0; done < (int) sr; done += kBlock) {
            l.fill (0.0f); rr.fill (0.0f);
            fx.process (l.data(), rr.data(), kBlock);
        }

        // Each echo's level: the energy within a millisecond of where it
        // lands (so a click the interpolation has smeared over two samples
        // still counts in full), as a click amplitude, and never more than the
        // clamp - which is what a runaway settles into.
        const double dL = s.timeL * 0.001 * sr, dR = s.timeR * 0.001 * sr;
        const int total = juce::jmax (kBlock, (int) (out.window * sr));
        const double reach = juce::jmax (1.0, 0.001 * sr);
        const int nL = (int) std::floor ((total - 1 - reach) / dL);   // echoes rendered in full
        const int nR = (int) std::floor ((total - 1 - reach) / dR);
        std::vector<double> eL ((size_t) juce::jmax (0, nL) + 2, 0.0), eR ((size_t) juce::jmax (0, nR) + 2, 0.0);
        auto collect = [reach] (std::vector<double>& e, double d, int i, float x) {
            const double k = std::round (i / d);
            if (k >= 1.0 && k < (double) e.size() && std::abs (i - k * d) <= reach)
                e[(size_t) k] += (double) x * x;
        };
        for (int pos = 0; pos < total; pos += kBlock) {
            if (stale()) return false;
            l.fill (0.0f); rr.fill (0.0f);
            if (pos == 0) l[0] = rr[0] = 1.0f;
            const int n = juce::jmin (kBlock, total - pos);
            fx.process (l.data(), rr.data(), n);
            for (int i = 0; i < n; ++i) {
                collect (eL, dL, pos + i, l[(size_t) i]);
                collect (eR, dR, pos + i, rr[(size_t) i]);
            }
        }
        auto toEchoes = [&] (const std::vector<double>& e, double d, int count, std::vector<Echo>& dst) {
            dst.clear();
            for (int k = 1; k <= count && k < (int) e.size(); ++k)
                dst.push_back ({ (float) (k * d / sr), juce::jmin (kClamp, (float) std::sqrt (e[(size_t) k])) });
        };
        toEchoes (eL, dL, nL, out.left);
        toEchoes (eR, dR, nR, out.right);
        return true;
    }

    Settings requested_;
    Echoes shown_, ready_;
    juce::SpinLock lock_;
    std::atomic<bool> fresh_ { false };
    ScopeWorker worker_ { "Delay scope" };   // last: stopped before the rest goes
};

// ---------------------------------------------------------------------------
//  Phaser: the frequency response of the whole unit (dry and wet at the
//  current Mix), 20 Hz..20 kHz. The bright curve is the response with the
//  sweep at Center, the two faint ones the ends of the sweep, and the shaded
//  band the range the notches travel. Worked out from FxChain's Phaser: N
//  first-order allpasses with a one-sample feedback loop around them, the
//  wet being (dry + chain) / 2. Stages sets the number of notches, N / 2.
// ---------------------------------------------------------------------------
class PhaserScope : public FxScope {
public:
    explicit PhaserScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, vdx7fx::ids::phaserOn, { vdx7fx::ids::phaserRate, vdx7fx::ids::phaserDepth,
                                                vdx7fx::ids::phaserCentre, vdx7fx::ids::phaserFb,
                                                vdx7fx::ids::phaserMix, vdx7fx::ids::phaserStages })
    {
        setTitle ("Phaser frequency response");
    }

    void paint (juce::Graphics& g) override {
        static constexpr int kStages[] = { 2, 4, 6, 8, 10, 12 };
        const float rate   = value (0);
        const float depth  = value (1) * 0.01f;
        const float centre = value (2);
        const float fb     = value (3) * 0.01f * 0.9f;
        const float mix    = value (4) * 0.01f;
        const int   stages = kStages[juce::jlimit (0, 5, juce::roundToInt (value (5)))];

        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Phaser  response");
        drawGlassCorner (g, r, juce::String (stages) + " stages  " + fxHz (rate));
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);

        const float sr = (float) hostRate();
        const float fLo = 20.0f, fHi = juce::jmin (20000.0f, sr * 0.49f);
        constexpr float dbTop = 12.0f, dbBottom = -24.0f;
        auto xFor = [&] (float f) {
            return plot.getX() + std::log (f / fLo) / std::log (fHi / fLo) * plot.getWidth();
        };
        auto yFor = [&] (float db) {
            return plot.getY() + (dbTop - juce::jlimit (dbBottom, dbTop, db)) / (dbTop - dbBottom) * plot.getHeight();
        };

        const float fMin = juce::jlimit (30.0f, sr * 0.45f, centre * std::pow (2.0f, -2.0f * depth));
        const float fMax = juce::jlimit (30.0f, sr * 0.45f, centre * std::pow (2.0f,  2.0f * depth));

        g.setColour (col::lcdOn.withAlpha (lit (0.08f)));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (fMin), plot.getY(),
                                                                juce::jmax (xFor (fMax), xFor (fMin) + 1.0f),
                                                                plot.getBottom()));
        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        for (float f : { 100.0f, 1000.0f, 10000.0f }) {
            const float x = xFor (f);
            g.setColour (col::lcdOn.withAlpha (0.07f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (col::lcdOn.withAlpha (0.32f));
            g.drawText (f < 1000.0f ? "100" : f < 10000.0f ? "1k" : "10k",
                        juce::Rectangle<float> (x + 2.0f, plot.getBottom() - 9.0f, 24.0f, 9.0f),
                        juce::Justification::centredLeft, false);
        }
        g.setColour (col::lcdOn.withAlpha (0.14f));
        g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), plot.getX(), plot.getRight());
        g.setColour (col::lcdOn.withAlpha (0.32f));
        g.drawText ("0 dB", juce::Rectangle<float> (plot.getRight() - 30.0f, yFor (0.0f) - 9.0f, 30.0f, 9.0f),
                    juce::Justification::centredRight, false);

        const int w = juce::jmax (1, (int) plot.getWidth());
        auto response = [&] (float fc) {
            const float wt = std::tan (juce::MathConstants<float>::pi * fc / sr);
            const float a  = (wt - 1.0f) / (wt + 1.0f);
            juce::Path p;
            for (int px = 0; px <= w; ++px) {
                const float f = fLo * std::pow (fHi / fLo, (float) px / (float) w);
                const auto  zi = std::polar (1.0f, -juce::MathConstants<float>::twoPi * f / sr);   // z^-1
                const auto  ap = (a + zi) / (1.0f + a * zi);
                std::complex<float> chain (1.0f, 0.0f);
                for (int k = 0; k < stages; ++k) chain *= ap;
                const auto wet = 0.5f * (1.0f + chain / (1.0f - fb * zi * chain));
                const float mag = std::abs ((1.0f - mix) + mix * wet);
                const float y = yFor (20.0f * std::log10 (juce::jmax (1.0e-6f, mag)));
                const float x = plot.getX() + (float) px;
                if (px == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };

        if (depth > 0.0f) {
            g.setColour (col::lcdOn.withAlpha (lit (0.3f)));
            g.strokePath (response (fMin), juce::PathStrokeType (1.0f));
            g.strokePath (response (fMax), juce::PathStrokeType (1.0f));
        }
        const auto main = response (juce::jlimit (30.0f, sr * 0.45f, centre));
        g.setColour (col::lcdOn.withAlpha (lit (0.12f)));
        g.strokePath (main, juce::PathStrokeType (3.5f));
        g.setColour (col::lcdOn.withAlpha (lit (1.0f)));
        g.strokePath (main, juce::PathStrokeType (1.3f));

        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText (depth <= 0.0f ? "fixed at " + fxHz (centre)
                                  : "sweep " + fxHz (fMin) + " " + dash + " " + fxHz (fMax),
                    bottom, juce::Justification::centredLeft, false);
        g.drawText (juce::String (stages / 2) + (stages == 2 ? " notch" : " notches"),
                    bottom, juce::Justification::centredRight, false);
    }
};

// ---------------------------------------------------------------------------
//  Reverb: the spectrum of the tail - a short burst of noise (10 ms, the same
//  noise every time so the picture holds still) through a private
//  vdx7fx::Reverb at the host's sample rate, and the whole tail it leaves
//  averaged into one spectrum (Hann-windowed FFTs, left and right summed),
//  20 Hz to 20 kHz on a 48 dB scale. A burst that
//  short has a lumpy spectrum of its own, which would pass straight through
//  into the picture, so each band is divided by the burst's own energy in
//  that band: what is left is the colour the reverb adds. Damping shows as
//  the top end falling away; Size and Feedback as how much tail there is to
//  hear, with its RT60
//  (the time it takes to fall 60 dB, measured from a separate click render
//  with Schroeder's backward integration) in the corner. With Freeze on, the
//  tank holds whatever is in it and takes no new input, so the picture dims
//  and says so.
// ---------------------------------------------------------------------------
class ReverbScope : public FxScope {
public:
    explicit ReverbScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, vdx7fx::ids::reverbOn, { vdx7fx::ids::reverbSize, vdx7fx::ids::reverbFb,
                                                vdx7fx::ids::reverbDamp, vdx7fx::ids::reverbRate,
                                                vdx7fx::ids::reverbDepth, vdx7fx::ids::reverbFreeze })
    {
        setTitle ("Reverb tail spectrum");
    }

    void paint (juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Spectrogram");
        auto bottom = in.removeFromBottom (10.0f);
        const auto plot = in.reduced (0.0f, 3.0f);
        const bool frozen = value (5) >= 0.5f;

        if (shown_.frames.empty() || shown_.burst.size() < 2)
            return;

        drawGlassCorner (g, r, frozen ? juce::String ("FROZEN") : "RT60 " + fxSeconds (shown_.rt60));

        const float sr = (float) shown_.rate;
        const float fLo = 0.0f, fHi = juce::jmin (20000.0f, sr * 0.49f);
        const float binHz = sr / (float) kFftSize;
        const int nb = (int) shown_.burst.size();

        // Spectrogram: time left to right, frequency (log) bottom to top,
        // level as phosphor brightness on a 60 dB scale. Each cell is the
        // tail's power in a sixth-octave band over the burst's own power there.
        const int w = juce::jmax (2, (int) plot.getWidth()), h = juce::jmax (2, (int) plot.getHeight());
        const int nf = (int) shown_.frames.size();
        // Each row reads only the bins inside its own pixel (so the
        // reassigned detail survives); the burst correction uses a smooth
        // sixth-octave average of the burst, scaled to the same bin count.
        std::vector<int> b0 ((size_t) h), b1 ((size_t) h);
        std::vector<double> src ((size_t) h, 0.0);
        // Linear frequency: every row spans the same number of Hz, so no
        // row is narrower than an FFT bin (on a log axis the bass rows were,
        // and showed as blocky steps).
        const float rowHz = (fHi - fLo) / (float) (h - 1);
        for (int py = 0; py < h; ++py) {
            const float f  = fLo + (fHi - fLo) * (1.0f - (float) py / (float) (h - 1));
            const float lo = f - 0.5f * rowHz, hi = f + 0.5f * rowHz;
            b0[(size_t) py] = juce::jlimit (1, nb - 1, juce::roundToInt (lo / binHz));
            b1[(size_t) py] = juce::jlimit (b0[(size_t) py], nb - 1, juce::roundToInt (hi / binHz));
            const int s0 = juce::jlimit (1, nb - 1, (int) std::floor (juce::jmin (f * 0.944f, f - 1.5f * binHz) / binHz));
            const int s1 = juce::jlimit (s0, nb - 1, (int) std::ceil (juce::jmax (f * 1.059f, f + 1.5f * binHz) / binHz));
            double avg = 0.0;
            for (int b = s0; b <= s1; ++b) avg += shown_.burst[(size_t) b];
            src[(size_t) py] = avg / (double) (s1 - s0 + 1) * (double) (b1[(size_t) py] - b0[(size_t) py] + 1);
        }
        std::vector<float> db ((size_t) (w * h), -300.0f);
        float top = -300.0f;
        for (int px = 0; px < w; ++px) {
            const auto& fr = shown_.frames[(size_t) juce::jlimit (0, nf - 1, px * nf / w)];
            for (int py = 0; py < h; ++py) {
                double e = 0.0;
                for (int b = b0[(size_t) py]; b <= b1[(size_t) py]; ++b) e += fr[(size_t) b];
                const float d = e > 1.0e-30 && src[(size_t) py] > 1.0e-30 ? (float) (10.0 * std::log10 (e / src[(size_t) py])) : -300.0f;
                db[(size_t) (py * w + px)] = d;
                top = juce::jmax (top, d);
            }
        }
        juce::Image img (juce::Image::ARGB, w, h, true);
        const float dim = frozen ? 0.35f : 1.0f;
        for (int py = 0; py < h; ++py)
            for (int px = 0; px < w; ++px) {
                const float t = juce::jlimit (0.0f, 1.0f, 1.0f + (db[(size_t) (py * w + px)] - top) / 60.0f);
                img.setPixelAt (px, py, col::lcdOn.withAlpha (lit (t * t * 0.95f * dim)));
            }
        g.drawImage (img, plot);

        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        for (float f : { 5000.0f, 10000.0f, 15000.0f }) {
            if (f >= fHi) continue;
            const float y = plot.getY() + (1.0f - (f - fLo) / (fHi - fLo)) * plot.getHeight();
            g.setColour (col::lcdOn.withAlpha (0.12f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (col::lcdOn.withAlpha (0.5f));
            g.drawText (juce::String ((int) (f / 1000.0f)) + "k",
                        juce::Rectangle<float> (plot.getX() + 2.0f, y - 9.0f, 24.0f, 9.0f),
                        juce::Justification::centredLeft, false);
        }
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        if (frozen) {
            const juce::String note ("tail held, input muted");
            const juce::Font font (juce::FontOptions (9.0f));
            juce::GlyphArrangement ga;
            ga.addLineOfText (font, note, 0.0f, 0.0f);
            const auto box = plot.withSizeKeepingCentre (ga.getBoundingBox (0, -1, true).getWidth() + 12.0f, 14.0f);
            g.setColour (col::lcdBg.withAlpha (0.9f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (col::lcdOn.withAlpha (0.35f));
            g.drawRoundedRectangle (box, 3.0f, 0.8f);
            g.setFont (font);
            g.setColour (col::lcdOn.withAlpha (0.9f));
            g.drawText (note, box, juce::Justification::centred, false);
        }
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawText ("0", bottom, juce::Justification::centredLeft, false);
        g.drawText (fxSeconds (shown_.window), bottom, juce::Justification::centredRight, false);
    }

protected:
    void valuesChanged() override {
        const Settings s { value (0), value (1), value (2), value (3), value (4), hostRate() };
        if (s == requested_) return;                   // Freeze only changes the drawing
        requested_ = s;
        worker_.request ([this, s] (const std::function<bool()>& stale) {
            Response resp;
            if (! render (s, resp, stale)) return;
            const juce::SpinLock::ScopedLockType sl (lock_);
            ready_ = std::move (resp);
            fresh_ = true;
        });
    }

    bool collectResults() override {
        if (! fresh_.exchange (false)) return false;
        const juce::SpinLock::ScopedLockType sl (lock_);
        shown_ = std::move (ready_);
        return true;
    }

private:
    static constexpr int kFftOrder = 11, kFftSize = 1 << kFftOrder;   // 2048 points

    struct Settings {
        float size = -1, feedback = 0, damping = 0, modRate = 0, modDepth = 0;
        double rate = 0;
        bool operator== (const Settings& o) const {
            return juce::exactlyEqual (size, o.size) && juce::exactlyEqual (feedback, o.feedback)
                && juce::exactlyEqual (damping, o.damping) && juce::exactlyEqual (modRate, o.modRate)
                && juce::exactlyEqual (modDepth, o.modDepth) && juce::exactlyEqual (rate, o.rate);
        }
    };
    struct Response {
        float  window = 0.0f, rt60 = 0.0f;   // seconds of tail analysed; its RT60
        double rate = 48000.0;
        std::vector<std::vector<float>> frames;   // the tail's power per FFT bin, one frame per time step
        std::vector<float> burst;            // the noise burst's own power per bin
    };

    static std::unique_ptr<vdx7fx::Reverb> makeReverb (const Settings& s, double sr, int block) {
        auto fx = std::make_unique<vdx7fx::Reverb>();
        fx->setEnabled (true);
        fx->setSize (s.size);
        fx->setFeedback (s.feedback);
        fx->setDamping (s.damping);
        fx->setModRate (s.modRate);
        fx->setModDepth (s.modDepth * (float) (sr / 48000.0));   // given in samples at 48 kHz
        fx->setFreeze (false);
        fx->setDryWet (100.0f);
        fx->prepare (sr, block);
        std::vector<float> l ((size_t) block, 0.0f), r ((size_t) block, 0.0f);
        for (int done = 0; done < (int) (sr * 0.06); done += block) {   // let the mix ramp fully wet
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
            fx->process (l.data(), r.data(), block);
        }
        return fx;
    }

    // RT60 from a click, rendered at a lower sample rate for long tails
    // (which leaves the envelope of a click the same). Returns -1 if stale.
    static float measureRt60 (const Settings& s, const std::function<bool()>& stale) {
        constexpr int kBlock = 512, kSlice = 32;

        // A first guess, to know how much to render: the FDN's matrix keeps
        // energy, so each trip round the lines (52 ms at Size 1 on average)
        // loses just the feedback gain. Damping only makes it shorter, and the
        // measurement takes care of that.
        const float loop  = juce::jlimit (0.0f, 0.98f, s.feedback * 0.8f);   // FxChain's Reverb
        const float trip  = 0.0522f * s.size;
        const float guess = loop > 0.001f ? 60.0f / (-20.0f * std::log10 (loop)) * trip : trip * 1.5f;
        const float length = juce::jlimit (0.25f, 30.0f, guess * 1.25f + 0.08f * s.size + 0.05f);
        const double sr = juce::jlimit (8000.0, 32000.0, 400000.0 / (double) length);
        auto fx = makeReverb (s, sr, kBlock);

        std::array<float, kBlock> l {}, rr {};
        const int total = juce::jmax (kBlock, (int) (length * sr));
        const int slices = (total + kSlice - 1) / kSlice;
        std::vector<double> e ((size_t) slices, 0.0);
        for (int pos = 0; pos < total; pos += kBlock) {
            if (stale()) return -1.0f;
            l.fill (0.0f); rr.fill (0.0f);
            if (pos == 0) l[0] = rr[0] = 1.0f;
            const int n = juce::jmin (kBlock, total - pos);
            fx->process (l.data(), rr.data(), n);
            for (int i = 0; i < n; ++i)
                e[(size_t) ((pos + i) / kSlice)] += (double) l[(size_t) i] * l[(size_t) i] + (double) rr[(size_t) i] * rr[(size_t) i];
        }

        // Schroeder: the energy still to come from each moment on, in dB; RT60
        // from its slope between -5 dB and -35 dB (or wherever it has got to
        // 60% of the way through the render, for very long tails).
        std::vector<double> edc ((size_t) slices + 1, 0.0);
        for (int k = slices - 1; k >= 0; --k) edc[(size_t) k] = edc[(size_t) k + 1] + e[(size_t) k];
        const double sliceSec = kSlice / sr;
        float rt60 = guess;
        if (edc[0] > 0.0) {
            auto dbAt = [&] (int k) { return 10.0 * std::log10 (juce::jmax (1.0e-30, edc[(size_t) k] / edc[0])); };
            int k5 = -1, kEnd = -1;
            const int limit = (int) (slices * 0.6);
            for (int k = 0; k < slices; ++k) {
                if (k5 < 0 && dbAt (k) <= -5.0) k5 = k;
                if (k5 >= 0 && (dbAt (k) <= -35.0 || k >= limit)) { kEnd = k; break; }
            }
            if (k5 >= 0 && kEnd > k5 && dbAt (kEnd) < -10.0)
                rt60 = (float) (-60.0 / ((dbAt (kEnd) + 5.0) / ((kEnd - k5) * sliceSec)));
        }
        return rt60;
    }

    // In-place radix-2 FFT.
    static void fft (std::vector<std::complex<float>>& x) {
        const size_t n = x.size();
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (x[i], x[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1) {
            const auto step = std::polar (1.0f, -juce::MathConstants<float>::twoPi / (float) len);
            for (size_t i = 0; i < n; i += len) {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t k = 0; k < len / 2; ++k) {
                    const auto a = x[i + k], b = x[i + k + len / 2] * w;
                    x[i + k] = a + b;
                    x[i + k + len / 2] = a - b;
                    w *= step;
                }
            }
        }
    }

    static bool render (const Settings& s, Response& out, const std::function<bool()>& stale) {
        out.rt60 = measureRt60 (s, stale);
        if (out.rt60 < 0.0f) return false;

        // The noise burst and its tail, at the host's rate so the spectrum
        // reaches 20 kHz. As much tail as the RT60, within 0.25..4 s.
        constexpr int kBlock = 512;
        const double sr = s.rate;
        out.rate = sr;
        out.window = juce::jlimit (0.25f, 4.0f, out.rt60);
        auto fx = makeReverb (s, sr, kBlock);

        const int burst = (int) (0.010 * sr);
        const int total = juce::jmax (kFftSize, (int) (out.window * sr) + burst);
        std::vector<float> left ((size_t) total, 0.0f), right ((size_t) total, 0.0f);
        uint32_t seed = 0x2545F491u;                   // the same noise every render
        for (int i = 0; i < burst; ++i) {
            seed = seed * 1664525u + 1013904223u;
            left[(size_t) i] = right[(size_t) i] = (float) (seed >> 8) / 8388608.0f - 1.0f;
        }
        std::vector<std::complex<float>> buf ((size_t) kFftSize);
        for (int i = 0; i < kFftSize; ++i) buf[(size_t) i] = { i < burst ? left[(size_t) i] : 0.0f, 0.0f };
        fft (buf);
        out.burst.resize ((size_t) kFftSize / 2 + 1);
        for (size_t k = 0; k < out.burst.size(); ++k) out.burst[k] = std::norm (buf[k]);

        for (int pos = 0; pos < total; pos += kBlock) {
            if (stale()) return false;
            fx->process (left.data() + pos, right.data() + pos, juce::jmin (kBlock, total - pos));
        }

        // Short-time FFTs (Hann), about 160 across the tail, left and right
        // summed - sharpened by reassignment: each bin's energy is moved to
        // the frequency (phase derivative, from an FFT with the window's time
        // derivative) and the time (group delay, from an FFT with the
        // time-ramped window) it really belongs to, instead of smearing over
        // the bin's width and the frame's length. Fixed, reasonable settings:
        // Hann window, bins more than 60 dB under the frame's level are left
        // where they are, shifts limited to 4 bins and 2 hops.
        std::vector<float> winH ((size_t) kFftSize), winDH ((size_t) kFftSize), winTH ((size_t) kFftSize);
        for (int i = 0; i < kFftSize; ++i) {
            const double ph = juce::MathConstants<double>::twoPi * i / kFftSize;
            winH[(size_t) i]  = (float) (0.5 * (1.0 - std::cos (ph)));
            winDH[(size_t) i] = (float) (juce::MathConstants<double>::pi / kFftSize * std::sin (ph));   // d/dn of h
            winTH[(size_t) i] = (float) (i - kFftSize / 2) * winH[(size_t) i];
        }
        const int hop = juce::jmax (64, (total - kFftSize) / 160);
        const int numFrames = (total - kFftSize) / hop + 1;
        const size_t nbins = (size_t) kFftSize / 2 + 1;
        out.frames.assign ((size_t) juce::jmax (1, numFrames), std::vector<float> (nbins, 0.0f));
        std::vector<std::complex<float>> bH ((size_t) kFftSize), bD ((size_t) kFftSize), bT ((size_t) kFftSize);
        constexpr float kThreshDb = -60.0f, kMaxBins = 4.0f, kMaxHops = 2.0f;
        for (int f = 0; f < numFrames; ++f) {
            if (stale()) return false;
            const int start = f * hop;
            for (const auto* ch : { &left, &right }) {
                for (int i = 0; i < kFftSize; ++i) {
                    const float x = (*ch)[(size_t) (start + i)];
                    bH[(size_t) i] = { x * winH[(size_t) i], 0.0f };
                    bD[(size_t) i] = { x * winDH[(size_t) i], 0.0f };
                    bT[(size_t) i] = { x * winTH[(size_t) i], 0.0f };
                }
                fft (bH); fft (bD); fft (bT);
                double sum = 0.0;
                for (size_t k = 1; k + 1 < nbins; ++k) sum += std::norm (bH[k]);
                const float floorPow = (float) (sum / (double) nbins) * std::pow (10.0f, kThreshDb / 10.0f);
                for (size_t k = 1; k + 1 < nbins; ++k) {
                    const float pw = std::norm (bH[k]);
                    if (pw <= 0.0f) continue;
                    size_t tk = k; int tf = f;
                    if (pw > floorPow) {
                        const auto den = bH[k] + std::complex<float> (1.0e-9f, 0.0f);
                        const float dBins = -(float) kFftSize / juce::MathConstants<float>::twoPi * (bD[k] / den).imag();
                        const float dHops = (bT[k] / den).real() / (float) hop;
                        if (std::abs (dBins) <= kMaxBins)
                            tk = (size_t) juce::jlimit (1, (int) nbins - 2, juce::roundToInt ((float) k + dBins));
                        if (std::abs (dHops) <= kMaxHops)
                            tf = juce::jlimit (0, numFrames - 1, juce::roundToInt ((float) f + dHops));
                    }
                    out.frames[(size_t) tf][tk] += pw;
                }
            }
        }
        return true;
    }

    Settings requested_;
    Response shown_, ready_;
    juce::SpinLock lock_;
    std::atomic<bool> fresh_ { false };
    ScopeWorker worker_ { "Reverb scope" };   // last: stopped before the rest goes
};

// ---------------------------------------------------------------------------
//  Chord: what one key plays, on a little keyboard around C3 - the key you
//  press in phosphor green, the notes the section adds in a paler mint, their
//  names above and the intervals below.
// ---------------------------------------------------------------------------
class ChordScope : public FxScope {
public:
    explicit ChordScope (juce::AudioProcessorValueTreeState& s)
        : FxScope (s, vdx7fx::ids::chordOn, { vdx7fx::ids::chordNote1, vdx7fx::ids::chordNote2,
                                               vdx7fx::ids::chordNote3, vdx7fx::ids::chordNote4,
                                               vdx7fx::ids::chordNote5, vdx7fx::ids::chordNote6,
                                               vdx7fx::ids::chordSustain })
    {
        setTitle ("Chord notes");
    }

    void paint (juce::Graphics& g) override {
        constexpr int root = 60;                       // C3, the DX7's middle C
        std::vector<int> added;
        for (size_t i = 0; i < 6; ++i) {
            const int st = juce::jlimit (-36, 36, juce::roundToInt (value (i)));
            if (st != 0) added.push_back (st);          // 0 = slot off
        }
        std::sort (added.begin(), added.end());
        added.erase (std::unique (added.begin(), added.end()), added.end());

        std::vector<int> notes { root };
        for (int st : added) notes.push_back (juce::jlimit (0, 127, root + st));
        std::sort (notes.begin(), notes.end());
        notes.erase (std::unique (notes.begin(), notes.end()), notes.end());

        const auto r = getLocalBounds().toFloat();
        auto in = drawGlass (g, r, "Chord  on C3");
        drawGlassCorner (g, r, juce::String ((int) notes.size()) + (notes.size() == 1 ? " note" : " notes")
                                 + (value (6) >= 0.5f ? "  hold" : ""));
        auto bottom = in.removeFromBottom (10.0f);

        // Note names on top, larger.
        auto names = in.removeFromTop (16.0f);
        juce::StringArray nameList;
        for (int n : notes) nameList.add (juce::MidiMessage::getMidiNoteName (n, true, true, 3));
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.setColour (col::lcdOn.withAlpha (lit (0.9f)));
        g.drawFittedText (nameList.joinIntoString ("  "), names.toNearestInt(), juce::Justification::centred, 1, 0.7f);

        // Whole octaves from the lowest note to the highest, two at least.
        auto floorDiv = [] (int a, int b) { return (a >= 0 ? a : a - b + 1) / b; };
        const int first = 12 * floorDiv (notes.front(), 12);
        int last = 12 * floorDiv (notes.back(), 12) + 11;
        if (last - first + 1 < 24) last = first + 23;

        auto isBlack = [] (int n) { const int pc = ((n % 12) + 12) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; };
        int whites = 0;
        for (int n = first; n <= last; ++n) if (! isBlack (n)) ++whites;

        auto area = in.reduced (0.0f, 4.0f);
        const float kw = area.getWidth() / (float) whites;
        const float kh = juce::jmin (area.getHeight(), juce::jmax (40.0f, kw * 5.5f));
        const auto kb = area.withSizeKeepingCentre (area.getWidth(), kh);

        const auto addedCol = col::lcdOn.interpolatedWith (juce::Colours::white, 0.45f);   // a paler mint
        auto fillFor = [&] (int n) -> juce::Colour {
            if (n == root) return col::lcdOn;
            if (std::find (notes.begin(), notes.end(), n) != notes.end()) return addedCol;
            return juce::Colours::transparentBlack;
        };

        std::vector<std::pair<int, juce::Rectangle<float>>> blacks;
        int wi = 0;
        for (int n = first; n <= last; ++n) {
            if (isBlack (n)) {
                const float x = kb.getX() + (float) wi * kw;   // straddles the line between two whites
                blacks.push_back ({ n, juce::Rectangle<float> (x - kw * 0.32f, kb.getY(), kw * 0.64f, kh * 0.6f) });
                continue;
            }
            const auto key = juce::Rectangle<float> (kb.getX() + (float) wi * kw, kb.getY(), kw, kh).reduced (0.5f, 0.0f);
            const auto fill = fillFor (n);
            if (! fill.isTransparent()) {
                g.setColour (fill.withAlpha (lit (0.2f)));
                g.fillRoundedRectangle (key.expanded (1.0f), 2.0f);
                g.setColour (fill.withAlpha (lit (0.9f)));
                g.fillRoundedRectangle (key, 1.5f);
            }
            g.setColour (col::lcdOn.withAlpha (0.3f));
            g.drawRoundedRectangle (key, 1.5f, 0.8f);
            ++wi;
        }
        for (auto& [n, key] : blacks) {
            const auto fill = fillFor (n);
            g.setColour (col::lcdBg.darker (0.5f));
            g.fillRoundedRectangle (key, 1.2f);
            if (! fill.isTransparent()) {
                g.setColour (fill.withAlpha (lit (0.9f)));
                g.fillRoundedRectangle (key.reduced (0.8f), 1.0f);
            }
            g.setColour (col::lcdOn.withAlpha (0.3f));
            g.drawRoundedRectangle (key, 1.2f, 0.8f);
        }

        juce::StringArray steps;
        for (int st : added) steps.add ((st > 0 ? "+" : "") + juce::String (st));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (col::lcdOn.withAlpha (0.75f));
        g.drawFittedText (added.empty() ? juce::String ("every interval off")
                                        : steps.joinIntoString (" ") + " st",
                          bottom.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
};

// ============================================================================
//  FxUnitPanel  -  one effect: header, on/off switch, its knobs, its scope.
//
//  Painted exactly like OperatorPanel / GlobalPanel (panel brown, amber
//  header band, dark edge) so the FX page reads as part of the same machine
//  rather than a bolted-on page. The knobs sit in rows of three at the same
//  size as the main page's; a short last row starts at the left. A unit's second
//  switch (the reverb's Freeze, the chord's Sustain) shares the top row with
//  its ON/OFF, which leaves the lower half free for the scope.
// ============================================================================
class FxUnitPanel : public juce::Component {
public:
    // `widthWeight` is how FxPanel divides its width between units - 1.0 is
    // a normal unit's share, smaller values ask for proportionally less.
    FxUnitPanel (const juce::String& titleText,
                 juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& enableParamId,
                 float widthWeight = 1.0f)
        : title_ (titleText), weight_ (widthWeight)
    {
        enableBtn.setButtonText ("OFF");
        enableBtn.setClickingTogglesState (true);
        enableBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
        enableBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        enableBtn.setTitle (titleText + " enable");
        enableBtn.setTooltip ("Change the " + titleText.toLowerCase() + " in and out");
        enableBtn.onStateChange = [this] {
            enableBtn.setButtonText (enableBtn.getToggleState() ? "ON" : "OFF");
        };
        addAndMakeVisible (enableBtn);

        if (auto* p = apvts.getParameter (enableParamId))
            enableAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p, enableBtn);
    }

    float widthWeight() const { return weight_; }

    // A switch in the knob grid, captioned like a knob, with a line of text
    // underneath where a knob prints its value (the delay's Self-Feedback,
    // sitting right under the Feedback knob it unlocks).
    struct SwitchTile : public juce::Component {
        SwitchTile (const juce::String& caption, juce::RangedAudioParameter& p,
                    std::function<juce::String (bool)> footerText)
            : caption_ (caption), footer_ (std::move (footerText))
        {
            btn.setButtonText ("OFF");
            btn.setClickingTogglesState (true);
            btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
            btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            btn.setTitle (p.getName (64));
            btn.setTooltip (p.getName (64));
            btn.onStateChange = [this] {
                const bool on = btn.getToggleState();
                btn.setButtonText (on ? "ON" : "OFF");
                if (on != lastOn_) { lastOn_ = on; repaint(); if (onChange) onChange(); }
            };
            addAndMakeVisible (btn);
            attachment = std::make_unique<juce::ButtonParameterAttachment> (p, btn);
        }
        void paint (juce::Graphics& g) override {
            auto b = getLocalBounds();
            g.setColour (col::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (caption_, b.removeFromTop (14), juce::Justification::centred);
            if (footer_) {
                g.setColour (col::text);
                g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
                g.drawText (footer_ (btn.getToggleState()), b.removeFromBottom (14), juce::Justification::centred);
            }
        }
        void resized() override {
            auto b = getLocalBounds();
            b.removeFromTop (14);
            b.removeFromBottom (14);
            btn.setBounds (b.withSizeKeepingCentre (juce::jmin (b.getWidth() - 12, 56), 24));
        }
        std::function<void()> onChange;   // after the switch has flipped
        juce::TextButton btn;
    private:
        juce::String caption_;
        std::function<juce::String (bool)> footer_;
        bool lastOn_ = false;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    // Adds a knob bound to `paramId` (a continuous, whole-number or choice
    // parameter - the phaser's Stages is a knob too).
    FxKnob* addKnob (const juce::String& caption,
                     juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramId)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr) return nullptr;
        auto k = std::make_unique<FxKnob> (caption, *p);
        auto* raw = k.get();
        addAndMakeVisible (*k);
        tiles.push_back (std::move (k));
        return raw;
    }

    // Adds a SwitchTile to the knob grid.
    SwitchTile* addSwitch (const juce::String& caption,
                           juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& paramId,
                           std::function<juce::String (bool)> footerText)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr) return nullptr;
        auto t = std::make_unique<SwitchTile> (caption, *p, std::move (footerText));
        auto* raw = t.get();
        addAndMakeVisible (*t);
        tiles.push_back (std::move (t));
        return raw;
    }

    // Adds a second switch next to ON/OFF (the reverb's Freeze).
    void addToggle (const juce::String& caption,
                    juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramId)
    {
        if (auto* p = apvts.getParameter (paramId)) {
            auto t = std::make_unique<Toggle> (caption, *p);
            addAndMakeVisible (*t);
            toggles.push_back (std::move (t));
        }
    }

    void setScope (std::unique_ptr<juce::Component> s) {
        scope = std::move (s);
        if (scope != nullptr) addAndMakeVisible (*scope);
        resized();
    }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto hdr = b.removeFromTop (22);
        g.setColour (col::accentDim);
        g.fillRoundedRectangle (hdr.toFloat().reduced (3, 3), 4.0f);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (title_, hdr, juce::Justification::centred);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromTop (24);
        b = b.reduced (6);

        auto top = b.removeFromTop (30).reduced (12, 2);
        if (toggles.empty()) {
            enableBtn.setBounds (top);
        } else {
            const int n = 1 + (int) toggles.size(), gap = 6;
            const int w = (top.getWidth() - gap * (n - 1)) / n;
            enableBtn.setBounds (top.removeFromLeft (w));
            for (auto& t : toggles) {
                top.removeFromLeft (gap);
                t->setBounds (top.removeFromLeft (w));
            }
        }
        b.removeFromTop (6);

        constexpr int kCols = 3, kRowH = 68;
        const int n = (int) tiles.size();
        if (n > 0) {
            const int rows = (n + kCols - 1) / kCols;
            const int cw = b.getWidth() / kCols;
            for (int i = 0; i < n; ++i) {
                const int row = i / kCols, c = i % kCols;
                tiles[(size_t) i]->setBounds (b.getX() + c * cw, b.getY() + row * kRowH, cw, kRowH);
            }
            b.removeFromTop (rows * kRowH);
        }

        if (scope != nullptr) {
            b.removeFromTop (6);
            scope->setBounds (b);
        }
    }

private:
    // A captioned switch that lights green while on, like ON/OFF.
    struct Toggle : public juce::TextButton {
        Toggle (const juce::String& caption, juce::RangedAudioParameter& p) {
            setButtonText (caption);
            setClickingTogglesState (true);
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
            setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            setTitle (p.getName (64));
            setTooltip (p.getName (64));
            attachment = std::make_unique<juce::ButtonParameterAttachment> (p, *this);
        }
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    juce::String title_;
    float weight_ = 1.0f;
    juce::TextButton enableBtn;
    std::unique_ptr<juce::ButtonParameterAttachment> enableAttachment;
    std::vector<std::unique_ptr<juce::Component>> tiles;   // knobs and switches, in grid order
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::unique_ptr<juce::Component> scope;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxUnitPanel)
};

// ============================================================================
//  FxPanel  -  the whole FX page: the four effect units side by side, in the
//  order they run (Chorus -> Delay -> Phaser -> Reverb), then the Chord
//  section, plus a signal-flow caption so the fixed routing is visible
//  rather than something you have to guess at.
//
//  Occupies the same rectangle the operator selector, operator panel,
//  algorithm view and global panel share, and is swapped in for all of them.
// ============================================================================
class FxPanel : public juce::Component {
public:
    explicit FxPanel (juce::AudioProcessorValueTreeState& apvts) {
        using namespace vdx7fx::ids;

        auto* chorus = addUnit ("CHORUS", apvts, chorusOn);
        chorus->addKnob ("RATE",   apvts, chorusRate);
        chorus->addKnob ("DEPTH",  apvts, chorusDepth);
        chorus->addKnob ("DELAY",  apvts, chorusDelay);
        chorus->addKnob ("SPREAD", apvts, chorusSpread);
        chorus->addKnob ("UNISON", apvts, chorusVoices);
        chorus->addKnob ("MIX",    apvts, chorusMix);
        chorus->setScope (std::make_unique<ChorusScope> (apvts));

        auto* delay = addUnit ("DELAY", apvts, delayOn);
        delay->addKnob ("TIME L", apvts, delayTimeL);
        delay->addKnob ("TIME R", apvts, delayTimeR);
        auto* fbKnob = delay->addKnob ("FBK", apvts, delayFb);
        delay->addKnob ("HPF",    apvts, delayHp);
        delay->addKnob ("MIX",    apvts, delayMix);
        // Allow Self-Feedback, right under the Feedback knob it unlocks. Off,
        // Feedback stops at 1.00 however far the knob turns (and the knob
        // prints the 1.00 that is actually used); on, the rest of its range
        // is live and the echoes can grow into self-oscillation.
        auto* selfFb = delay->addSwitch ("SELF-FB", apvts, delaySelfFb,
                                         [] (bool on) { return juce::String (on ? "cap at 1.20" : "cap at 1.00"); });
        if (fbKnob != nullptr && selfFb != nullptr) {
            selfFb->btn.setTooltip ("Allow Self-Feedback: lets FBK go above 1.00, where the echoes grow instead of fading");
            fbKnob->shownValue = [selfFb] (float v) { return selfFb->btn.getToggleState() ? v : juce::jmin (1.0f, v); };
            selfFb->onChange = [fbKnob] { fbKnob->repaint(); };
        }
        delay->setScope (std::make_unique<DelayScope> (apvts));

        auto* phaser = addUnit ("PHASER", apvts, phaserOn);
        phaser->addKnob ("RATE",   apvts, phaserRate);
        phaser->addKnob ("DEPTH",  apvts, phaserDepth);
        phaser->addKnob ("CENTER", apvts, phaserCentre);
        phaser->addKnob ("FBK",    apvts, phaserFb);
        phaser->addKnob ("STAGES", apvts, phaserStages);
        phaser->addKnob ("MIX",    apvts, phaserMix);
        phaser->setScope (std::make_unique<PhaserScope> (apvts));

        auto* reverb = addUnit ("REVERB", apvts, reverbOn);
        reverb->addToggle ("FREEZE", apvts, reverbFreeze);
        reverb->addKnob ("SIZE",   apvts, reverbSize);
        reverb->addKnob ("FBK",    apvts, reverbFb);
        reverb->addKnob ("DAMP",   apvts, reverbDamp);
        reverb->addKnob ("MOD HZ", apvts, reverbRate);
        reverb->addKnob ("MOD DP", apvts, reverbDepth);
        reverb->addKnob ("MIX",    apvts, reverbMix);
        reverb->setScope (std::make_unique<ReverbScope> (apvts));

        // Chord: six intervals (-36..+36 semitones each, 0 = off), 1-2-3 over
        // 4-5-6. It sits to the right of the audio effects and needs a bit
        // less width than they do. Sustain holds every note-off back (the
        // played note and any of the six added) for as long as it's on, and
        // lets them all go the moment it's switched back off.
        auto* chord = addUnit ("CHORD", apvts, chordOn, 0.9f);
        chord->addToggle ("SUSTAIN", apvts, chordSustain);
        chord->addKnob ("1", apvts, chordNote1);
        chord->addKnob ("2", apvts, chordNote2);
        chord->addKnob ("3", apvts, chordNote3);
        chord->addKnob ("4", apvts, chordNote4);
        chord->addKnob ("5", apvts, chordNote5);
        chord->addKnob ("6", apvts, chordNote6);
        chord->setScope (std::make_unique<ChordScope> (apvts));
    }

    void paint (juce::Graphics& g) override {
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("CHORD (per note)  +  DX7 ENGINE  >  CHORUS  >  DELAY  >  PHASER  >  REVERB  >  OUT",
                    getLocalBounds().removeFromBottom (16),
                    juce::Justification::centred);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromBottom (18);   // signal-flow caption

        const int gap = 8;
        const int n = (int) units.size();
        if (n == 0)
            return;

        // Units share the width by weight, so the Chord unit can be a little
        // narrower than the four audio effects without leaving a gap. The
        // last unit absorbs any rounding remainder so the row still fills
        // exactly to the right edge.
        float totalWeight = 0.0f;
        for (auto& u : units) totalWeight += u->widthWeight();
        const int usableW = b.getWidth() - gap * (n - 1);

        int x = b.getX();
        for (int i = 0; i < n; ++i) {
            const bool last = (i == n - 1);
            const int w = last ? (b.getRight() - x)
                                : juce::roundToInt ((float) usableW * units[(size_t) i]->widthWeight() / totalWeight);
            units[(size_t) i]->setBounds (x, b.getY(), w, b.getHeight());
            x += w + gap;
        }
    }

private:
    FxUnitPanel* addUnit (const juce::String& name,
                          juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& enableId,
                          float widthWeight = 1.0f)
    {
        auto u = std::make_unique<FxUnitPanel> (name, apvts, enableId, widthWeight);
        addAndMakeVisible (*u);
        auto* raw = u.get();
        units.push_back (std::move (u));
        return raw;
    }

    std::vector<std::unique_ptr<FxUnitPanel>> units;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
};

// ============================================================================
//  SoundingKeyboard  -  the on-screen keyboard, which also shows the notes the
//  engine plays without their key being held: the extra notes the Chord
//  section adds (and notes its Sustain keeps sounding). Keys you hold light up
//  as usual; the added notes get an amber tint, so a chord's shape is visible
//  on the keyboard while it plays.
//
//  `sounding` is written on the audio thread (VDX7AudioProcessor::
//  soundingNotes); its listener callbacks only raise a flag, and a timer on
//  the message thread turns that into a repaint.
// ============================================================================
class SoundingKeyboard : public juce::MidiKeyboardComponent {
public:
    SoundingKeyboard (juce::MidiKeyboardState& played, juce::MidiKeyboardState& sounding)
        : juce::MidiKeyboardComponent (played, juce::MidiKeyboardComponent::horizontalKeyboard),
          sounding_ (sounding),
          refresh_ ([this] { if (watcher_.dirty.exchange (false)) repaint(); })
    {
        sounding_.addListener (&watcher_);
        refresh_.startTimerHz (30);
    }
    ~SoundingKeyboard() override {
        refresh_.stopTimer();
        sounding_.removeListener (&watcher_);
    }

    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour) override
    {
        juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);
        if (! isDown && isAdded (note)) {
            g.setColour (col::accent.withAlpha (0.55f));
            g.fillRect (area.reduced (1.0f, 0.0f));
        }
    }

    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override
    {
        juce::MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, noteFillColour);
        if (! isDown && isAdded (note)) {
            g.setColour (col::accent.withAlpha (0.8f));
            g.fillRect (area.reduced (area.getWidth() * 0.12f, 0.0f).withTrimmedBottom (area.getHeight() * 0.1f));
        }
    }

private:
    bool isAdded (int note) const { return sounding_.isNoteOnForChannels (0xffff, note); }

    // Called on the audio thread: only raise a flag.
    struct Watcher : juce::MidiKeyboardState::Listener {
        std::atomic<bool> dirty { false };
        void handleNoteOn  (juce::MidiKeyboardState*, int, int, float) override { dirty.store (true); }
        void handleNoteOff (juce::MidiKeyboardState*, int, int, float) override { dirty.store (true); }
    };

    // Message thread: turns the flag into a repaint. (A plain nested Timer
    // rather than juce::TimedCallback, so older JUCE 8 releases build too.)
    struct Refresher : juce::Timer {
        explicit Refresher (std::function<void()> f) : fn (std::move (f)) {}
        void timerCallback() override { fn(); }
        std::function<void()> fn;
    };

    juce::MidiKeyboardState& sounding_;
    Watcher watcher_;
    Refresher refresh_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundingKeyboard)
};

// ============================================================================
//  ChevronButton  -  a small button that shows only a dropdown chevron, drawn
//  like the combo boxes' own arrows. Used for the ROM menu, which is set up
//  once and then rarely touched, so it stays out of the way at the end of the
//  header row.
// ============================================================================
class ChevronButton : public juce::TextButton {
public:
    void paintButton (juce::Graphics& g, bool over, bool down) override {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
        const auto c = getLocalBounds().toFloat().getCentre();
        juce::Path p;
        p.startNewSubPath (c.x - 7.0f, c.y - 2.5f);
        p.lineTo (c.x, c.y + 3.0f);
        p.lineTo (c.x + 7.0f, c.y - 2.5f);
        g.setColour (findColour (juce::TextButton::textColourOffId));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
};

// ============================================================================
//  MenuButton  -  a small "v" (optionally with a word in front of it) that
//  opens a menu, used in the header's second row for settings that are
//  switched rather than dialled: the controller assignments and the MORE menu.
//  A single click opens the menu; a double click does not, it calls onReset
//  instead (every control in that row returns to its default on a double
//  click). To tell the two apart the menu opens once the double-click interval
//  has passed without a second click. `lit` draws the chevron amber, e.g. while
//  an assignment is active, and dim otherwise.
// ============================================================================
class MenuButton : public juce::TextButton, private juce::Timer {
public:
    std::function<void()> onMenu, onReset;
    bool lit = true;

    void paintButton (juce::Graphics& g, bool over, bool down) override {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
        auto b = getLocalBounds().toFloat();
        const auto text = getButtonText();
        if (text.isNotEmpty()) {
            g.setColour (col::text);
            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            g.drawText (text, b.withTrimmedRight (12.0f), juce::Justification::centred, false);
            b = b.removeFromRight (16.0f);
        }
        const auto c = b.getCentre();
        const float hw = juce::jmin (4.0f, b.getWidth() * 0.28f);
        juce::Path p;
        p.startNewSubPath (c.x - hw, c.y - hw * 0.45f);
        p.lineTo (c.x, c.y + hw * 0.5f);
        p.lineTo (c.x + hw, c.y - hw * 0.45f);
        g.setColour (lit ? col::accent : col::textDim.withAlpha (0.6f));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

    void clicked() override {
        if (isTimerRunning()) {                     // second click in time: reset
            stopTimer();
            if (onReset) onReset();
            return;
        }
        startTimer (juce::MouseEvent::getDoubleClickTimeout());
    }

private:
    void timerCallback() override {
        stopTimer();
        if (onMenu) onMenu();
    }
};

// Returns a toggle button's parameter to its default on a double click. The
// second click of the pair still toggles the button on its mouse-up, so the
// reset waits for that mouse-up (listeners hear it after the button has acted)
// and has the last word.
class ResetOnDoubleClick : public juce::MouseListener {
public:
    explicit ResetOnDoubleClick (juce::RangedAudioParameter& p) : param (p) {}
    void mouseDoubleClick (const juce::MouseEvent&) override { pending = true; }
    void mouseUp (const juce::MouseEvent&) override {
        if (! pending) return;
        pending = false;
        param.beginChangeGesture();
        param.setValueNotifyingHost (param.getDefaultValue());
        param.endChangeGesture();
    }
private:
    juce::RangedAudioParameter& param;
    bool pending = false;
};

// ============================================================================
//  ContentPanel  -  fixed-design-resolution container for the whole UI.
//  The editor itself may be any size/aspect ratio (e.g. an Android device's
//  full screen); this panel stays at its native pixel layout and gets scaled
//  as a whole (via Component::setTransform) to fit inside the editor,
//  centred, preserving aspect ratio. That means nothing ever gets clipped or
//  hidden on odd aspect ratios - you just get even letterbox/pillarbox
//  margins painted in the normal background colour instead of stray black
//  bars, and the internal layout code never has to know about screen shape.
// ============================================================================
class ContentPanel : public juce::Component {
public:
    void paint (juce::Graphics& g) override {
        g.setColour (col::accentDim);
        g.fillRect (0, 86, getWidth(), 2);
    }
};

} // namespace vdx7ui

// ============================================================================
//  Editor
// ============================================================================
class VDX7AudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::ChangeListener
{
public:
    explicit VDX7AudioProcessorEditor (VDX7AudioProcessor&);
    ~VDX7AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshAll();
    void selectOperator (int op);
    void setFxViewVisible (bool shouldShowFx);   // swaps the voice page for the FX page
    void rebuildProgramList (bool sendSelect);
    void rebuildBankList();    // 8 factory banks with a ROM, 1 starter bank without
    void importBankFromFile();
    void loadRomFile (bool firmware);   // firmware ROM, or the factory voice set
    void refreshEngineStatus();
    void exportToFile (bool wholeBank);
    void saveIntoBankFile();   // save current voice into a slot of an existing .syx bank
    void layoutContent();      // lays out everything inside `content` at the fixed design size

    // Fixed design resolution. All child components are laid out against this
    // size regardless of the editor's actual size; `content` is then scaled
    // as a whole (letterboxed/pillarboxed, centred) to fit whatever window or
    // screen the host/standalone app gives us. This is what keeps controls
    // from being clipped on unusual aspect ratios (e.g. 21:9 or 16:9 phones).
    static constexpr int kDesignW = 1180;
    static constexpr int kDesignH = 594;

    VDX7AudioProcessor& processor;
    vdx7ui::DXLookAndFeel lnf;
    vdx7ui::CompactLookAndFeel compactLnf;   // selector buttons + their level knobs; outlives them
    juce::TooltipWindow   tooltips { this, 600 };   // shows knob descriptions

    vdx7ui::ContentPanel content;   // holds every visible child at design resolution

    juce::Label       title;
    juce::ComboBox    bankBox, progBox;
    juce::TextButton  prevBtn { "<" }, nextBtn { ">" }, initBtn { "INIT" }, fileBtn { "FILE" };
    juce::TextButton  fxBtn   { "FX" };   // top left, toggles the FX page
    vdx7ui::ChevronButton romBtn;         // "v" at the end of the row: firmware / factory-voice loading
    std::unique_ptr<juce::FileChooser> chooser;
    static constexpr int kUserBankId = 100;
    juce::String openBankFileName_;   // filename of the currently-open user bank (save-protection)

    vdx7ui::LcdComponent  lcd;
    vdx7ui::AlgorithmCard algoCard;   // ALGORITHM card: algorithm, feedback, key sync + diagram
    std::vector<std::unique_ptr<vdx7ui::OperatorPanel>> ops;
    std::unique_ptr<vdx7ui::GlobalPanel> global;
    std::unique_ptr<vdx7ui::FxPanel>     fxPanel;
    bool showingFx_ = false;
    vdx7ui::CardFrame opCard { "OPERATORS  |  VOLUME" };   // module card behind the selector + level knobs
    std::array<juce::TextButton,6> opSelect;   // operator selector: two rows of three (Operator 1..6)
    std::array<juce::Slider,6>     opLevel;    // each operator's output level, beside its button
    int selectedOp_ = 0;
    vdx7ui::EnvelopeView  envView;              // the six operator EGs, under the selector
    vdx7ui::SoundingKeyboard keyboard;   // held keys + chord-added notes

    // Header, second row: the DX7's FUNCTION page (see setupFunctionRow), plus
    // the native engine's DC blocker knob at its end.
    vdx7ui::HeaderLookAndFeel headerLnf;   // outlives the controls below
    juce::Slider      tuneBar, portaTimeBar, pbBar;
    juce::TextButton  monoBtn, portaModeBtn;
    std::array<juce::Slider, 4> ctlRange;             // MW, FC, BC, AT range
    std::array<vdx7ui::MenuButton, 4> ctlAssign;      // ... and each one's Pitch / Amp / EG Bias menu
    vdx7ui::MenuButton moreBtn;                       // MIDI channel + memory protect
    juce::Slider      dcKnob;
    std::vector<std::unique_ptr<vdx7ui::ResetOnDoubleClick>> resetListeners;
    void setupFunctionRow();
    void showAssignMenu (int controller);
    void showMoreMenu();
    void refreshFunctionMenus();   // chevron lit state + tooltips follow the parameters
    void setParamValue (const juce::String& id, float value);   // plain value, with a gesture
    void resetParam (const juce::String& id);
    void updateFunctionLabels();   // POLY/MONO and the portamento mode's wording
    void layoutFunctionRow (juce::Rectangle<int> row);

    std::vector<vdx7ui::ParamSlider*> allSliders;   // non-owning, for refresh
    // Binds each knob to its APVTS parameter (two-way, host-automation aware).
    // Declared last so it is destroyed first, before the sliders it references.
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> attachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::ParameterAttachment>>       fnWatchers;   // menu-driven params -> refreshFunctionMenus
    bool updatingUI = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VDX7AudioProcessorEditor)
};