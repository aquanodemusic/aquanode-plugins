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
#include <array>
#include <functional>
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
    const juce::Colour lcdBg     (0xff10301c);   // dark green
    const juce::Colour lcdOn     (0xff7dffae);   // phosphor green
    const juce::Colour ledOn     (0xffff5a44);   // red 7-seg
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

    juce::Font getLabelFont (juce::Label&) override {
        return juce::Font (juce::FontOptions (12.0f));
    }
};

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

        g.setColour (col::lcdBg);
        g.fillRoundedRectangle (b.reduced (4.0f), 4.0f);

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
//  the output baseline) and which operator carries the feedback loop.
//
//  The connection topology was decoded directly from the emulator's own DX7
//  algorithm ROM (External/OPS.h), so what is drawn matches what is heard.
// ============================================================================
class AlgoComponent : public juce::Component, private juce::Timer {
public:
    std::function<int()> algoProvider;      // 0..31
    std::function<int()> feedbackProvider;  // 0..7
    std::function<std::array<int,6>()> opLevelProvider; // OP1..OP6 output levels

    AlgoComponent() { setTitle ("Algorithm routing"); startTimerHz (15); }
    ~AlgoComponent() override { stopTimer(); }

    // dest[i] = operator that OP(i+1) feeds (0 = final output/carrier); fb = the
    // operator (1..6) carrying the feedback loop.
    struct AlgoInfo { int dest[6]; int fb; };

    static const AlgoInfo& info (int algo /*0..31*/) {
        static const AlgoInfo table[32] = {
            { {0,1,0,3,4,5}, 6 }, { {0,1,0,3,4,5}, 2 }, { {0,1,2,0,4,5}, 6 }, { {0,1,2,0,4,5}, 4 },
            { {0,1,0,3,0,5}, 6 }, { {0,1,0,3,0,5}, 5 }, { {0,1,0,3,3,5}, 6 }, { {0,1,0,3,3,5}, 4 },
            { {0,1,0,3,3,5}, 2 }, { {0,1,2,0,4,4}, 3 }, { {0,1,2,0,4,4}, 6 }, { {0,1,0,3,3,3}, 2 },
            { {0,1,0,3,3,3}, 6 }, { {0,1,0,3,4,4}, 6 }, { {0,1,0,3,4,4}, 2 }, { {0,1,1,3,1,5}, 6 },
            { {0,1,1,3,1,5}, 2 }, { {0,1,1,1,4,5}, 3 }, { {0,1,2,0,0,5}, 6 }, { {0,0,2,0,4,4}, 3 },
            { {0,0,2,0,0,5}, 3 }, { {0,1,0,0,0,5}, 6 }, { {0,0,2,0,0,5}, 6 }, { {0,0,0,0,0,5}, 6 },
            { {0,0,0,0,0,5}, 6 }, { {0,0,2,0,4,4}, 6 }, { {0,0,2,0,4,4}, 3 }, { {0,1,0,3,4,0}, 5 },
            { {0,0,0,3,0,5}, 6 }, { {0,0,0,3,4,0}, 5 }, { {0,0,0,0,0,0}, 6 }, { {0,0,0,0,0,0}, 6 },
        };
        return table[juce::jlimit (0, 31, algo)];
    }

