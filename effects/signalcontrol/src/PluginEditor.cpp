/*
  ==============================================================================
    SignalControl - Drawable CV Generator
    Editor Implementation  v5

    Changes in v5:
      1. Bright green active highlight for Tempo Sync, Draw, Erase buttons.
      2. Rate knob extended to ±100 Hz with custom thirds mapping:
           first third  : -100 → -1 Hz  (fast reverse)
           middle third :   -1 → +1 Hz  (slow zone, fine detail)
           last third   :   +1 → +100 Hz (fast forward)
         Zero (standstill) sits exactly at the knob centre.
      3. Negative rate causes reverse playback in the processor.
      4. Transform row: Mirror X / Mirror Y buttons + Shift X / Shift Y sliders.
         Values shifted off one edge wrap back from the opposite edge.
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace Palette
{
    // === Backgrounds — dark lacquered metal ===
    const juce::Colour bg{ 0xFF1A1F1A };
    const juce::Colour bgPanel{ 0xFF252B25 };
    const juce::Colour bgStrip{ 0xFF1E231E };
    const juce::Colour canvasBg{ 0xFF0D110D };
    const juce::Colour canvasInner{ 0xFF111511 };

    // === Grid ===
    const juce::Colour gridMajor{ 0xFF1E2B1E };
    const juce::Colour gridMinor{ 0xFF182018 };
    const juce::Colour gridCenter{ 0xFF253025 };

    // === Curve — vivid electric green ===
    const juce::Colour curveStroke{ 0xFF3DFF5C };
    const juce::Colour curveFill{ 0x303DFF5C };
    const juce::Colour curveGlow{ 0x183DFF5C };

    // === Scan — bright lime ===
    const juce::Colour scanLine{ 0xFF8FFF00 };
    const juce::Colour scanGlow{ 0x2A8FFF00 };
    const juce::Colour scanDot{ 0xFFBFFF60 };

    // === Text ===
    const juce::Colour titleText{ 0xFFE0ECDB };
    const juce::Colour labelText{ 0xFF8BA885 };
    const juce::Colour dimText{ 0xFF4D6B49 };
    const juce::Colour accentText{ 0xFFD4A84A };

    // === Buttons — OFF state ===
    const juce::Colour btnOff{ 0xFF3C4A3C };
    const juce::Colour btnOffHi{ 0xFF475647 };
    const juce::Colour btnOffBdr{ 0xFF5A6E5A };
    const juce::Colour btnOffText{ 0xFF8BA885 };

    // === Buttons — ON / Active state  (bright green) ===
    // Body: a rich mid-green that reads clearly as "active"
    const juce::Colour btnOn{ 0xFF1B7A2C };
    const juce::Colour btnOnHi{ 0xFF22A83C };
    // Border: electric green — very visible
    const juce::Colour btnOnBdr{ 0xFF39FF5A };
    // Text: near-white green so it reads against the green body
    const juce::Colour btnOnText{ 0xFFCCFFD8 };

    // === Mode buttons active state (same bright green family) ===
    const juce::Colour modeActive{ 0xFF1B7A2C };
    const juce::Colour modeActiveBdr{ 0xFF39FF5A };
    const juce::Colour modeActiveText{ 0xFF39FF5A };

    // === Knob ===
    const juce::Colour knobTrack{ 0xFF2E3E2E };
    const juce::Colour knobFill{ 0xFF3DFF5C };
    const juce::Colour knobBody{ 0xFF3A4A3A };
    const juce::Colour knobSheen{ 0xFF4E5E4E };
    const juce::Colour knobBorder{ 0xFF5A7A5A };

    // === Border / separator ===
    const juce::Colour separator{ 0xFF2D3D2D };
    const juce::Colour borderGlow{ 0xFF3DFF5C };

    const juce::Colour silverHi{ 0xFF5A6E5A };
    const juce::Colour silverLo{ 0xFF232D23 };
}

//==============================================================================
// =====================  DrawCanvas  =========================================
//==============================================================================
DrawCanvas::DrawCanvas(SignalControlAudioProcessor& p) : processor(p)
{
    setOpaque(false);
}

void DrawCanvas::clearCurve()
{
    for (auto& v : processor.curveData)
        v.store(-1.0f);
    repaint();
}

float DrawCanvas::xToPhase(int x) const
{
    return juce::jlimit(0.0f, 1.0f, (float)x / (float)juce::jmax(1, getWidth() - 1));
}

float DrawCanvas::yToCV(int y) const
{
    return juce::jlimit(0.0f, 1.0f, 1.0f - (float)y / (float)juce::jmax(1, getHeight() - 1));
}

int DrawCanvas::phaseToX(float phase) const
{
    return (int)(phase * (float)(getWidth() - 1));
}

static inline int phaseToIndex(float phase)
{
    return juce::jlimit(0, CURVE_RESOLUTION - 1, (int)(phase * (float)CURVE_RESOLUTION));
}

void DrawCanvas::applyInterpolatedStroke(float phase1, float cv1, float phase2, float cv2)
{
    float p1 = juce::jlimit(0.0f, 1.0f, phase1);
    float p2 = juce::jlimit(0.0f, 1.0f, phase2);
    float v1 = juce::jlimit(0.0f, 1.0f, cv1);
    float v2 = juce::jlimit(0.0f, 1.0f, cv2);

    if (p1 > p2) { std::swap(p1, p2); std::swap(v1, v2); }

    int idx1 = phaseToIndex(p1);
    int idx2 = phaseToIndex(p2);

    processor.curveWriteInProgress.store(true);

    if (idx1 == idx2)
    {
        processor.curveData[idx1].store(v2);
    }
    else
    {
        for (int idx = idx1; idx <= idx2; ++idx)
        {
            float t = (float)(idx - idx1) / (float)(idx2 - idx1);
            float v = v1 + t * (v2 - v1);
            processor.curveData[idx].store(juce::jlimit(0.0f, 1.0f, v));
        }
    }

    processor.curveWriteInProgress.store(false);
}

void DrawCanvas::eraseStroke(float phase1, float phase2)
{
    float p1 = juce::jlimit(0.0f, 1.0f, phase1);
    float p2 = juce::jlimit(0.0f, 1.0f, phase2);
    if (p1 > p2) std::swap(p1, p2);

    int idx1 = phaseToIndex(p1);
    int idx2 = phaseToIndex(p2);

    processor.curveWriteInProgress.store(true);
    for (int idx = idx1; idx <= idx2; ++idx)
        processor.curveData[idx].store(-1.0f);
    processor.curveWriteInProgress.store(false);
}

void DrawCanvas::mouseDown(const juce::MouseEvent& e)
{
    rightClickErasing = e.mods.isRightButtonDown();

    float phase = xToPhase(e.x);
    float cv = yToCV(e.y);

    if (rightClickErasing || activeMode == DrawMode::Erase)
    {
        eraseStroke(phase, phase);
    }
    else
    {
        processor.curveWriteInProgress.store(true);
        processor.curveData[phaseToIndex(phase)].store(cv);
        processor.curveWriteInProgress.store(false);
    }

    lastPhase = phase;
    lastCV = cv;
    repaint();
}

void DrawCanvas::mouseDrag(const juce::MouseEvent& e)
{
    float phase = xToPhase(e.x);
    float cv = yToCV(e.y);

    bool prevValid = (lastPhase >= 0.0f);
    float fromPhase = prevValid ? lastPhase : phase;
    float fromCV = prevValid ? lastCV : cv;

    if (rightClickErasing || activeMode == DrawMode::Erase)
        eraseStroke(fromPhase, phase);
    else
        applyInterpolatedStroke(fromPhase, fromCV, phase, cv);

    lastPhase = phase;
    lastCV = cv;
    repaint();
}

void DrawCanvas::mouseUp(const juce::MouseEvent&)
{
    rightClickErasing = false;
    lastPhase = -1.0f;
    lastCV = -1.0f;
}

void DrawCanvas::paint(juce::Graphics& g)
{
    auto b = getLocalBounds();
    int  w = b.getWidth();
    int  h = b.getHeight();

    // Canvas deep black-green background
    {
        juce::ColourGradient grad(Palette::canvasInner, 0.0f, 0.0f,
            Palette::canvasBg, 0.0f, (float)h, false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(b.toFloat(), 5.0f);
    }

    g.setColour(juce::Colour(0xFF1A2A1A).withAlpha(0.6f));
    g.drawLine(1.0f, 1.0f, (float)w - 1.0f, 1.0f, 1.0f);
    g.drawLine(1.0f, 1.0f, 1.0f, (float)h - 1.0f, 1.0f);

    // Grid — major every 1/4, minor every 1/8
    for (int div = 1; div < 8; ++div)
    {
        int gx = juce::roundToInt((float)div / 8.0f * (float)w);
        g.setColour((div % 2 == 0) ? Palette::gridMajor : Palette::gridMinor);
        g.drawVerticalLine(gx, 2.0f, (float)h - 2.0f);
    }
    for (float level : { 0.25f, 0.5f, 0.75f })
    {
        int gy = juce::roundToInt((1.0f - level) * (float)h);
        g.setColour((level == 0.5f) ? Palette::gridCenter : Palette::gridMajor);
        g.drawHorizontalLine(gy, 2.0f, (float)w - 2.0f);
    }

    // ---- Curve ----
    // Outer glow pass
    {
        juce::Path glowPath;
        bool inSeg = false;
        for (int px = 0; px < w; ++px)
        {
            float phase = (float)px / (float)juce::jmax(1, w - 1);
            int   idx = juce::jlimit(0, CURVE_RESOLUTION - 1, (int)(phase * CURVE_RESOLUTION));
            float cv = processor.curveData[idx].load();
            bool  drawn = (cv >= 0.0f);
            if (drawn)
            {
                float py = (1.0f - cv) * (float)h;
                if (!inSeg) { glowPath.startNewSubPath((float)px, py); inSeg = true; }
                else          glowPath.lineTo((float)px, py);
            }
            else if (inSeg) { inSeg = false; }
        }
        g.setColour(Palette::curveGlow);
        g.strokePath(glowPath, juce::PathStrokeType(6.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Fill + stroke pass
    {
        juce::Path fillPath, strokePath;
        bool inSeg = false;

        for (int px = 0; px < w; ++px)
        {
            float phase = (float)px / (float)juce::jmax(1, w - 1);
            int   idx = juce::jlimit(0, CURVE_RESOLUTION - 1, (int)(phase * CURVE_RESOLUTION));
            float cv = processor.curveData[idx].load();
            bool  drawn = (cv >= 0.0f);

            if (drawn)
            {
                float py = (1.0f - cv) * (float)h;
                if (!inSeg)
                {
                    strokePath.startNewSubPath((float)px, py);
                    fillPath.startNewSubPath((float)px, (float)h);
                    fillPath.lineTo((float)px, py);
                    inSeg = true;
                }
                else
                {
                    strokePath.lineTo((float)px, py);
                    fillPath.lineTo((float)px, py);
                }
            }
            else
            {
                if (inSeg)
                {
                    fillPath.lineTo((float)px, (float)h);
                    fillPath.closeSubPath();
                    inSeg = false;
                }
            }
        }
        if (inSeg) { fillPath.lineTo((float)w, (float)h); fillPath.closeSubPath(); }

        g.setColour(Palette::curveFill);
        g.fillPath(fillPath);

        g.setColour(Palette::curveStroke);
        g.strokePath(strokePath, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- Scan line ----
    float scanX = currentScan * (float)w;

    {
        juce::ColourGradient glow(
            Palette::scanGlow, scanX, 0.0f,
            juce::Colours::transparentBlack, scanX + 20.0f, 0.0f, false);
        glow.addColour(0.0, Palette::scanGlow);
        g.setGradientFill(glow);
        g.fillRect(juce::Rectangle<float>(scanX - 10.0f, 0.0f, 20.0f, (float)h));
    }

    g.setColour(Palette::scanLine.withAlpha(0.55f));
    g.drawVerticalLine((int)scanX - 1, 0.0f, (float)h);
    g.setColour(Palette::scanLine);
    g.drawVerticalLine((int)scanX, 0.0f, (float)h);
    g.setColour(Palette::scanLine.withAlpha(0.55f));
    g.drawVerticalLine((int)scanX + 1, 0.0f, (float)h);

    if (currentCVVal >= 0.0f)
    {
        float dotY = (1.0f - currentCVVal) * (float)h;
        g.setColour(Palette::scanGlow.withAlpha(0.5f));
        g.fillEllipse(scanX - 9.0f, dotY - 9.0f, 18.0f, 18.0f);
        g.setColour(Palette::accentText);
        g.fillEllipse(scanX - 5.0f, dotY - 5.0f, 10.0f, 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.fillEllipse(scanX - 2.0f, dotY - 3.0f, 3.0f, 3.0f);
    }

    // Canvas border
    g.setColour(Palette::borderGlow.withAlpha(0.20f));
    g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 5.0f, 1.5f);
    g.setColour(Palette::separator);
    g.drawRoundedRectangle(b.toFloat().reduced(1.0f), 4.5f, 0.5f);
}

//==============================================================================
// =====================  LookAndFeel  ========================================
//==============================================================================
namespace
{
    static void drawGlossSheen(juce::Graphics& g, juce::Rectangle<float> b,
        juce::Colour sheenColour, float radius)
    {
        auto top = b.removeFromTop(b.getHeight() * 0.45f);
        juce::ColourGradient sheen(sheenColour.withAlpha(0.22f), top.getX(), top.getY(),
            juce::Colours::transparentBlack, top.getX(), top.getBottom(), false);
        g.setGradientFill(sheen);
        g.fillRoundedRectangle(top, radius);
    }

    struct SCLookAndFeel : public juce::LookAndFeel_V4
    {
        // ---- Rotary knob ---- glossy dark metal with green arc
        void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
            float sliderPos, float rotaryStartAngle,
            float rotaryEndAngle, juce::Slider&) override
        {
            float radius = (float)juce::jmin(w, h) * 0.5f - 5.0f;
            float cx = (float)x + (float)w * 0.5f;
            float cy = (float)y + (float)h * 0.5f;

            // Track
            juce::Path track;
            track.addCentredArc(cx, cy, radius, radius, 0.0f,
                rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(Palette::knobTrack);
            g.strokePath(track, juce::PathStrokeType(3.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Fill arc from center (0 Hz = sliderPos 0.5)
            float centerPos = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
            float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            juce::Path arc;
            float arcFrom = juce::jmin(centerPos, angle);
            float arcTo = juce::jmax(centerPos, angle);
            if (arcFrom < arcTo)
            {
                arc.addCentredArc(cx, cy, radius, radius, 0.0f, arcFrom, arcTo, true);
                g.setColour(Palette::knobFill);
                g.strokePath(arc, juce::PathStrokeType(3.5f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            float kr = radius * 0.60f;

            // Drop shadow
            g.setColour(juce::Colour(0x44000000));
            g.fillEllipse(cx - kr + 1.5f, cy - kr + 2.5f, kr * 2.0f, kr * 2.0f);

            // Body gradient
            {
                juce::ColourGradient body(Palette::knobSheen, cx - kr * 0.4f, cy - kr * 0.4f,
                    Palette::knobBody.darker(0.3f), cx + kr * 0.4f, cy + kr * 0.4f, false);
                g.setGradientFill(body);
                g.fillEllipse(cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
            }

            // Gloss highlight
            {
                juce::ColourGradient spec(juce::Colours::white.withAlpha(0.18f),
                    cx - kr * 0.3f, cy - kr * 0.5f,
                    juce::Colours::transparentBlack,
                    cx + kr * 0.2f, cy, false);
                g.setGradientFill(spec);
                g.fillEllipse(cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
            }

            g.setColour(Palette::knobBorder);
            g.drawEllipse(cx - kr, cy - kr, kr * 2.0f, kr * 2.0f, 1.2f);

            // Pointer
            float px = cx + (kr - 5.0f) * std::sin(angle);
            float py = cy - (kr - 5.0f) * std::cos(angle);
            g.setColour(Palette::knobFill);
            g.drawLine(cx, cy, px, py, 2.5f);

            g.setColour(Palette::knobFill.brighter(0.5f));
            g.fillEllipse(px - 2.0f, py - 2.0f, 4.0f, 4.0f);
        }

        // ---- Toggle button (Tempo Sync) — bright green when ON ----
        void drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
            bool hovered, bool down) override
        {
            bool on = btn.getToggleState();
            auto b = btn.getLocalBounds().toFloat().reduced(1.0f);
            float r = 5.0f;

            juce::Colour bodyTop, bodyBot;
            if (on)
            {
                bodyTop = Palette::btnOnHi;
                bodyBot = Palette::btnOn.darker(0.2f);
            }
            else
            {
                bodyTop = hovered ? Palette::btnOffHi : Palette::btnOff;
                bodyBot = Palette::btnOff.darker(0.2f);
            }
            if (down) { bodyTop = bodyTop.darker(0.15f); bodyBot = bodyBot.darker(0.15f); }

            juce::ColourGradient bodyGrad(bodyTop, b.getX(), b.getY(),
                bodyBot, b.getX(), b.getBottom(), false);
            g.setGradientFill(bodyGrad);
            g.fillRoundedRectangle(b, r);

            drawGlossSheen(g, b, juce::Colours::white, r);

            // Bright green border when on, subtle when off
            g.setColour(on ? Palette::btnOnBdr : Palette::btnOffBdr);
            g.drawRoundedRectangle(b, r, on ? 1.5f : 1.0f);

            g.setColour(on ? Palette::btnOnText : Palette::btnOffText);
            g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
            g.drawFittedText(btn.getButtonText(), btn.getLocalBounds(),
                juce::Justification::centred, 1);
        }

        // ---- Text button (Clear, Mirror, mode buttons) ----
        void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
            const juce::Colour& /*defaultColour*/, bool hovered, bool down) override
        {
            auto b = btn.getLocalBounds().toFloat().reduced(1.0f);
            float r = 5.0f;

            juce::Colour bodyTop = hovered ? Palette::btnOffHi : Palette::btnOff;
            juce::Colour bodyBot = Palette::btnOff.darker(0.25f);
            if (down) { bodyTop = bodyTop.darker(0.2f); bodyBot = bodyBot.darker(0.2f); }

            juce::ColourGradient bodyGrad(bodyTop, b.getX(), b.getY(),
                bodyBot, b.getX(), b.getBottom(), false);
            g.setGradientFill(bodyGrad);
            g.fillRoundedRectangle(b, r);

            drawGlossSheen(g, b, juce::Colours::white, r);

            g.setColour(Palette::btnOffBdr);
            g.drawRoundedRectangle(b, r, 1.0f);
        }

        void drawButtonText(juce::Graphics& g, juce::TextButton& btn,
            bool, bool) override
        {
            g.setColour(Palette::btnOffText);
            g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
            g.drawFittedText(btn.getButtonText(), btn.getLocalBounds(),
                juce::Justification::centred, 1);
        }

        // ---- Linear slider — green thumb, subtle track ----
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
            float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
            const juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            if (style != juce::Slider::LinearHorizontal &&
                style != juce::Slider::LinearVertical)
            {
                LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                    sliderPos, 0, 0, style, slider);
                return;
            }

            bool isH = (style == juce::Slider::LinearHorizontal);
            float cx = isH ? sliderPos : (float)x + (float)width * 0.5f;
            float cy = isH ? (float)y + (float)height * 0.5f : sliderPos;

            // Track
            float trackThick = 3.0f;
            if (isH)
            {
                float ty = cy - trackThick * 0.5f;
                juce::ColourGradient trk(Palette::knobTrack, (float)x, ty,
                    Palette::knobTrack.brighter(0.12f), (float)(x + width), ty, false);
                g.setGradientFill(trk);
                g.fillRoundedRectangle((float)x, ty, (float)width, trackThick, trackThick * 0.5f);
            }
            else
            {
                float tx = cx - trackThick * 0.5f;
                g.setColour(Palette::knobTrack);
                g.fillRoundedRectangle(tx, (float)y, trackThick, (float)height, trackThick * 0.5f);
            }

            // Thumb — green pill
            float thumbR = 7.0f;
            juce::ColourGradient thumbGrad(
                Palette::knobFill.brighter(0.3f), cx - thumbR * 0.4f, cy - thumbR * 0.4f,
                Palette::knobFill.darker(0.2f), cx + thumbR * 0.3f, cy + thumbR * 0.3f, false);
            g.setGradientFill(thumbGrad);
            g.fillEllipse(cx - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f);

            // Gloss
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.fillEllipse(cx - thumbR * 0.55f, cy - thumbR * 0.7f, thumbR * 0.9f, thumbR * 0.6f);

            // Border
            g.setColour(Palette::knobFill.brighter(0.5f));
            g.drawEllipse(cx - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f, 1.2f);
        }

        // ---- ComboBox ----
        void drawComboBox(juce::Graphics& g, int w, int h, bool,
            int, int, int, int, juce::ComboBox&) override
        {
            auto b = juce::Rectangle<float>(0.0f, 0.0f, (float)w, (float)h);
            juce::ColourGradient bodyGrad(Palette::btnOffHi, 0.0f, 0.0f,
                Palette::btnOff.darker(0.2f), 0.0f, (float)h, false);
            g.setGradientFill(bodyGrad);
            g.fillRoundedRectangle(b, 4.0f);

            drawGlossSheen(g, b, juce::Colours::white, 4.0f);

            g.setColour(Palette::btnOffBdr);
            g.drawRoundedRectangle(b, 4.0f, 1.0f);

            g.setColour(Palette::labelText);
            float ax = (float)w - 16.0f, ay = (float)h * 0.5f;
            juce::Path arrow;
            arrow.addTriangle(ax, ay - 3.5f, ax + 8.0f, ay - 3.5f, ax + 4.0f, ay + 3.5f);
            g.fillPath(arrow);
        }

        void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override
        {
            g.setColour(Palette::bgPanel);
            g.fillRect(0, 0, w, h);
            g.setColour(Palette::btnOffBdr);
            g.drawRect(0, 0, w, h, 1);
        }

        void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
            bool isSep, bool isActive, bool isHighlighted,
            bool, bool,
            const juce::String& text, const juce::String&,
            const juce::Drawable*, const juce::Colour*) override
        {
            if (isSep)
            {
                g.setColour(Palette::separator);
                g.fillRect(area.reduced(6, 0).withHeight(1));
                return;
            }
            if (isHighlighted && isActive)
            {
                g.setColour(Palette::modeActive);
                g.fillRect(area);
            }
            g.setColour((isHighlighted && isActive) ? Palette::btnOnText : Palette::btnOffText);
            g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
            g.drawText(text, area.reduced(8, 0), juce::Justification::centredLeft);
        }

        juce::Font getComboBoxFont(juce::ComboBox&) override
        {
            return juce::Font(juce::FontOptions().withHeight(12.0f));
        }

        juce::Label* createSliderTextBox(juce::Slider& s) override
        {
            auto* l = LookAndFeel_V4::createSliderTextBox(s);
            l->setColour(juce::Label::textColourId, Palette::labelText);
            l->setColour(juce::Label::backgroundColourId, Palette::bgPanel);
            l->setColour(juce::TextEditor::textColourId, Palette::labelText);
            l->setColour(juce::TextEditor::backgroundColourId, Palette::bgPanel);
            return l;
        }
    };

    static SCLookAndFeel sharedLAF;
}

