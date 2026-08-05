/*
  ==============================================================================
    PluginEditor.cpp  —  Vocode GUI implementation
  ==============================================================================
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"

// Colour constants (match the processor defaults)
namespace Col
{
    static const juce::Colour bg{ 0xff0b0d13 };
    static const juce::Colour panelBg{ 0xff101420 };
    static const juce::Colour surface{ 0xff161b28 };
    static const juce::Colour border{ 0xff1e2535 };
    static const juce::Colour gridLine{ 0xff182030 };
    static const juce::Colour carrier{ 0xff44aaff };
    static const juce::Colour modulator{ 0xffff8844 };
    static const juce::Colour output{ 0xff55eedd };
    static const juce::Colour bw{ 0xffffcc33 };
    static const juce::Colour text{ 0xff8aa0b0 };
    static const juce::Colour textDim{ 0xff3a4a58 };
    static const juce::Colour accent{ 0xff55eedd }; // same as output
}

//==============================================================================
// VocodeLookAndFeel
//==============================================================================
VocodeLookAndFeel::VocodeLookAndFeel()
{
    // Rotary slider colours
    setColour(juce::Slider::rotarySliderOutlineColourId, Col::border);
    setColour(juce::Slider::thumbColourId, Col::accent);
    setColour(juce::Slider::textBoxTextColourId, Col::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::textBoxHighlightColourId, Col::accent.withAlpha(0.35f));

    // Label
    setColour(juce::Label::textColourId, Col::textDim);
    setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));

    // ComboBox
    setColour(juce::ComboBox::backgroundColourId, Col::surface);
    setColour(juce::ComboBox::textColourId, Col::text);
    setColour(juce::ComboBox::outlineColourId, Col::border);
    setColour(juce::ComboBox::arrowColourId, Col::accent);
    setColour(juce::ComboBox::focusedOutlineColourId, Col::accent);

    // PopupMenu
    setColour(juce::PopupMenu::backgroundColourId, Col::surface);
    setColour(juce::PopupMenu::textColourId, Col::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Col::accent.withAlpha(0.18f));
    setColour(juce::PopupMenu::highlightedTextColourId, Col::accent);

    // Toggle button
    setColour(juce::ToggleButton::textColourId, Col::text);
    setColour(juce::ToggleButton::tickColourId, Col::accent);
    setColour(juce::ToggleButton::tickDisabledColourId, Col::border);
}

juce::Font VocodeLookAndFeel::getLabelFont(juce::Label& l)
{
    return juce::Font(juce::FontOptions(l.getFont().getHeight()));
}

void VocodeLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float pos,
    float startA, float endA,
    juce::Slider& /*s*/)
{
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float r = juce::jmin(w, h) * 0.39f;
    const float ang = startA + pos * (endA - startA);

    // --- Track arc ---
    {
        juce::Path track;
        track.addCentredArc(cx, cy, r, r, 0.f, startA, endA, true);
        g.setColour(Col::border);
        g.strokePath(track, juce::PathStrokeType(2.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- Filled value arc ---
    {
        juce::Path fill;
        fill.addCentredArc(cx, cy, r, r, 0.f, startA, ang, true);

        // Teal gradient along the arc (fake it with a linear gradient)
        juce::ColourGradient grad(Col::accent.withAlpha(0.6f), cx - r, cy,
            Col::accent, cx + r, cy, false);
        g.setGradientFill(grad);
        g.strokePath(fill, juce::PathStrokeType(2.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- Knob body ---
    const float kr = r * 0.56f;
    {
        // Radial gradient: slightly lighter at top-left
        juce::ColourGradient grad(juce::Colour(0xff1c2130), cx - kr * 0.4f, cy - kr * 0.5f,
            juce::Colour(0xff090b10), cx + kr * 0.4f, cy + kr * 0.5f, true);
        g.setGradientFill(grad);
        g.fillEllipse(cx - kr, cy - kr, kr * 2.f, kr * 2.f);
        g.setColour(Col::border);
        g.drawEllipse(cx - kr, cy - kr, kr * 2.f, kr * 2.f, 1.0f);
    }

    // --- Pointer dot ---
    const float pd = r * 0.80f;
    const float px = cx + pd * std::sin(ang);
    const float py = cy - pd * std::cos(ang);
    // Line from knob centre to dot
    g.setColour(Col::accent.withAlpha(0.7f));
    g.drawLine(cx + std::sin(ang) * kr * 0.25f,
        cy - std::cos(ang) * kr * 0.25f,
        px, py, 1.5f);
    // Dot
    g.setColour(Col::accent);
    g.fillEllipse(px - 3.f, py - 3.f, 6.f, 6.f);
    // Glow halo
    g.setColour(Col::accent.withAlpha(0.25f));
    g.fillEllipse(px - 6.f, py - 6.f, 12.f, 12.f);
}

void VocodeLookAndFeel::drawToggleButton(juce::Graphics& g,
    juce::ToggleButton& b,
    bool highlighted, bool /*down*/)
{
    const float W = (float)b.getWidth();
    const float H = (float)b.getHeight();
    const bool  on = b.getToggleState();
    const float cr = H * 0.28f;          // corner radius

    juce::Rectangle<float> bounds(1.5f, 1.5f, W - 3.f, H - 3.f);

    // Background fill
    g.setColour(on ? Col::accent.withAlpha(0.13f)
        : juce::Colour(0xff0e1018));
    g.fillRoundedRectangle(bounds, cr);

    // Border
    g.setColour((on ? Col::accent : Col::border).withAlpha(highlighted ? 1.f : 0.75f));
    g.drawRoundedRectangle(bounds, cr, 1.3f);

    // LED indicator
    const float dotR = (H - 10.f) * 0.33f;
    const float dotX = 9.f + dotR;
    const float dotY = H * 0.5f;
    if (on)
    {
        g.setColour(Col::accent.withAlpha(0.3f));
        g.fillEllipse(dotX - dotR * 1.9f, dotY - dotR * 1.9f, dotR * 3.8f, dotR * 3.8f);
        g.setColour(Col::accent);
        g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.f, dotR * 2.f);
    }
    else
    {
        g.setColour(Col::border);
        g.drawEllipse(dotX - dotR, dotY - dotR, dotR * 2.f, dotR * 2.f, 1.0f);
    }

    // Button text
    g.setColour(on ? Col::accent : Col::text);
    g.setFont(juce::FontOptions(10.f));
    g.drawText(b.getButtonText(),
        juce::Rectangle<float>(dotX + dotR + 8.f, 0.f,
            W - dotX - dotR - 12.f, H),
        juce::Justification::centredLeft);
}

//==============================================================================
// SpectrumBandEditor
//==============================================================================
SpectrumBandEditor::SpectrumBandEditor(VocodeAudioProcessor& p)
    : proc(p)
{
    bwCurve.fill(0.5f);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(30);
}

SpectrumBandEditor::~SpectrumBandEditor()
{
    stopTimer();
}

void SpectrumBandEditor::timerCallback()
{
    const int nb = proc.getNumBins();
    if (nb > 0 && nb <= (int)carrierMag.size())
        proc.getDisplayMagnitudes(carrierMag.data(), modMag.data(), vocodeMag.data(), nb);

    proc.getBandwidthCurve(bwCurve);
    repaint();
}

// ---- Band / pixel helpers ------------------------------------------------
int SpectrumBandEditor::xToBand(float x) const noexcept
{
    const int nb = juce::jmax(1, proc.getNumBands());
    return juce::jlimit(0, nb - 1, (int)(x / getWidth() * nb));
}
float SpectrumBandEditor::bandToX(int band) const noexcept
{
    const int nb = juce::jmax(1, proc.getNumBands());
    return ((float)band + 0.5f) / (float)nb * (float)getWidth();
}
float SpectrumBandEditor::yToVal(float y) const noexcept
{
    // top of bw strip = 1.0 (widest)  /  bottom = 0.0 (narrowest)
    const float t = bwStripTop();
    const float b = bwStripBottom();
    return 1.f - juce::jlimit(0.f, 1.f, (y - t) / (b - t));
}
float SpectrumBandEditor::valToY(float val) const noexcept
{
    return bwStripTop() + (1.f - val) * (bwStripBottom() - bwStripTop());
}

// ---- Spectrum path builder -----------------------------------------------
juce::Path SpectrumBandEditor::buildSpectrumPath(const float* mags, int numBins,
    double sampleRate,
    float w, float h,
    float dBmin, float dBmax) const
{
    juce::Path p;
    const float fMin = 20.f;
    const float fMax = (float)(sampleRate * 0.5);
    const float logMin = std::log(fMin);
    const float logRange = std::log(fMax) - logMin;

    bool started = false;
    for (int px = 0; px < (int)w; px += 2)
    {
        const float xn = (float)px / w;
        const float freq = std::exp(logMin + xn * logRange);
        // Convert frequency to bin index
        // bin k → freq  k * sr / fftSize;   fftSize = (numBins-1)*2
        const float binF = freq * (float)((numBins - 1) * 2) / (float)sampleRate;
        const int   b0 = juce::jlimit(0, numBins - 2, (int)binF);
        const float t = binF - (float)b0;
        const float mag = mags[b0] * (1.f - t) + mags[b0 + 1] * t;
        const float dB = 20.f * std::log10(mag + 1e-9f);
        const float yn = juce::jlimit(0.f, 1.f, (dB - dBmax) / (dBmin - dBmax));
        const float yPx = yn * h;

        if (!started) { p.startNewSubPath((float)px, yPx); started = true; }
        else          p.lineTo((float)px, yPx);
    }
    if (started)
    {
        p.lineTo(w, h);
        p.lineTo(0.f, h);
        p.closeSubPath();
    }
    return p;
}

// ---- Mouse interaction ---------------------------------------------------
void SpectrumBandEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        proc.resetBandwidthCurve();
        return;
    }
    if (!isInBwStrip((float)e.y)) return;

    dragging = true;
    dragBandA = dragBandB = xToBand((float)e.x);
    dragValA = dragValB = yToVal((float)e.y);
    proc.setBandwidthCurveRange(dragBandA, dragBandB, dragValA, dragValB);
}

void SpectrumBandEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging) return;
    dragBandB = xToBand((float)e.x);
    dragValB = yToVal(juce::jlimit(bwStripTop(), bwStripBottom(), (float)e.y));
    proc.setBandwidthCurveRange(dragBandA, dragBandB, dragValA, dragValB);
    // Update start to last position for continuous painting
    dragBandA = dragBandB;
    dragValA = dragValB;
}

void SpectrumBandEditor::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
}