    void paint (juce::Graphics& g) override {
        auto b = getLocalBounds().reduced (6);
        g.setColour (col::panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 6.0f, 1.0f);

        auto inner = b.reduced (8);
        const int algo = algoProvider ? algoProvider() : 0;
        const int fb   = feedbackProvider ? feedbackProvider() : 0;

        auto header = inner.removeFromTop (24);
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("ALGORITHM", header.removeFromLeft (86), juce::Justification::centredLeft);
        g.setColour (col::accent);
        g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
        g.drawText (juce::String (algo + 1), header.removeFromLeft (40), juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("FEEDBACK " + juce::String (fb), header, juce::Justification::centredRight);

        drawRouting (g, inner.reduced (2, 2), algo, fb);
    }

private:
    void timerCallback() override { repaint(); }

    void drawRouting (juce::Graphics& g, juce::Rectangle<int> area, int algo, int fb) {
        const AlgoInfo& a = info (algo);

        std::array<int,6> lv { {0,0,0,0,0,0} };
        if (opLevelProvider) lv = opLevelProvider();

        // Assign each operator a "rank" = distance to the output along its chain,
        // so modulators sit above the carriers they feed.  Carriers = rank 0.
        int rank[7] = {0,0,0,0,0,0,0};
        for (int pass = 0; pass < 6; ++pass)
            for (int op = 1; op <= 6; ++op) {
                int d = a.dest[op-1];
                if (d != 0) rank[op] = juce::jmax (rank[op], rank[d] + 1);
            }
        int maxRank = 0;
        for (int op = 1; op <= 6; ++op) maxRank = juce::jmax (maxRank, rank[op]);

        // Column per operator (OP1..OP6 left to right), row per rank (0 at bottom).
        const float boxW = juce::jmin (34.0f, area.getWidth() / 6.4f);
        const float boxH = juce::jmin (24.0f, area.getHeight() / (float) (maxRank + 1) - 6.0f);
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

        // Modulation arrows (from modulator box bottom to target box top).
        for (int op = 1; op <= 6; ++op) {
            int d = a.dest[op-1];
            auto from = boxFor (op);
            if (d == 0) {
                // carrier -> drop to the output baseline
                drawArrow (g, from.getCentreX(), from.getBottom(),
                           from.getCentreX(), (float) area.getBottom(), col::accentDim);
            } else {
                auto to = boxFor (d);
                drawArrow (g, from.getCentreX(), from.getBottom(),
                           to.getCentreX(), to.getY(), col::accent.withAlpha (0.85f));
            }
        }

        // Operator boxes on top of the wiring.
        for (int op = 1; op <= 6; ++op) {
            auto r = boxFor (op);
            const bool carrier = (a.dest[op-1] == 0);
            const bool active  = lv[(size_t)(op-1)] > 0;
            g.setColour (carrier ? col::accentDim : col::panelHi);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (active ? col::accent : col::edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, carrier ? 1.6f : 1.0f);
            g.setColour (active ? col::text : col::textDim);
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText (juce::String (op), r, juce::Justification::centred);

            if (op == fb) drawFeedbackLoop (g, r);
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

    static void drawFeedbackLoop (juce::Graphics& g, juce::Rectangle<float> r) {
        // Small loop hugging the top-right corner of the feedback operator.
        auto loop = juce::Rectangle<float> (r.getRight() - 4.0f, r.getY() - 8.0f, 11.0f, 11.0f);
        juce::Path p;
        p.addArc (loop.getX(), loop.getY(), loop.getWidth(), loop.getHeight(),
                  0.4f, juce::MathConstants<float>::twoPi - 0.2f, true);
        g.setColour (col::lcdOn);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }
};

// ============================================================================
//  OperatorPanel  -  one DX7 operator's 21 editable parameters.
// ============================================================================
class OperatorPanel : public juce::Component {
public:
    // displayIndex: 1..6 (OP1..OP6).  vcedOp: 0..5 block index in the VCED.
    OperatorPanel (int displayIndex, int vcedOp, SliderFactory make)
        : opDisplay (displayIndex)
    {
        using namespace vdx7;
        const int base = vcedOp * kOpVcedStride;
        const juce::String opTag = "OP" + juce::String (displayIndex) + " ";
        auto add = [&](const juce::String& cap, const juce::String& full,
                       int mn, int mx, int off){
            auto* s = make (cap, opTag + full, mn, mx, base + off);
            owned.emplace_back (s);
            addAndMakeVisible (s);
        };
        // 10 knobs per row.  Row 0 ends with R1-R4 at the right; row 1 ends with
        // L1-L4 at the right; detune sits alone on the last row.
        // Row 0: OUT CRS FINE MODE  AMS KVS  R1 R2 R3 R4
        add ("OUT",  "Output Level",              0, 99, OP_OL);
        add ("CRS",  "Frequency Coarse",          0, 31, OP_FC);
        add ("FINE", "Frequency Fine",            0, 99, OP_FF);
        add ("MODE", "Oscillator Mode (0 ratio / 1 fixed)", 0, 1, OP_MODE);
        add ("AMS",  "Amplitude Mod Sensitivity",    0, 3,  OP_AMS);
        add ("KVS",  "Key Velocity Sensitivity",     0, 7,  OP_KVS);
        add ("R1",   "EG Rate 1 (attack)",        0, 99, OP_R1);
        add ("R2",   "EG Rate 2 (decay 1)",       0, 99, OP_R2);
        add ("R3",   "EG Rate 3 (decay 2)",       0, 99, OP_R3);
        add ("R4",   "EG Rate 4 (release)",       0, 99, OP_R4);
        // Row 1: BKPT LDEP RDEP LCRV RCRV RS  L1 L2 L3 L4
        add ("BKPT", "Keyboard Scaling Break Point", 0, 99, OP_BP);
        add ("LDEP", "Keyboard Scaling Left Depth",  0, 99, OP_LD);
        add ("RDEP", "Keyboard Scaling Right Depth", 0, 99, OP_RD);
        add ("LCRV", "Keyboard Scaling Left Curve",  0, 3,  OP_LC);
        add ("RCRV", "Keyboard Scaling Right Curve", 0, 3,  OP_RC);
        add ("RS",   "Keyboard Rate Scaling",        0, 7,  OP_RS);
        add ("L1",   "EG Level 1 (attack)",       0, 99, OP_L1);
        add ("L2",   "EG Level 2 (decay 1)",      0, 99, OP_L2);
        add ("L3",   "EG Level 3 (sustain)",      0, 99, OP_L3);
        add ("L4",   "EG Level 4 (release)",      0, 99, OP_L4);
        // Row 2: DET
        add ("DET",  "Detune (7 = centre)",       0, 14, OP_DET);
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
        g.drawText ("OP " + juce::String (opDisplay), hdr, juce::Justification::centred);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromTop (24);
        b = b.reduced (4);
        const int cols = 10;  // 10 knobs per row (matches the global/LFO panel)
        const int rows = (int) owned.size() / cols + ((int) owned.size() % cols ? 1 : 0);
        const int cw = b.getWidth() / cols;
        const int ch = b.getHeight() / juce::jmax (1, rows);
        for (int i = 0; i < (int) owned.size(); ++i) {
            int r = i / cols, c = i % cols;
            owned[(size_t) i]->setBounds (b.getX() + c*cw, b.getY() + r*ch, cw, ch);
        }
    }

private:
    int opDisplay;
    std::vector<std::unique_ptr<ParamSlider>> owned;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OperatorPanel)
};

// ============================================================================
//  GlobalPanel  -  global / LFO / pitch-EG parameters.
// ============================================================================
class GlobalPanel : public juce::Component {
public:
    explicit GlobalPanel (SliderFactory make) {
        using namespace vdx7;
        auto add = [&](const juce::String& cap, const juce::String& full,
                       int mn, int mx, int off){
            auto* s = make (cap, full, mn, mx, off);
            owned.emplace_back (s);
            addAndMakeVisible (s);
        };
        add ("ALGO",   "Algorithm",                     0, 31, G_ALG);
        add ("FBK",    "Feedback",                      0, 7,  G_FB);
        add ("OKS",    "Oscillator Key Sync",           0, 1,  G_OKS);
        add ("TRANSP", "Transpose (24 = C3)",           0, 48, G_TRANSPOSE);
        add ("PMS",    "LFO Pitch Mod Sensitivity",     0, 7,  G_LPMS);
        add ("LFO WV", "LFO Waveform",                  0, 5,  G_LFW);
        add ("LFO SP", "LFO Speed",                     0, 99, G_LFS);
        add ("LFO DL", "LFO Delay",                     0, 99, G_LFD);
        add ("LFO PM", "LFO Pitch Mod Depth",           0, 99, G_LPMD);
        add ("LFO AM", "LFO Amplitude Mod Depth",       0, 99, G_LAMD);
        add ("LFO KS", "LFO Key Sync",                  0, 1,  G_LFKS);
        add ("PR1",    "Pitch EG Rate 1",               0, 99, G_PR1);
        add ("PR2",    "Pitch EG Rate 2",               0, 99, G_PR2);
        add ("PR3",    "Pitch EG Rate 3",               0, 99, G_PR3);
        add ("PR4",    "Pitch EG Rate 4",               0, 99, G_PR4);
        add ("PL1",    "Pitch EG Level 1",              0, 99, G_PL1);
        add ("PL2",    "Pitch EG Level 2",              0, 99, G_PL2);
        add ("PL3",    "Pitch EG Level 3",              0, 99, G_PL3);
        add ("PL4",    "Pitch EG Level 4",              0, 99, G_PL4);
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
        g.drawText ("GLOBAL  /  LFO  /  PITCH EG", hdr, juce::Justification::centred);
    }

    void resized() override {
        auto b = getLocalBounds();
        b.removeFromTop (24);
        b = b.reduced (4);
        const int cols = 10;
        const int rows = (int) owned.size() / cols + ((int) owned.size() % cols ? 1 : 0);
        const int cw = b.getWidth() / cols;
        const int ch = b.getHeight() / juce::jmax (1, rows);
        for (int i = 0; i < (int) owned.size(); ++i) {
            int r = i / cols, c = i % cols;
            owned[(size_t) i]->setBounds (b.getX() + c*cw, b.getY() + r*ch, cw, ch);
        }
    }

private:
    std::vector<std::unique_ptr<ParamSlider>> owned;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalPanel)
};

// ============================================================================
//  FxKnob  -  ParamSlider's continuous sibling.
//
//  ParamSlider is integer-only: it steps by 1 and prints (int) getValue(),
//  which is right for VCED bytes but wrong for 0.5 Hz or 137.4 ms. This one
//  takes its range and its printed text straight from the APVTS parameter, so
//  a log-skewed frequency knob reads correctly without the UI knowing which
//  units it is showing. Same tile geometry and same painted caption/value
//  bands as ParamSlider, so the two look identical side by side.
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

private:
    juce::String valueText() const {
        auto txt = param.getCurrentValueAsText();
        auto unit = param.getLabel();
        return unit.isEmpty() ? txt : txt + " " + unit;
    }