//==============================================================================
// =====================  Editor  =============================================
//==============================================================================

// --------------------------------------------------------------------------
// Custom NormalisableRange for the rate knob.
//
// Linear knob position [0,1] → Hz value with thirds:
//   0.0 – 0.5  : maps -100 → 0   Hz  (reverse, slow approach)
//   0.5 – 1.0  : maps    0 → +100 Hz  (forward)
// WITHIN each half, the inner 1/3 of the HALF (i.e. centre 1/6 of total)
// covers -1..0 / 0..+1, giving fine resolution near zero.
//
// Simpler formulation — we use a symmetric piecewise log-like map:
//   position p ∈ [0,1], centre at 0.5
//   let u = (p - 0.5)*2   ∈ [-1,+1]  (signed distance from centre)
//   thirds (of each half):
//     |u| ∈ [0, 1/3]  → |Hz| ∈ [0, 1]    linear in this sub-range
//     |u| ∈ [1/3, 1]  → |Hz| ∈ [1, 100]  exponential in this sub-range
//   sign of Hz = sign of u
//
// This gives:
//   p=0.0  → -100 Hz
//   p=1/6  →  -1 Hz
//   p=0.5  →   0 Hz (centre)
//   p=5/6  →  +1 Hz
//   p=1.0  → +100 Hz
// --------------------------------------------------------------------------
static float rateProportionToValue(float proportion)
{
    // proportion [0,1] → Hz in [-100, +100]
    float u = (proportion - 0.5f) * 2.0f;  // [-1,+1]
    float au = std::abs(u);
    float hz;
    if (au <= (1.0f / 3.0f))
    {
        // Inner third of each half: 0..1/3 → 0..1 Hz  (linear)
        hz = au * 3.0f;
    }
    else
    {
        // Outer two-thirds: 1/3..1 → 1..100 Hz  (exponential)
        float t = (au - 1.0f / 3.0f) / (2.0f / 3.0f);  // [0,1]
        hz = std::pow(100.0f, t);   // 1..100
    }
    return (u >= 0.0f) ? hz : -hz;
}