// ---- Paint ---------------------------------------------------------------
void SpectrumBandEditor::paint(juce::Graphics& g)
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float specH = bwStripTop() - 4.f;   // height of the spectrum zone
    const float bwTop = bwStripTop();
    const float bwBot = bwStripBottom();
    const float bwH = bwBot - bwTop;

    // =========================================================
    //  1. Overall background
    // =========================================================
    g.fillAll(Col::bg);

    // =========================================================
    //  2. Spectrum zone grid
    // =========================================================
    {
        const double sr = proc.getSampleRate_();
        const float  fMin = 20.f;
        const float  fMax = (sr > 0.0) ? (float)(sr * 0.5) : 22050.f;
        const float  logMin = std::log(fMin);
        const float  logRange = std::log(fMax) - logMin;

        // Vertical frequency gridlines
        const float freqLines[] = { 50,100,200,500,1000,2000,5000,10000,20000 };
        const char* freqLabels[] = { "50","100","200","500","1k","2k","5k","10k","20k" };

        for (int i = 0; i < 9; ++i)
        {
            const float f = freqLines[i];
            if (f < fMin || f > fMax) continue;
            const float xf = (std::log(f) - logMin) / logRange * W;

            g.setColour(Col::gridLine);
            g.drawVerticalLine((int)xf, 0.f, specH);

            g.setColour(Col::textDim);
            g.setFont(juce::FontOptions(8.5f));
            g.drawText(freqLabels[i],
                (int)xf - 14, (int)specH - 15, 28, 12,
                juce::Justification::centred);
        }

        // Horizontal dB gridlines
        const float dBgrid[] = { -12.f, -24.f, -36.f, -48.f, -60.f };
        const float dBmin = -78.f, dBmax = 3.f;

        for (float db : dBgrid)
        {
            const float yn = (db - dBmax) / (dBmin - dBmax) * specH;
            g.setColour(Col::gridLine);
            g.drawHorizontalLine((int)yn, 0.f, W);

            // dB label on left edge
            g.setColour(Col::textDim);
            g.setFont(juce::FontOptions(7.5f));
            g.drawText(juce::String((int)db) + "dB",
                4, (int)yn - 9, 36, 10,
                juce::Justification::centredLeft);
        }
    }

    // =========================================================
    //  3. Spectrum fills
    // =========================================================
    const int    numBins = proc.getNumBins();
    const double sr = proc.getSampleRate_();

    if (numBins > 1 && sr > 0.0)
    {
        // Helper — build just the top line (no closed bottom) for stroking
        auto buildLine = [&](const float* mags) -> juce::Path
            {
                juce::Path lp;
                const float fMin = 20.f;
                const float fMax = (float)(sr * 0.5);
                const float logMin = std::log(fMin);
                const float logRange = std::log(fMax) - logMin;
                const float dBmin = -78.f, dBmax = 3.f;
                bool started = false;
                for (int px = 0; px < (int)W; px += 2)
                {
                    const float xn = (float)px / W;
                    const float freq = std::exp(logMin + xn * logRange);
                    const float binF = freq * (float)((numBins - 1) * 2) / (float)sr;
                    const int   b0 = juce::jlimit(0, numBins - 2, (int)binF);
                    const float t = binF - (float)b0;
                    const float mag = mags[b0] * (1.f - t) + mags[b0 + 1] * t;
                    const float dB = 20.f * std::log10(mag + 1e-9f);
                    const float yn = juce::jlimit(0.f, 1.f, (dB - dBmax) / (dBmin - dBmax));
                    const float yPx = yn * specH;
                    if (!started) { lp.startNewSubPath((float)px, yPx); started = true; }
                    else lp.lineTo((float)px, yPx);
                }
                return lp;
            };

        // --- Carrier (blue, bottom layer) ---
        {
            juce::Path fill = buildSpectrumPath(carrierMag.data(), numBins, sr, W, specH);
            g.setColour(Col::carrier.withAlpha(0.09f));
            g.fillPath(fill);

            juce::Path line = buildLine(carrierMag.data());
            g.setColour(Col::carrier.withAlpha(0.50f));
            g.strokePath(line, juce::PathStrokeType(1.1f));
        }

        // --- Modulator (orange, middle layer) ---
        {
            juce::Path fill = buildSpectrumPath(modMag.data(), numBins, sr, W, specH);
            g.setColour(Col::modulator.withAlpha(0.08f));
            g.fillPath(fill);

            juce::Path line = buildLine(modMag.data());
            g.setColour(Col::modulator.withAlpha(0.40f));
            g.strokePath(line, juce::PathStrokeType(1.1f));
        }

        // --- Vocoded output (teal, top layer – brightest) ---
        {
            juce::Path fill = buildSpectrumPath(vocodeMag.data(), numBins, sr, W, specH);
            // Gradient fill: brighter at top
            juce::ColourGradient grad(Col::output.withAlpha(0.22f), 0.f, 0.f,
                Col::output.withAlpha(0.04f), 0.f, specH, false);
            g.setGradientFill(grad);
            g.fillPath(fill);

            juce::Path line = buildLine(vocodeMag.data());
            g.setColour(Col::output.withAlpha(0.80f));
            g.strokePath(line, juce::PathStrokeType(1.6f));
        }
    }

    // =========================================================
    //  4. Band dividers (drawn when band count is not too large)
    // =========================================================
    {
        const int nb = proc.getNumBands();
        if (nb > 1 && nb <= 64)
        {
            g.setColour(juce::Colour(0xff1e2a38).withAlpha(0.55f));
            for (int b = 1; b < nb; ++b)
            {
                const float bx = (float)b / (float)nb * W;
                g.drawVerticalLine((int)bx, 0.f, specH);
            }
        }
    }

    // =========================================================
    //  5. Legend (spectrum zone — top-right corner)
    // =========================================================
    {
        struct Legend { juce::Colour col; const char* name; };
        const Legend items[] = {
            { Col::carrier,   "CARRIER"   },
            { Col::modulator, "MODULATOR" },
            { Col::output,    "OUTPUT"    },
        };
        const float lx = W - 98.f;
        const float ly = 8.f;

        for (int i = 0; i < 3; ++i)
        {
            const float iy = ly + i * 16.f;
            // Small colour swatch
            g.setColour(items[i].col.withAlpha(0.8f));
            g.fillRoundedRectangle(lx, iy + 1.f, 8.f, 8.f, 1.5f);
            // Label
            g.setColour(items[i].col.withAlpha(0.55f));
            g.setFont(juce::FontOptions(8.5f));
            g.drawText(items[i].name,
                (int)(lx + 12), (int)iy, 80, 10,
                juce::Justification::centredLeft);
        }
    }

    // =========================================================
    //  6. Separator between spectrum and bandwidth strip
    // =========================================================
    g.setColour(Col::border);
    g.fillRect(0.f, bwTop - 4.f, W, 4.f);

    // =========================================================
    //  7. Bandwidth strip background
    // =========================================================
    {
        juce::ColourGradient grad(juce::Colour(0xff0d1018), 0.f, bwTop,
            Col::bg, 0.f, bwBot, false);
        g.setGradientFill(grad);
        g.fillRect(0.f, bwTop, W, bwH);
    }

    // Mid-line + top/bottom bounds in BW strip
    g.setColour(Col::border.withAlpha(0.5f));
    g.drawHorizontalLine((int)((bwTop + bwBot) * 0.5f), 0.f, W);
    g.setColour(Col::border);
    g.drawHorizontalLine((int)bwTop, 0.f, W);

    // =========================================================
    //  8. Bandwidth curve
    // =========================================================
    {
        const int nb = proc.getNumBands();
        if (nb > 0)
        {
            // --- Filled area under curve ---
            juce::Path curveFill;
            curveFill.startNewSubPath(0.f, bwBot);
            for (int b = 0; b < nb; ++b)
                curveFill.lineTo(bandToX(b), valToY(bwCurve[b]));
            curveFill.lineTo(W, bwBot);
            curveFill.closeSubPath();

            juce::ColourGradient fillGrad(Col::bw.withAlpha(0.22f), 0.f, bwTop,
                Col::bw.withAlpha(0.04f), 0.f, bwBot, false);
            g.setGradientFill(fillGrad);
            g.fillPath(curveFill);

            // --- Curve line ---
            juce::Path curveLine;
            bool lineStarted = false;
            for (int b = 0; b < nb; ++b)
            {
                const float cx2 = bandToX(b);
                const float cy2 = valToY(bwCurve[b]);
                if (!lineStarted) { curveLine.startNewSubPath(cx2, cy2); lineStarted = true; }
                else              curveLine.lineTo(cx2, cy2);
            }
            g.setColour(Col::bw.withAlpha(0.88f));
            g.strokePath(curveLine, juce::PathStrokeType(1.8f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // --- Dots (only when band count is manageable) ---
            if (nb <= 96)
            {
                for (int b = 0; b < nb; ++b)
                {
                    const float cx2 = bandToX(b);
                    const float cy2 = valToY(bwCurve[b]);
                    g.setColour(Col::bw.withAlpha(0.5f));
                    g.fillEllipse(cx2 - 4.f, cy2 - 4.f, 8.f, 8.f);
                    g.setColour(Col::bw);
                    g.fillEllipse(cx2 - 2.f, cy2 - 2.f, 4.f, 4.f);
                }
            }
        }
    }

    // =========================================================
    //  9. Bandwidth strip labels / hints
    // =========================================================
    g.setFont(juce::FontOptions(8.5f));
    g.setColour(Col::bw.withAlpha(0.38f));
    g.drawText("BANDWIDTH",
        6, (int)bwTop + 4, 80, 12, juce::Justification::centredLeft);

    g.setColour(Col::textDim.withAlpha(0.55f));
    g.drawText("drag to draw  ·  right-click to reset",
        0, (int)bwBot - 14, (int)W - 6, 12,
        juce::Justification::centredRight);

    // Wide / Narrow labels on the right edge of the strip
    g.setColour(Col::textDim);
    g.drawText("WIDE",
        (int)W - 36, (int)bwTop + 4, 34, 10,
        juce::Justification::centredRight);
    g.drawText("NARROW",
        (int)W - 52, (int)bwBot - 14, 50, 10,
        juce::Justification::centredRight);
}

//==============================================================================
// VocodeAudioProcessorEditor
//==============================================================================
VocodeAudioProcessorEditor::VocodeAudioProcessorEditor(VocodeAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    spectrumEditor(p)
{
    setLookAndFeel(&laf);

    // ---- Helper to configure a rotary knob + its label ----
    auto initKnob = [&](juce::Slider& s, juce::Label& l, const juce::String& name)
        {
            s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 14);
            addAndMakeVisible(s);

            l.setText(name, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
            l.setFont(juce::Font(juce::FontOptions(9.f)));
            l.setColour(juce::Label::textColourId, Col::textDim);
            addAndMakeVisible(l);
        };

    initKnob(slAttack, lbAttack, "ATTACK");
    initKnob(slRelease, lbRelease, "RELEASE");
    initKnob(slMorph, lbMorph, "MORPH");
    initKnob(slDryWet, lbDryWet, "DRY / WET");
    initKnob(slBands, lbBands, "BANDS");

    // ---- APVTS slider attachments ----
    attAttack = std::make_unique<SlAtt>(p.apvts, "attack", slAttack);
    attRelease = std::make_unique<SlAtt>(p.apvts, "release", slRelease);
    attMorph = std::make_unique<SlAtt>(p.apvts, "morph", slMorph);
    attDryWet = std::make_unique<SlAtt>(p.apvts, "dryWet", slDryWet);
    attBands = std::make_unique<SlAtt>(p.apvts, "numBands", slBands);

    // ---- FFT size combo ----
    cbFFT.addItem("1024", 1);
    cbFFT.addItem("2048", 2);
    cbFFT.addItem("4096", 3);
    cbFFT.addItem("8192", 4);
    cbFFT.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(cbFFT);

    lbFFT.setText("FFT SIZE", juce::dontSendNotification);
    lbFFT.setJustificationType(juce::Justification::centred);
    lbFFT.setFont(juce::Font(juce::FontOptions(9.f)));
    lbFFT.setColour(juce::Label::textColourId, Col::textDim);
    addAndMakeVisible(lbFFT);

    attFFT = std::make_unique<CbAtt>(p.apvts, "fftSize", cbFFT);

    // ---- Self-vocode toggle ----
    btnSelf.setButtonText("SELF-VOCODE");
    addAndMakeVisible(btnSelf);
    attSelf = std::make_unique<BtnAtt>(p.apvts, "selfVocode", btnSelf);

    // ---- Spectrum visualiser ----
    addAndMakeVisible(spectrumEditor);

    setSize(840, 520);
    setResizable(false, false);
}

VocodeAudioProcessorEditor::~VocodeAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

//------------------------------------------------------------------------------
void VocodeAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float W = (float)getWidth();

    // ---- Control panel background ----
    {
        juce::ColourGradient grad(juce::Colour(0xff111825), 0.f, 0.f,
            juce::Colour(0xff0c0f18), 0.f, (float)kControlH, false);
        g.setGradientFill(grad);
        g.fillRect(0, 0, getWidth(), kControlH);
    }

    // ---- Top accent line (carrier→output gradient) ----
    {
        juce::ColourGradient grad(Col::carrier.withAlpha(0.55f), 0.f, 0.f,
            Col::output.withAlpha(0.55f), W, 0.f, false);
        g.setGradientFill(grad);
        g.fillRect(0.f, 0.f, W, 2.f);
    }

    // ---- Divider between controls and spectrum ----
    g.setColour(Col::border);
    g.fillRect(0, kControlH - 1, getWidth(), 1);

    // ---- Vertical separator before combo / toggle section ----
    g.setColour(Col::border.withAlpha(0.5f));
    g.fillRect(530, 14, 1, kControlH - 28);

    // ---- Plugin name (top-right) ----
    {
        const int nx = getWidth() - 152;

        // Subtle glow behind the name
        g.setColour(Col::output.withAlpha(0.04f));
        g.fillRoundedRectangle((float)nx - 4.f, 12.f, 148.f, 36.f, 4.f);

        g.setFont(juce::FontOptions("Courier New", 21.f, juce::Font::bold));
        g.setColour(juce::Colour(0xffd0e0e8));
        g.drawText("VOCODE", nx, 14, 140, 28, juce::Justification::centredRight);

        g.setFont(juce::FontOptions(8.f));
        g.setColour(Col::textDim);
        g.drawText("FFT VOCODER", nx, 46, 140, 10, juce::Justification::centredRight);

        // Small teal decorative line under name
        g.setColour(Col::output.withAlpha(0.35f));
        g.fillRect((float)(nx + 80), 42.f, 60.f, 1.f);
    }
}

//------------------------------------------------------------------------------
void VocodeAudioProcessorEditor::resized()
{
    // ---- Five rotary knobs (left section) ----
    const int kW = 86;   // knob slot width
    const int kH = 90;   // knob component height
    const int lH = 14;   // label height
    const int kY = 12;   // top margin
    const int lY = kY + kH + 2;
    const int gap = 6;

    juce::Slider* knobs[] = { &slAttack, &slRelease, &slMorph, &slDryWet, &slBands };
    juce::Label* labels[] = { &lbAttack, &lbRelease, &lbMorph, &lbDryWet, &lbBands };
    constexpr int  nKnobs = 5;

    int kx = 12;
    for (int i = 0; i < nKnobs; ++i)
    {
        knobs[i]->setBounds(kx, kY, kW, kH);
        labels[i]->setBounds(kx, lY, kW, lH);
        kx += kW + gap;
    }

    // ---- Right section: FFT size + self-vocode ----
    const int rx = 544;

    lbFFT.setBounds(rx, 16, 138, 14);
    cbFFT.setBounds(rx, 33, 138, 26);
    btnSelf.setBounds(rx, 78, 138, 30);

    // ---- Spectrum editor: full width below controls ----
    spectrumEditor.setBounds(0, kControlH, getWidth(), getHeight() - kControlH);
}