    juce::RangedAudioParameter& param;
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::String caption_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxKnob)
};

// ============================================================================
//  FxUnitPanel  -  one effect: header, on/off switch, and its knobs.
//
//  Painted exactly like OperatorPanel / GlobalPanel (panel brown, amber
//  header band, dark edge) so the FX page reads as part of the same machine
//  rather than a bolted-on page.
// ============================================================================
class FxUnitPanel : public juce::Component {
public:
    FxUnitPanel (const juce::String& titleText,
                 juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& enableParamId)
        : title_ (titleText)
    {
        enableBtn.setButtonText ("OFF");
        enableBtn.setClickingTogglesState (true);
        enableBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
        enableBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        enableBtn.setTitle (titleText + " enable");
        enableBtn.setTooltip ("Switch the " + titleText.toLowerCase() + " in and out");
        enableBtn.onStateChange = [this] {
            enableBtn.setButtonText (enableBtn.getToggleState() ? "ON" : "OFF");
        };
        addAndMakeVisible (enableBtn);

        if (auto* p = apvts.getParameter (enableParamId))
            enableAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p, enableBtn);
    }

    // Adds a continuous knob bound to `paramId`.
    void addKnob (const juce::String& caption,
                  juce::AudioProcessorValueTreeState& apvts,
                  const juce::String& paramId)
    {
        if (auto* p = apvts.getParameter (paramId)) {
            auto k = std::make_unique<FxKnob> (caption, *p);
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
    }

    // Adds a captioned combo box (the phaser's stage count) in a knob-sized
    // tile, so it lines up with the rotaries around it.
    void addChoice (const juce::String& caption,
                    juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramId)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr)
            return;

        auto tile = std::make_unique<ChoiceTile> (caption, *p);
        addAndMakeVisible (*tile);
        choices.push_back (std::move (tile));
    }

    // Adds a second toggle inside the panel (the reverb's Freeze).
    void addToggle (const juce::String& caption,
                    juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramId)
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr)
            return;

        auto tile = std::make_unique<ToggleTile> (caption, *p);
        addAndMakeVisible (*tile);
        toggles.push_back (std::move (tile));
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

        enableBtn.setBounds (b.removeFromTop (30).reduced (12, 2));
        b.removeFromTop (6);

        // Every control gets the same tile size, laid out two per row, so the
        // four panels line up with each other however many knobs each has.
        std::vector<juce::Component*> tiles;
        for (auto& k : knobs)   tiles.push_back (k.get());
        for (auto& c : choices) tiles.push_back (c.get());
        for (auto& t : toggles) tiles.push_back (t.get());
        if (tiles.empty())
            return;

        const int cols = 2;
        const int rows = ((int) tiles.size() + cols - 1) / cols;
        const int cw = b.getWidth() / cols;
        const int ch = b.getHeight() / juce::jmax (1, rows);
        for (int i = 0; i < (int) tiles.size(); ++i) {
            const int r = i / cols, c = i % cols;
            tiles[(size_t) i]->setBounds (b.getX() + c*cw, b.getY() + r*ch, cw, ch);
        }
    }