static float rateValueToProportion(float hz)
{
    float ahz = std::abs(hz);
    float au;
    if (ahz <= 1.0f)
    {
        au = ahz / 3.0f;   // 0..1 Hz → 0..1/3
    }
    else
    {
        float t = std::log(ahz) / std::log(100.0f);  // 1..100 → 0..1
        au = (1.0f / 3.0f) + t * (2.0f / 3.0f);
    }
    float u = (hz >= 0.0f) ? au : -au;
    return u * 0.5f + 0.5f;
}

//------------------------------------------------------------------------------
// Applies the rate knob's *display* NormalisableRange.
//
// IMPORTANT: this must be called AFTER (re)creating rateAttach, because
// constructing a SliderAttachment resets the slider's range to the raw
// parameter range (-100..100 linear). Calling this beforehand would have
// no lasting effect — that was the bug that made the 100x toggle do nothing.
void SignalControlAudioProcessorEditor::applyRateRange(bool is100x)
{
    if (is100x)
    {
        // ±100 Hz with thirds log mapping
        juce::NormalisableRange<double> nr(
            -100.0, 100.0,
            [](double, double, double p) -> double { return (double)rateProportionToValue((float)p); },
            [](double, double, double v) -> double { return (double)rateValueToProportion((float)v); },
            nullptr
        );
        nr.interval = 0.0;
        rateKnob.setNormalisableRange(nr);
    }
    else
    {
        // ±1 Hz linear — clamp current value into range first
        double cur = juce::jlimit(-1.0, 1.0, rateKnob.getValue());
        juce::NormalisableRange<double> nr(-1.0, 1.0, 0.001);
        rateKnob.setNormalisableRange(nr);
        rateKnob.setValue(cur, juce::dontSendNotification);
    }
}