private:
    // A combo box wearing a knob tile's caption band, so mixed rows stay tidy.
    struct ChoiceTile : public juce::Component {
        ChoiceTile (const juce::String& caption, juce::RangedAudioParameter& p)
            : caption_ (caption)
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&p))
                box.addItemList (choice->choices, 1);
            box.setTitle (p.getName (64));
            box.setTooltip (p.getName (64));
            addAndMakeVisible (box);
            attachment = std::make_unique<juce::ComboBoxParameterAttachment> (p, box);
        }
        void paint (juce::Graphics& g) override {
            g.setColour (col::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (caption_, getLocalBounds().removeFromTop (14),
                        juce::Justification::centred);
        }
        void resized() override {
            auto b = getLocalBounds();
            b.removeFromTop (14);
            b.removeFromBottom (14);
            box.setBounds (b.reduced (6, juce::jmax (0, (b.getHeight() - 28) / 2)));
        }
        juce::String caption_;
        juce::ComboBox box;
        std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
    };

    // Same idea for a boolean.
    struct ToggleTile : public juce::Component {
        ToggleTile (const juce::String& caption, juce::RangedAudioParameter& p)
            : caption_ (caption)
        {
            btn.setButtonText ("OFF");
            btn.setClickingTogglesState (true);
            btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
            btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            btn.setTitle (p.getName (64));
            btn.setTooltip (p.getName (64));
            btn.onStateChange = [this] {
                btn.setButtonText (btn.getToggleState() ? "ON" : "OFF");
            };
            addAndMakeVisible (btn);
            attachment = std::make_unique<juce::ButtonParameterAttachment> (p, btn);
        }
        void paint (juce::Graphics& g) override {
            g.setColour (col::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (caption_, getLocalBounds().removeFromTop (14),
                        juce::Justification::centred);
        }
        void resized() override {
            auto b = getLocalBounds();
            b.removeFromTop (14);
            b.removeFromBottom (14);
            btn.setBounds (b.reduced (6, juce::jmax (0, (b.getHeight() - 28) / 2)));
        }
        juce::String caption_;
        juce::TextButton btn;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    };

    juce::String title_;
    juce::TextButton enableBtn;
    std::unique_ptr<juce::ButtonParameterAttachment> enableAttachment;
    std::vector<std::unique_ptr<FxKnob>>     knobs;
    std::vector<std::unique_ptr<ChoiceTile>> choices;
    std::vector<std::unique_ptr<ToggleTile>> toggles;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxUnitPanel)
};

// ============================================================================
//  FxPanel  -  the whole FX page: four effect strips side by side, in the
//  order they run (Chorus -> Delay -> Phaser -> Reverb), plus a signal-flow
//  caption so the fixed routing is visible rather than something you have to
//  guess at.
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
        chorus->addKnob ("MIX",    apvts, chorusMix);

        auto* delay = addUnit ("DELAY", apvts, delayOn);
        delay->addKnob ("TIME L", apvts, delayTimeL);
        delay->addKnob ("TIME R", apvts, delayTimeR);
        delay->addKnob ("FBK",    apvts, delayFb);
        delay->addKnob ("HPF",    apvts, delayHp);
        delay->addKnob ("MIX",    apvts, delayMix);

        auto* phaser = addUnit ("PHASER", apvts, phaserOn);
        phaser->addKnob   ("RATE",   apvts, phaserRate);
        phaser->addKnob   ("DEPTH",  apvts, phaserDepth);
        phaser->addKnob   ("CENTER", apvts, phaserCentre);
        phaser->addKnob   ("FBK",    apvts, phaserFb);
        phaser->addKnob   ("MIX",    apvts, phaserMix);
        phaser->addChoice ("STAGES", apvts, phaserStages);

        auto* reverb = addUnit ("REVERB", apvts, reverbOn);
        reverb->addKnob   ("SIZE",   apvts, reverbSize);
        reverb->addKnob   ("FBK",    apvts, reverbFb);
        reverb->addKnob   ("DAMP",   apvts, reverbDamp);
        reverb->addKnob   ("MOD HZ", apvts, reverbRate);
        reverb->addKnob   ("MOD DP", apvts, reverbDepth);
        reverb->addKnob   ("MIX",    apvts, reverbMix);
        reverb->addToggle ("FREEZE", apvts, reverbFreeze);
    }

    void paint (juce::Graphics& g) override {
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("DX7 ENGINE  >  CHORUS  >  DELAY  >  PHASER  >  REVERB  >  OUT",
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
        const int w = (b.getWidth() - gap * (n - 1)) / n;
        for (int i = 0; i < n; ++i)
            units[(size_t) i]->setBounds (b.getX() + i * (w + gap), b.getY(), w, b.getHeight());
    }

private:
    FxUnitPanel* addUnit (const juce::String& name,
                          juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& enableId)
    {
        auto u = std::make_unique<FxUnitPanel> (name, apvts, enableId);
        addAndMakeVisible (*u);
        auto* raw = u.get();
        units.push_back (std::move (u));
        return raw;
    }

    std::vector<std::unique_ptr<FxUnitPanel>> units;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
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
    juce::TooltipWindow   tooltips { this, 600 };   // shows knob descriptions

    vdx7ui::ContentPanel content;   // holds every visible child at design resolution

    juce::Label       title;
    juce::ComboBox    bankBox, progBox;
    juce::TextButton  prevBtn { "<" }, nextBtn { ">" }, initBtn { "INIT" }, fileBtn { "FILE" };
    juce::TextButton  fxBtn   { "FX" };   // top left, toggles the FX page
    juce::TextButton  romBtn  { "ROM" };  // firmware / factory-voice loading
    juce::Label       engineLabel;        // which engine is actually running
    std::unique_ptr<juce::FileChooser> chooser;
    static constexpr int kUserBankId = 100;
    juce::String openBankFileName_;   // filename of the currently-open user bank (save-protection)

    vdx7ui::LcdComponent  lcd;
    vdx7ui::AlgoComponent algo;
    std::vector<std::unique_ptr<vdx7ui::OperatorPanel>> ops;
    std::unique_ptr<vdx7ui::GlobalPanel> global;
    std::unique_ptr<vdx7ui::FxPanel>     fxPanel;
    bool showingFx_ = false;
    std::array<juce::TextButton,6> opSelect;   // operator selector sidebar (Operator 1..6)
    int selectedOp_ = 0;
    juce::MidiKeyboardComponent keyboard;

    std::vector<vdx7ui::ParamSlider*> allSliders;   // non-owning, for refresh
    // Binds each knob to its APVTS parameter (two-way, host-automation aware).
    // Declared last so it is destroyed first, before the sliders it references.
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> attachments;
    bool updatingUI = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VDX7AudioProcessorEditor)
};