//------------------------------------------------------------------------------
SignalControlAudioProcessorEditor::SignalControlAudioProcessorEditor(SignalControlAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), drawCanvas(p)
{
    setLookAndFeel(&sharedLAF);
    setSize(680, 430);   // 2-row strip is shorter than previous 3-row
    setResizable(true, true);
    setResizeLimits(520, 360, 1400, 900);

    addAndMakeVisible(drawCanvas);

    // ---- Rate knob ----
    rateKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rateKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
    rateKnob.setNumDecimalPlacesToDisplay(3);
    rateKnob.setTextValueSuffix(" Hz");

    // Default range: ±1 Hz linear (no 100x).
    // The APVTS parameter stays ±100 Hz so full-range values are preserved.
    // NOTE: this is just a placeholder — creating the SliderAttachment below
    // will reset the slider's range to the parameter's raw range, so the
    // real ±1 Hz display range is (re)applied via applyRateRange() *after*
    // the attachment is constructed.
    {
        juce::NormalisableRange<double> nr(-1.0, 1.0, 0.001);
        rateKnob.setNormalisableRange(nr);
    }

    addAndMakeVisible(rateKnob);

    // ---- 100x rate multiplier toggle ----
    rate100xButton.setClickingTogglesState(true);
    rate100xButton.setToggleState(false, juce::dontSendNotification);
    rate100xButton.onClick = [this]()
        {
            bool is100x = rate100xButton.getToggleState();
            // Detach so we can change the range without fighting the attachment
            rateAttach.reset();

            // Re-create the attachment FIRST. Its constructor resets the
            // slider's NormalisableRange to the raw parameter range
            // (-100..100 linear), which would silently undo any custom
            // range we set beforehand. So apply our display range AFTER.
            rateAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                audioProcessor.apvts, "rate", rateKnob);

            applyRateRange(is100x);
        };
    addAndMakeVisible(rate100xButton);

    rateLabel.setText("RATE", juce::dontSendNotification);
    rateLabel.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
    rateLabel.setColour(juce::Label::textColourId, Palette::dimText);
    rateLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(rateLabel);

    // ---- Tempo sync ----
    addAndMakeVisible(tempoSyncButton);
    tempoSyncButton.onClick = [this]() { updateSyncVisibility(); repaint(); };

    // ---- Division combo ----
    buildSyncDivisionMenu();
    divisionLabel.setText("DIVISION", juce::dontSendNotification);
    divisionLabel.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
    divisionLabel.setColour(juce::Label::textColourId, Palette::dimText);
    divisionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(divisionCombo);
    addAndMakeVisible(divisionLabel);

    // ---- Clear ----
    addAndMakeVisible(clearButton);
    clearButton.onClick = [this]() { drawCanvas.clearCurve(); };

    // ---- Draw mode buttons — ToggleButton radio group ----
    modeDrawButton.setRadioGroupId(1, juce::dontSendNotification);
    modeEraseButton.setRadioGroupId(1, juce::dontSendNotification);
    modeDrawButton.setClickingTogglesState(true);
    modeEraseButton.setClickingTogglesState(true);
    addAndMakeVisible(modeDrawButton);
    addAndMakeVisible(modeEraseButton);
    modeDrawButton.onClick = [this]() { setDrawModeUI(DrawMode::Draw);  };
    modeEraseButton.onClick = [this]() { setDrawModeUI(DrawMode::Erase); };

    // ---- Transform row — Mirror ----
    addAndMakeVisible(mirrorXButton);
    addAndMakeVisible(mirrorYButton);
    mirrorXButton.onClick = [this]() { audioProcessor.mirrorCurveX(); };
    mirrorYButton.onClick = [this]() { audioProcessor.mirrorCurveY(); };

    // ---- Shift X slider (horizontal — wraps) ----
    shiftXSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    shiftXSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    shiftXSlider.setRange(-1.0, 1.0, 0.001);
    shiftXSlider.setValue(0.0, juce::dontSendNotification);
    shiftXSlider.setDoubleClickReturnValue(true, 0.0);
    shiftXSlider.onValueChange = [this]()
        {
            float cur = (float)shiftXSlider.getValue();
            float delta = cur - prevShiftX;
            if (std::abs(delta) > 0.0005f)
            {
                int slots = juce::roundToInt(delta * (float)CURVE_RESOLUTION);
                if (slots != 0)
                    audioProcessor.shiftCurveX(slots);
            }
            prevShiftX = cur;
        };
    shiftXSlider.onDragEnd = [this]()
        {
            shiftXSlider.setValue(0.0, juce::dontSendNotification);
            prevShiftX = 0.0f;
        };
    addAndMakeVisible(shiftXSlider);

    shiftXLabel.setText("Move L/R", juce::dontSendNotification);
    shiftXLabel.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
    shiftXLabel.setColour(juce::Label::textColourId, Palette::dimText);
    shiftXLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(shiftXLabel);

    // ---- Shift Y slider (vertical — wraps) ----
    shiftYSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    shiftYSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    shiftYSlider.setRange(-1.0, 1.0, 0.001);
    shiftYSlider.setValue(0.0, juce::dontSendNotification);
    shiftYSlider.setDoubleClickReturnValue(true, 0.0);
    shiftYSlider.onValueChange = [this]()
        {
            float cur = (float)shiftYSlider.getValue();
            float delta = cur - prevShiftY;
            if (std::abs(delta) > 0.0005f)
                audioProcessor.shiftCurveY(delta);
            prevShiftY = cur;
        };
    shiftYSlider.onDragEnd = [this]()
        {
            shiftYSlider.setValue(0.0, juce::dontSendNotification);
            prevShiftY = 0.0f;
        };
    addAndMakeVisible(shiftYSlider);

    shiftYLabel.setText("Move U/D", juce::dontSendNotification);
    shiftYLabel.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
    shiftYLabel.setColour(juce::Label::textColourId, Palette::dimText);
    shiftYLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(shiftYLabel);

    // ---- CV readout ----
    cvReadout.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    cvReadout.setColour(juce::Label::textColourId, Palette::accentText);
    cvReadout.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(cvReadout);

    // ---- APVTS Attachments ----
    rateAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "rate", rateKnob);
    // Creating the attachment above resets rateKnob's range to the raw
    // parameter range (-100..100 linear) — apply our ±1 Hz display range
    // now that the attachment exists (matches rate100xButton's initial
    // "off" state).
    applyRateRange(false);

    syncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "tempoSync", tempoSyncButton);
    divAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "syncDivision", divisionCombo);

    updateSyncVisibility();
    setDrawModeUI(DrawMode::Draw);
    startTimerHz(60);
}

SignalControlAudioProcessorEditor::~SignalControlAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//------------------------------------------------------------------------------
void SignalControlAudioProcessorEditor::buildSyncDivisionMenu()
{
    divisionCombo.clear(juce::dontSendNotification);
    for (int i = 1; i <= 8; ++i)
        divisionCombo.addItem(juce::String(i) + "/8", i);
    divisionCombo.setSelectedId(4, juce::dontSendNotification);
    divisionCombo.setColour(juce::ComboBox::textColourId, Palette::labelText);
    divisionCombo.setColour(juce::ComboBox::backgroundColourId, Palette::btnOff);
    divisionCombo.setColour(juce::ComboBox::outlineColourId, Palette::btnOffBdr);
}

void SignalControlAudioProcessorEditor::updateSyncVisibility()
{
    bool synced = tempoSyncButton.getToggleState();
    rateKnob.setVisible(!synced);
    rateLabel.setVisible(!synced);
    divisionCombo.setVisible(synced);
    divisionLabel.setVisible(synced);
    resized();
}

void SignalControlAudioProcessorEditor::setDrawModeUI(DrawMode m)
{
    drawCanvas.setDrawMode(m);
    modeDrawButton.setToggleState(m == DrawMode::Draw, juce::dontSendNotification);
    modeEraseButton.setToggleState(m == DrawMode::Erase, juce::dontSendNotification);
    repaint();
}

//------------------------------------------------------------------------------
void SignalControlAudioProcessorEditor::timerCallback()
{
    float scan = audioProcessor.scanPosition.load();
    float cv = audioProcessor.currentCV.load();
    drawCanvas.updateScanPosition(scan, cv);

    if (cv < 0.0f)
        cvReadout.setText("CV: --   GATE: 0", juce::dontSendNotification);
    else
        cvReadout.setText("CV: " + juce::String(cv, 3) + "   GATE: 1",
            juce::dontSendNotification);
}

//------------------------------------------------------------------------------
void SignalControlAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    int  w = bounds.getWidth();
    int  h = bounds.getHeight();

    // ---- Main dark metal background ----
    {
        juce::ColourGradient bgGrad(Palette::bg.brighter(0.06f), 0.0f, 0.0f,
            Palette::bg.darker(0.1f), 0.0f, (float)h, false);
        g.setGradientFill(bgGrad);
        g.fillAll();
    }

    // ---- Title bar ----
    {
        juce::Rectangle<int> titleBar(0, 0, w, 32);
        juce::ColourGradient hdr(Palette::bgPanel.brighter(0.1f), 0.0f, 0.0f,
            Palette::bgPanel.darker(0.1f), 0.0f, 32.0f, false);
        g.setGradientFill(hdr);
        g.fillRect(titleBar);

        juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.07f), 0.0f, 0.0f,
            juce::Colours::transparentBlack, 0.0f, 16.0f, false);
        g.setGradientFill(sheen);
        g.fillRect(juce::Rectangle<int>(0, 0, w, 16));

        g.setColour(Palette::separator);
        g.drawHorizontalLine(31, 0.0f, (float)w);
        g.setColour(Palette::borderGlow.withAlpha(0.15f));
        g.drawHorizontalLine(32, 0.0f, (float)w);

        g.setColour(Palette::titleText);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f).withStyle("Bold")));
        g.drawText("SIGNAL CONTROL", titleBar.reduced(14, 0), juce::Justification::centredLeft);

        g.setColour(Palette::dimText);
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
        g.drawText("CV GENERATOR  |  ch1: signal  |  ch2: gate",
            titleBar.reduced(14, 0), juce::Justification::centredRight);
    }

    // ---- Bottom control strip background (2 rows, rate knob spans both) ----
    {
        const int rowH = 48;
        const int stripH = rowH * 2 + 8;
        int stripY = h - stripH;

        juce::ColourGradient strip(Palette::bgStrip.brighter(0.05f), 0.0f, (float)stripY,
            Palette::bgStrip.darker(0.05f), 0.0f, (float)h, false);
        g.setGradientFill(strip);
        g.fillRect(0, stripY, w, stripH);

        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(0, stripY, w, 12);

        // Top border of strip
        g.setColour(Palette::separator);
        g.drawHorizontalLine(stripY, 0.0f, (float)w);
        g.setColour(Palette::borderGlow.withAlpha(0.10f));
        g.drawHorizontalLine(stripY + 1, 0.0f, (float)w);

        // Mid-divider between the two rows
        int midY = stripY + 8 + rowH;
        g.setColour(Palette::separator.withAlpha(0.5f));
        g.drawHorizontalLine(midY, 98.0f, (float)w - 8.0f);

    }

    // ---- Hint text on canvas ----
    g.setColour(Palette::dimText.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    auto canvasBounds = drawCanvas.getBounds();
    g.drawText("right-drag: erase",
        canvasBounds.reduced(6, 3).removeFromBottom(12),
        juce::Justification::centredRight);
}

//------------------------------------------------------------------------------
void SignalControlAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(32);   // title

    // ----------------------------------------------------------------
    //  Bottom control strip — 2 rows × 7 columns
    //
    //  Col layout (left to right):
    //    col 0-1 : Rate knob 2×2 block (spans both rows)
    //    Row 0:  col 2=Tempo Sync  col 3=Draw  col 4=Erase  col 5=Clear  col 6=CV readout
    //    Row 1:  col 2=100x        col 3=Mir X col 4=Mir Y  col 5=Mv L/R col 6=Mv U/D
    // ----------------------------------------------------------------

    const int rowH = 48;
    const int pad = 6;    // inner vertical padding per row
    const int stripH = rowH * 2 + 8;  // 8 px top padding
    const int hgap = 4;   // horizontal gap between cells

    auto stripArea = area.removeFromBottom(stripH);

    // Canvas takes the rest
    area.reduce(10, 6);
    drawCanvas.setBounds(area);

    // Strip top padding
    stripArea.removeFromTop(8);

    // Split into two rows
    auto row0 = stripArea.removeFromTop(rowH).reduced(0, pad);
    auto row1 = stripArea.reduced(0, pad);

    // ---- Rate knob block: 2 cols wide, spans both rows ----
    // We recombine row0 and row1 left portions for a unified rect.
    const int rateBlockW = 90;
    auto rateBlock0 = row0.removeFromLeft(rateBlockW);
    row0.removeFromLeft(hgap);
    auto rateBlock1 = row1.removeFromLeft(rateBlockW);
    row1.removeFromLeft(hgap);

    // Union of the two row rects gives the full 2-row tall block
    auto rateBlock = rateBlock0.getUnion(rateBlock1).reduced(4, 0);

    bool synced = tempoSyncButton.getToggleState();
    if (!synced)
    {
        auto kb = rateBlock;
        auto lbl = kb.removeFromTop(13);
        rateLabel.setBounds(lbl);
        rateKnob.setBounds(kb);
        rateKnob.setVisible(true);
        rateLabel.setVisible(true);
        divisionCombo.setVisible(false);
        divisionLabel.setVisible(false);
    }
    else
    {
        auto kb = rateBlock;
        auto lbl = kb.removeFromTop(13);
        divisionLabel.setBounds(lbl);
        divisionCombo.setBounds(kb.removeFromTop(28).withSizeKeepingCentre(rateBlockW - 12, 24));
        rateKnob.setVisible(false);
        rateLabel.setVisible(false);
        divisionCombo.setVisible(true);
        divisionLabel.setVisible(true);
    }

    // ---- Helper: distribute remaining row width evenly across N cells ----
    // Row 0 remaining: 5 cells — Tempo Sync | Draw | Erase | Clear | CV readout
    // Row 1 remaining: 5 cells — 100x | Mirror X | Mirror Y | Move L/R | Move U/D
    //
    // totalW0 == totalW1 (both rows had the same rate-block width removed),
    // so a SINGLE btnW is computed here and reused for every toggle/text
    // button in both rows. This keeps the 100x / Mirror X / Mirror Y row
    // grid-aligned under Tempo Sync / Draw / Erase / Clear, and guarantees
    // every button has identical width and height.
    const int totalRowW = row0.getWidth();
    // 4 buttons share 62% of the width; CV readout / sliders get the rest.
    int btnW = (int)((float)totalRowW * 0.62f / 4.0f) - hgap;
    if (btnW < 44) btnW = 44;

    // Row 0 — CV readout gets whatever's left over.
    {
        tempoSyncButton.setBounds(row0.removeFromLeft(btnW).reduced(0, 1));
        row0.removeFromLeft(hgap);
        modeDrawButton.setBounds(row0.removeFromLeft(btnW).reduced(0, 1));
        row0.removeFromLeft(hgap);
        modeEraseButton.setBounds(row0.removeFromLeft(btnW).reduced(0, 1));
        row0.removeFromLeft(hgap);
        clearButton.setBounds(row0.removeFromLeft(btnW).reduced(0, 1));
        row0.removeFromLeft(hgap);
        // CV readout gets whatever's left
        cvReadout.setBounds(row0);
    }

    // Row 1 — only 3 buttons (100x / Mirror X / Mirror Y), so the two
    // shift sliders share whatever width remains after them.
    {
        int totalW1 = row1.getWidth();
        int sliderW = (totalW1 - 3 * (btnW + hgap) - hgap) / 2 - hgap;
        if (sliderW < 60) sliderW = 60;

        rate100xButton.setBounds(row1.removeFromLeft(btnW).reduced(0, 1));
        row1.removeFromLeft(hgap);
        mirrorXButton.setBounds(row1.removeFromLeft(btnW).reduced(0, 1));
        row1.removeFromLeft(hgap);
        mirrorYButton.setBounds(row1.removeFromLeft(btnW).reduced(0, 1));
        row1.removeFromLeft(hgap);

        // Move L/R: label on top, slider below
        auto shiftXArea = row1.removeFromLeft(sliderW);
        shiftXLabel.setBounds(shiftXArea.removeFromTop(11));
        shiftXSlider.setBounds(shiftXArea);
        row1.removeFromLeft(hgap);

        // Move U/D: label on top, slider below
        shiftYLabel.setBounds(row1.removeFromTop(11));
        shiftYSlider.setBounds(row1);
    }
}