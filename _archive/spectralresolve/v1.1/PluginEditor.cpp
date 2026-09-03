#include "PluginEditor.h"

//==============================================================================
static const struct GridEntry { float freq; const char* label; bool major; } GRID[] =
{
    {   20.000f, "20 Hz",  true  },
    {   32.703f, "C1",     false },
    {   65.406f, "C2",     false },
    {  100.000f, "100 Hz", true  },
    {  130.813f, "C3",     false },
    {  261.626f, "C4",     false },
    {  523.251f, "C5",     false },
    { 1000.000f, "1 kHz",  true  },
    { 1046.502f, "C6",     false },
    { 2093.005f, "C7",     false },
    { 4186.009f, "C8",     false },
    { 5000.000f, "5 kHz",  true  },
    { 8372.018f, "C9",     false },
    {10000.000f, "10 kHz", true  },
    {16744.036f, "C10",    false },
    {20000.000f, "20 kHz", true  },
};

static juce::String colourToHex(juce::Colour c)
{
    return juce::String::toHexString(
        (int)((c.getRed() << 16) | (c.getGreen() << 8) | c.getBlue()))
        .paddedLeft('0', 6).toUpperCase();
}

//==============================================================================
SpectralResolverEditor::SpectralResolverEditor(SpectralResolverProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&laf);
    setResizable(true, true);
    setResizeLimits(700, 460, 4000, 4000);   // max expanded; sidebar-hidden allows extreme narrow/tall
    setSize(1100, 680);

    // Initialise temporal interpolation buffers (all rows silent / zero coverage)
    prevMainPixMags.assign(IMG_H, -200.f);
    prevScPixMags.assign(IMG_H, -200.f);
    prevMainPixAlphas.assign(IMG_H, 0.f);
    prevScPixAlphas.assign(IMG_H, 0.f);

    // ── Label styling ────────────────────────────────────────────────────────
    auto styleLabel = [&](juce::Label& l, const char* text)
        {
            l.setText(text, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
            l.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
            l.setFont(juce::Font(10.f));
            addAndMakeVisible(l);
        };

    // ── Rotary slider styling ────────────────────────────────────────────────
    auto styleSlider = [&](juce::Slider& s, juce::Label& l, const char* name)
        {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 14);
            s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff111111));
            s.setColour(juce::Slider::thumbColourId, juce::Colour(0xfffcffa4));
            addAndMakeVisible(s);
            styleLabel(l, name);
        };

    // ── ComboBox styling ─────────────────────────────────────────────────────
    auto styleCombo = [&](juce::ComboBox& c, juce::Label& l, const char* name)
        {
            c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111111));
            c.setColour(juce::ComboBox::textColourId, juce::Colour(0xfffcffa4));
            c.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2a2a2a));
            c.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xfffcffa4));
            addAndMakeVisible(c);
            styleLabel(l, name);
        };

    // ── Hex input styling ────────────────────────────────────────────────────
    auto styleHex = [&](juce::TextEditor& te, juce::Label& l,
        const char* name, int colIdx)
        {
            te.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.f, juce::Font::plain));
            te.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff111111));
            te.setColour(juce::TextEditor::textColourId, juce::Colour(0xfffcffa4));
            te.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a2a2a));
            te.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff555555));
            te.setInputRestrictions(6, "0123456789ABCDEFabcdef");
            te.setJustification(juce::Justification::centred);
            addAndMakeVisible(te);
            styleLabel(l, name);
            te.onReturnKey = [this, &te, colIdx] { applyHexInput(te, colIdx); };
            te.onFocusLost = [this, &te, colIdx] { applyHexInput(te, colIdx); };
        };

    styleSlider(sliderLow, labelLow, "Freq Low");
    styleSlider(sliderHigh, labelHigh, "Freq High");
    styleSlider(sliderThresh, labelThresh, "Threshold");
    styleSlider(sliderReassign, labelReassign, "Max Reassign");
    styleSlider(sliderSpeed, labelSpeed, "Scroll Speed");

    styleCombo(comboFFT, labelFFT, "FFT Size");
    styleCombo(comboHop, labelHop, "Hop Size");
    styleCombo(comboWindow, labelWindow, "Window");
    styleCombo(comboDecim, labelDecim, "Decimation");
    styleCombo(comboInterp, labelInterp, "Smooth");

    comboFFT.addItem("1024", 1); comboFFT.addItem("2048", 2);
    comboFFT.addItem("4096", 3); comboFFT.addItem("8192", 4);
    comboFFT.addItem("16384", 5);

    comboHop.addItem("1/2", 1); comboHop.addItem("1/4", 2);
    comboHop.addItem("1/8", 3); comboHop.addItem("1/16", 4);
    comboHop.addItem("1/32", 5); comboHop.addItem("1/64", 6);
    comboHop.addItem("1/128", 7); comboHop.addItem("1/256", 8);

    comboWindow.addItem("Hann", 1);
    comboWindow.addItem("Blackman-Harris", 2);
    comboWindow.addItem("Nuttall", 3);
    comboWindow.addItem("Kaiser", 4);

    comboDecim.addItem("Off  (22kHz)", 1); comboDecim.addItem("/2  (11kHz)", 2);
    comboDecim.addItem("/4  (5.5kHz)", 3); comboDecim.addItem("/8  (2.8kHz)", 4);
    comboDecim.addItem("/16 (1.4kHz)", 5);

    comboInterp.addItem("Off", 1);
    comboInterp.addItem("On", 2);

    styleHex(hexBgColor, lblBgColor, "BG Colour", 0);
    styleHex(hexGradLow, lblGradLow, "Grad Low", 1);
    styleHex(hexGradHigh, lblGradHigh, "Grad High", 2);

    attachLow = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, "FREQ_LOW", sliderLow);
    attachHigh = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, "FREQ_HIGH", sliderHigh);
    attachThresh = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, "THRESHOLD", sliderThresh);
    attachReassign = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, "MAX_REASSIGN", sliderReassign);
    attachSpeed = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, "SCROLL_SPEED", sliderSpeed);

    attachFFT = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (processor.apvts, "FFT_SIZE", comboFFT);
    attachHop = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (processor.apvts, "HOP_RATIO", comboHop);
    attachWindow = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (processor.apvts, "WINDOW", comboWindow);
    attachDecim = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (processor.apvts, "DECIM", comboDecim);
    attachInterp = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (processor.apvts, "INTERPOLATE", comboInterp);

    {
        juce::Graphics g(spectroImage);
        g.fillAll(juce::Colours::black);
    }

    // ── Sidebar toggle button ────────────────────────────────────────────────
    btnToggleSidebar.setButtonText("Hide Controls");
    btnToggleSidebar.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181818));
    btnToggleSidebar.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfffcffa4));
    btnToggleSidebar.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfffcffa4));
    btnToggleSidebar.onClick = [this] { toggleSidebar(); };
    addAndMakeVisible(btnToggleSidebar);

    syncHexBoxes();
    updateSliderColours();
    startTimerHz(60);
}

SpectralResolverEditor::~SpectralResolverEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
juce::Colour SpectralResolverEditor::getColourParam(int idx) const
{
    auto& a = processor.apvts;
    auto rd = [&](const char* id) { return (uint8_t)*a.getRawParameterValue(id); };

    switch (idx)
    {
    case 0: return juce::Colour(rd("BG_R"), rd("BG_G"), rd("BG_B"));
    case 1: return juce::Colour(rd("GRAD_LOW_R"), rd("GRAD_LOW_G"), rd("GRAD_LOW_B"));
    case 2: return juce::Colour(rd("GRAD_HIGH_R"), rd("GRAD_HIGH_G"), rd("GRAD_HIGH_B"));
    default: return juce::Colours::black;
    }
}

void SpectralResolverEditor::setColourParam(int idx, juce::Colour c)
{
    auto& a = processor.apvts;
    auto wr = [&](const char* id, float v)
        {
            if (auto* par = a.getParameter(id))
                par->setValueNotifyingHost(a.getParameterRange(id).convertTo0to1(v));
        };
    switch (idx)
    {
    case 0: wr("BG_R", float(c.getRed())); wr("BG_G", float(c.getGreen())); wr("BG_B", float(c.getBlue())); break;
    case 1: wr("GRAD_LOW_R", float(c.getRed())); wr("GRAD_LOW_G", float(c.getGreen())); wr("GRAD_LOW_B", float(c.getBlue())); break;
    case 2: wr("GRAD_HIGH_R", float(c.getRed())); wr("GRAD_HIGH_G", float(c.getGreen())); wr("GRAD_HIGH_B", float(c.getBlue())); break;
    default: break;
    }
}

void SpectralResolverEditor::applyHexInput(juce::TextEditor& te, int colIdx)
{
    juce::String text = te.getText().trim().toUpperCase();
    if (text.length() == 6)
    {
        setColourParam(colIdx, juce::Colour::fromString("ff" + text));
        te.setText(text, juce::dontSendNotification);
    }
}

void SpectralResolverEditor::syncHexBoxes()
{
    juce::TextEditor* boxes[3] = { &hexBgColor, &hexGradLow, &hexGradHigh };
    for (int i = 0; i < 3; ++i)
    {
        juce::String hex = colourToHex(getColourParam(i));
        if (!boxes[i]->hasKeyboardFocus(false) &&
            boxes[i]->getText().toUpperCase() != hex)
            boxes[i]->setText(hex, juce::dontSendNotification);
    }
}

void SpectralResolverEditor::updateSliderColours()
{
    const juce::Colour gradLow = getColourParam(1);
    const juce::Colour gradHigh = getColourParam(2);

    // ── Knob arc colours ─────────────────────────────────────────────────────
    juce::Slider* sliders[] = { &sliderLow, &sliderHigh,
                                  &sliderThresh, &sliderReassign, &sliderSpeed };
    for (auto* s : sliders)
    {
        s->setColour(juce::Slider::backgroundColourId, gradLow);
        s->setColour(juce::Slider::thumbColourId, gradHigh);
        s->repaint();
    }

    // ── All labels → gradHigh ────────────────────────────────────────────────
    juce::Label* labels[] = { &labelLow,   &labelHigh,   &labelThresh,
                               &labelReassign, &labelSpeed,
                               &labelFFT,   &labelHop,    &labelWindow,
                               &labelDecim, &labelInterp,
                               &lblBgColor, &lblGradLow,  &lblGradHigh };
    for (auto* l : labels)
    {
        l->setColour(juce::Label::textColourId, gradHigh);
        l->repaint();
    }

    // ── Combo text + arrow → gradHigh ────────────────────────────────────────
    juce::ComboBox* combos[] = { &comboFFT, &comboHop, &comboWindow,
                                  &comboDecim, &comboInterp };
    for (auto* c : combos)
    {
        c->setColour(juce::ComboBox::textColourId, gradHigh);
        c->setColour(juce::ComboBox::arrowColourId, gradHigh);
        c->repaint();
    }

    // ── Hex editor text → gradHigh ────────────────────────────────────────────
    juce::TextEditor* hexes[] = { &hexBgColor, &hexGradLow, &hexGradHigh };
    for (auto* te : hexes)
    {
        te->setColour(juce::TextEditor::textColourId, gradHigh);
        te->repaint();
    }

    // ── Sidebar toggle button text → gradHigh ─────────────────────────────────
    btnToggleSidebar.setColour(juce::TextButton::textColourOffId, gradHigh);
    btnToggleSidebar.setColour(juce::TextButton::textColourOnId, gradHigh);
    btnToggleSidebar.repaint();
}

//==============================================================================
// Forward-map reassigned bins to pixel rows.
//
// Each bin's vertical extent in the spectrogram is proportional to its
// log-scale frequency width: log2(f + binHz) - log2(f).  At low frequencies
// this is many pixels (tall mark); at high frequencies it shrinks to a
// fraction of a pixel (sub-pixel mark rendered with fractional alpha).
//
// For sub-pixel bins the stored alpha is the raw coverage fraction.  Callers
// should boost the final paint alpha proportionally to signal strength so that
// loud high-frequency partials remain clearly visible despite narrow height.
//==============================================================================
void SpectralResolverEditor::frameToPixelMagsAlphas(const SpectralFrame& frame,
    float freqLow, float freqHigh,
    float binHz,
    std::vector<float>& outMags,
    std::vector<float>& outAlphas) const
{
    outMags.assign(IMG_H, -200.f);
    outAlphas.assign(IMG_H, 0.f);
    if (freqLow >= freqHigh) return;

    const float logLow = std::log2(freqLow);
    const float logHigh = std::log2(freqHigh);
    const float range = logHigh - logLow;

    for (size_t i = 0; i < frame.freqHz.size(); ++i)
    {
        const float f = frame.freqHz[i];
        if (f < freqLow || f > freqHigh) continue;

        const float norm = (std::log2(f) - logLow) / range;
        const float rowF = (1.f - norm) * float(IMG_H - 1);   // 0 = top (HF), IMG_H-1 = bottom (LF)
        const float mag = frame.magDB[i];

        // ── Pixel height of this bin in log-frequency space ───────────────
        // The bin spans [f, f+binHz]; clamp the upper edge to freqHigh so bins
        // at the very top of the display don't overshoot.
        float binHPx = 1.f;
        if (binHz > 0.f)
        {
            const float fTop = std::min(f + binHz, freqHigh);
            const float normTop = (std::log2(std::max(fTop, freqLow + 0.001f)) - logLow) / range;
            // normTop > norm  →  smaller row index  →  row height = (normTop-norm)*imgH
            binHPx = std::max(0.01f, (normTop - norm) * float(IMG_H - 1));
        }

        if (binHPx >= 1.f)
        {
            // Fill the integer rows this bin covers (higher freq = smaller row index)
            const int r_bottom = juce::jlimit(0, IMG_H - 1, (int)rowF);
            const int r_top = juce::jlimit(0, IMG_H - 1, r_bottom - (int)binHPx);
            for (int r = r_top; r <= r_bottom; ++r)
            {
                if (mag > outMags[r])
                {
                    outMags[r] = mag;
                    outAlphas[r] = 1.f;
                }
            }
        }
        else
        {
            // Sub-pixel: single row, alpha = fractional coverage
            const int row = juce::jlimit(0, IMG_H - 1, (int)(rowF + 0.5f));
            if (mag > outMags[row])
            {
                outMags[row] = mag;
                outAlphas[row] = binHPx;   // < 1; caller boosts by signal strength
            }
        }
    }
}

//==============================================================================
// Scroll the image left by `count` pixels, filling new right columns with bgColour.
//==============================================================================
static void scrollImage(juce::Image& img, int count, juce::Colour bgColour)
{
    {
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
        const int stride = bd.pixelStride;
        const int rowBytes = img.getWidth() * stride;
        const int scrollBy = count * stride;
        const int keepBytes = rowBytes - scrollBy;

        for (int y = 0; y < img.getHeight(); ++y)
        {
            uint8_t* row = bd.getLinePointer(y);
            std::memmove(row, row + scrollBy, (size_t)keepBytes);
        }
    }

    for (int x = img.getWidth() - count; x < img.getWidth(); ++x)
        for (int y = 0; y < img.getHeight(); ++y)
            img.setPixelAt(x, y, bgColour);
}

//==============================================================================
// Paint one frame into the rightmost `speed` columns using time-reassigned
// bin positions (unchanged from original).
//
// Gradient mapping:
//   t_linear = (magDB - threshDB) / (-threshDB)    [0 at threshold, 1 at 0 dB]
//   t_curved = t_linear ^ GRAD_GAMMA                [γ>1 pulls midrange toward gradLow
//                                                    for richer colour variation]
//==============================================================================
static constexpr float GRAD_GAMMA = 1.8f;   // shape of the gradient curve

static inline float magToT(float magDB, float threshDB) noexcept
{
    // threshDB is negative (e.g. -30).  We map [threshDB, 0] → [0, 1],
    // then apply a power curve so mid-level energy shows mid-gradient colour.
    const float t = juce::jlimit(0.f, 1.f, (magDB - threshDB) / (-threshDB));
    return std::pow(t, GRAD_GAMMA);
}

//==============================================================================
// Paint one frame into the rightmost `speed` columns.
//
// Each reassigned bin is drawn with a vertical height proportional to its
// log-scale frequency extent (binHz wide in Hz).  Low-frequency bins spread
// over several rows; high-frequency bins shrink to sub-pixel marks whose alpha
// equals their pixel coverage, then is boosted by signal strength so that loud
// high-frequency partials remain clearly visible.
//
// Gradient mapping:
//   t_linear = (magDB - threshDB) / (-threshDB)    [0 at threshold, 1 at 0 dB]
//   t_curved = t_linear ^ GRAD_GAMMA
//==============================================================================
static void paintFrameBatch(juce::Image& img,
    const SpectralFrame& frame,
    int                  speed,
    float                freqLow,
    float                freqHigh,
    float                threshDB,
    juce::Colour         gradLow,
    juce::Colour         gradHigh,
    float                binHz)        // ← frequency width of one FFT bin
{
    if (freqLow >= freqHigh || speed <= 0) return;

    const int   imgW = img.getWidth();
    const int   imgH = img.getHeight();
    const int   batchStart = imgW - speed;
    const float logLow = std::log2(freqLow);
    const float logHigh = std::log2(freqHigh);
    const float range = logHigh - logLow;
    const bool  hasTime = !frame.timeOffsetHops.empty();

    static constexpr float EMPTY = -200.f;

    // Per-batch-column, per-row: strongest magnitude and its alpha coverage
    std::vector<std::vector<float>> colMags(speed, std::vector<float>(imgH, EMPTY));
    std::vector<std::vector<float>> colAlphas(speed, std::vector<float>(imgH, 0.f));

    for (size_t i = 0; i < frame.freqHz.size(); ++i)
    {
        const float f = frame.freqHz[i];
        if (f < freqLow || f > freqHigh) continue;

        const float norm = (std::log2(f) - logLow) / range;
        const float rowF = (1.f - norm) * float(imgH - 1);   // 0=top(HF), imgH-1=bottom(LF)
        const float mag = frame.magDB[i];

        // ── Time-reassignment batch column ────────────────────────────────
        int batchCol = speed - 1;
        if (hasTime && i < frame.timeOffsetHops.size())
        {
            const int pixOff = juce::roundToInt(frame.timeOffsetHops[i] * float(speed));
            batchCol = juce::jlimit(0, speed - 1, (speed - 1) + pixOff);
        }

        // ── Pixel height of this bin in log-frequency space ───────────────
        float binHPx = 1.f;
        if (binHz > 0.f)
        {
            const float fTop = std::min(f + binHz, freqHigh);
            const float normTop = (std::log2(std::max(fTop, freqLow + 0.001f)) - logLow) / range;
            binHPx = std::max(0.01f, (normTop - norm) * float(imgH - 1));
        }

        if (binHPx >= 1.f)
        {
            // Full pixel(s): fill rows [rowF - binHPx … rowF]
            const int r_bottom = juce::jlimit(0, imgH - 1, (int)rowF);
            const int r_top = juce::jlimit(0, imgH - 1, r_bottom - (int)binHPx);
            for (int r = r_top; r <= r_bottom; ++r)
            {
                if (mag > colMags[batchCol][r])
                {
                    colMags[batchCol][r] = mag;
                    colAlphas[batchCol][r] = 1.f;
                }
            }
        }
        else
        {
            // Sub-pixel: single row, fractional coverage alpha
            const int row = juce::jlimit(0, imgH - 1, (int)(rowF + 0.5f));
            if (mag > colMags[batchCol][row])
            {
                colMags[batchCol][row] = mag;
                colAlphas[batchCol][row] = binHPx;
            }
        }
    }

    // ── Render accumulated columns ─────────────────────────────────────────
    for (int c = 0; c < speed; ++c)
    {
        const int imgCol = batchStart + c;
        if (imgCol < 0 || imgCol >= imgW) continue;

        for (int y = 0; y < imgH; ++y)
        {
            if (colMags[c][y] <= EMPTY + 1.f) continue;

            const float t = magToT(colMags[c][y], threshDB);
            const float raw = colAlphas[c][y];
            // Boost sub-pixel alpha by signal strength so loud HF partials are
            // always clearly visible: alpha → 1 as t → 1, stays narrow when quiet.
            const float a = juce::jlimit(0.f, 1.f, raw + t * (1.f - raw));

            const juce::Colour paint(
                (uint8_t)(gradLow.getRed() + t * (gradHigh.getRed() - gradLow.getRed())),
                (uint8_t)(gradLow.getGreen() + t * (gradHigh.getGreen() - gradLow.getGreen())),
                (uint8_t)(gradLow.getBlue() + t * (gradHigh.getBlue() - gradLow.getBlue()))
            );

            if (a >= 0.999f)
            {
                img.setPixelAt(imgCol, y, paint);
            }
            else
            {
                const auto bg = img.getPixelAt(imgCol, y);
                img.setPixelAt(imgCol, y, bg.interpolatedWith(paint, (uint8_t)(a * 255.f)));
            }
        }
    }
}

//==============================================================================
// Paint one column of the image from a per-row magnitude + alpha array.
// Alpha < 1 triggers a blend with the existing background pixel so that
// sub-pixel high-frequency marks are rendered cleanly.  The final alpha
// is boosted by signal strength (same formula as paintFrameBatch) so that
// loud partials punch through even at tiny pixel heights.
//==============================================================================
static void paintSingleColumn(juce::Image& img,
    const std::vector<float>& pixMags,
    const std::vector<float>& alphas,
    int                       col,
    float                     threshDB,
    juce::Colour              gradLow,
    juce::Colour              gradHigh)
{
    if (col < 0 || col >= img.getWidth()) return;
    const int imgH = (int)pixMags.size();
    for (int y = 0; y < imgH; ++y)
    {
        if (pixMags[y] <= -199.f) continue;
        const float t = magToT(pixMags[y], threshDB);
        const float raw = (y < (int)alphas.size()) ? alphas[y] : 1.f;
        const float a = juce::jlimit(0.f, 1.f, raw + t * (1.f - raw));

        const juce::Colour paint(
            (uint8_t)(gradLow.getRed() + t * (gradHigh.getRed() - gradLow.getRed())),
            (uint8_t)(gradLow.getGreen() + t * (gradHigh.getGreen() - gradLow.getGreen())),
            (uint8_t)(gradLow.getBlue() + t * (gradHigh.getBlue() - gradLow.getBlue()))
        );

        if (a >= 0.999f)
        {
            img.setPixelAt(col, y, paint);
        }
        else
        {
            const auto bg = img.getPixelAt(col, y);
            img.setPixelAt(col, y, bg.interpolatedWith(paint, (uint8_t)(a * 255.f)));
        }
    }
}

//==============================================================================
void SpectralResolverEditor::toggleSidebar()
{
    sidebarVisible = !sidebarVisible;

    // Show / hide all controls
    juce::Component* controls[] = {
        &sliderLow,  &sliderHigh,  &sliderThresh,  &sliderReassign,  &sliderSpeed,
        &labelLow,   &labelHigh,   &labelThresh,   &labelReassign,   &labelSpeed,
        &comboFFT,   &comboHop,    &comboWindow,   &comboDecim,      &comboInterp,
        &labelFFT,   &labelHop,    &labelWindow,   &labelDecim,      &labelInterp,
        &hexBgColor, &hexGradLow,  &hexGradHigh,
        &lblBgColor, &lblGradLow,  &lblGradHigh
    };
    for (auto* c : controls)
        c->setVisible(sidebarVisible);

    btnToggleSidebar.setButtonText(sidebarVisible ? "Hide Controls"
        : "Show Controls");

    if (sidebarVisible)
    {
        // Restore corner resizer, remove border resizer, restore min height
        borderResizer.reset();
        setResizable(true, true);
        setResizeLimits(700, 460, 4000, 4000);
        setSize(getWidth(), getHeight() + CTRL_H);
    }
    else
    {
        // Remove corner resizer, add all-edge border resizer, allow tiny heights
        setResizable(true, false);
        borderResizer = std::make_unique<juce::ResizableBorderComponent>(this, &resizeConstrainer);
        addAndMakeVisible(*borderResizer);
        borderResizer->toBack();   // stays behind spectrogram so it doesn't eat clicks
        setResizeLimits(40, 80, 2000, 4000);
        setSize(getWidth(), getHeight() - CTRL_H);
    }

    resized();
}

//==============================================================================
void SpectralResolverEditor::timerCallback()
{
    syncHexBoxes();

    const juce::Colour gradLow = getColourParam(1);
    const juce::Colour gradHigh = getColourParam(2);

    if (gradLow != lastGradLow || gradHigh != lastGradHigh)
    {
        lastGradLow = gradLow;
        lastGradHigh = gradHigh;
        updateSliderColours();
        repaint();
    }

    const int          speed = juce::roundToInt(processor.apvts.getRawParameterValue("SCROLL_SPEED")->load());
    const juce::Colour bgColour = getColourParam(0);

    if (bgColour != lastBgColour)
    {
        juce::Graphics gImg(spectroImage);
        gImg.setColour(bgColour);
        gImg.fillAll();
        lastBgColour = bgColour;

        // Reset interpolation state so we don't blend across the colour change
        std::fill(prevMainPixMags.begin(), prevMainPixMags.end(), -200.f);
        std::fill(prevScPixMags.begin(), prevScPixMags.end(), -200.f);
    }

    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    const bool        smoothOn = (int)processor.apvts.getRawParameterValue("INTERPOLATE")->load() > 0;

    // ── Drain main queue ───────────────────────────────────────────────────
    SpectralFrame incoming;
    bool gotNew = processor.popFrame(incoming);
    if (gotNew)
    {
        SpectralFrame newer;
        while (processor.popFrame(newer)) incoming = std::move(newer);
        lastFrame = std::move(incoming);
        hasLastFrame = true;
        lastFrameTimeMs = nowMs;
    }

    // ── Drain sidechain queue ──────────────────────────────────────────────
    SpectralFrame scIncoming;
    bool gotSCNew = processor.popSCFrame(scIncoming);
    if (gotSCNew)
    {
        SpectralFrame newer;
        while (processor.popSCFrame(newer)) scIncoming = std::move(newer);
        scLastFrame = std::move(scIncoming);
        hasScLastFrame = true;
        scLastFrameTimeMs = nowMs;
    }

    const bool mainActive = hasLastFrame && (nowMs - lastFrameTimeMs < SILENCE_TIMEOUT_MS);
    const bool scActive = hasScLastFrame && (nowMs - scLastFrameTimeMs < SILENCE_TIMEOUT_MS);

    const float freqLow = float(sliderLow.getValue());
    const float freqHigh = float(sliderHigh.getValue());
    // Threshold drives the bottom of the gradient: bins at threshold → gradLow,
    // bins at 0 dB → gradHigh.  Using the live parameter value means the gradient
    // rescales automatically whenever the user moves the Threshold knob.
    const float threshDB = float(sliderThresh.getValue());

    // Always scroll
    scrollImage(spectroImage, speed, bgColour);

    // ── Paint main signal ──────────────────────────────────────────────────
    if (mainActive)
    {
        const float binHz = float(processor.getBinHz());

        if (smoothOn)
        {
            // True time interpolation: cosine-blend between the previous frame's
            // pixel-row mags/alphas and the current frame's, one unique blend per column.
            // Column 0 (leftmost new column) = blended toward prev;
            // Column speed-1 (rightmost)     = current frame.
            // This fills every hop gap with a smooth visual transition — no bars.
            std::vector<float> currMags, currAlphas;
            frameToPixelMagsAlphas(lastFrame, freqLow, freqHigh, binHz, currMags, currAlphas);

            std::vector<float> colMags(IMG_H), colAlphas(IMG_H);

            for (int c = 0; c < speed; ++c)
            {
                const float t = (speed > 1) ? float(c) / float(speed - 1) : 1.f;
                const float tc = 0.5f * (1.f - std::cos(t * juce::MathConstants<float>::pi));

                for (int y = 0; y < IMG_H; ++y)
                {
                    const float p = prevMainPixMags[y];
                    const float q = currMags[y];
                    if (p <= -199.f && q <= -199.f) { colMags[y] = -200.f; colAlphas[y] = 0.f; continue; }
                    const float pp = (p <= -199.f) ? q : p;
                    const float qq = (q <= -199.f) ? p : q;
                    colMags[y] = pp + tc * (qq - pp);
                    colAlphas[y] = prevMainPixAlphas[y] + tc * (currAlphas[y] - prevMainPixAlphas[y]);
                }
                paintSingleColumn(spectroImage, colMags, colAlphas, IMG_W - speed + c, threshDB, gradLow, gradHigh);
            }
            prevMainPixMags = std::move(currMags);
            prevMainPixAlphas = std::move(currAlphas);
        }
        else
        {
            paintFrameBatch(spectroImage, lastFrame, speed,
                freqLow, freqHigh, threshDB, gradLow, gradHigh, binHz);
        }
    }

    // ── Paint sidechain (inverted gradient) ────────────────────────────────
    if (scActive)
    {
        const float binHz = float(processor.getBinHz());

        if (smoothOn)
        {
            std::vector<float> currMags, currAlphas;
            frameToPixelMagsAlphas(scLastFrame, freqLow, freqHigh, binHz, currMags, currAlphas);

            std::vector<float> colMags(IMG_H), colAlphas(IMG_H);

            for (int c = 0; c < speed; ++c)
            {
                const float t = (speed > 1) ? float(c) / float(speed - 1) : 1.f;
                const float tc = 0.5f * (1.f - std::cos(t * juce::MathConstants<float>::pi));

                for (int y = 0; y < IMG_H; ++y)
                {
                    const float p = prevScPixMags[y];
                    const float q = currMags[y];
                    if (p <= -199.f && q <= -199.f) { colMags[y] = -200.f; colAlphas[y] = 0.f; continue; }
                    const float pp = (p <= -199.f) ? q : p;
                    const float qq = (q <= -199.f) ? p : q;
                    colMags[y] = pp + tc * (qq - pp);
                    colAlphas[y] = prevScPixAlphas[y] + tc * (currAlphas[y] - prevScPixAlphas[y]);
                }
                paintSingleColumn(spectroImage, colMags, colAlphas, IMG_W - speed + c, threshDB, gradHigh, gradLow);
            }
            prevScPixMags = std::move(currMags);
            prevScPixAlphas = std::move(currAlphas);
        }
        else
        {
            paintFrameBatch(spectroImage, scLastFrame, speed,
                freqLow, freqHigh, threshDB, gradHigh, gradLow, binHz);
        }
    }

    if (mainActive || scActive || gotNew || gotSCNew)
        repaint();
}

//==============================================================================
void SpectralResolverEditor::paint(juce::Graphics& g)
{
    const juce::Colour bgColour = getColourParam(0);
    const juce::Colour gradHigh = getColourParam(2);

    g.fillAll(bgColour.darker(0.3f));

    const int W = getWidth();
    const int H = getHeight();

    const int sX = PAD;
    const int sY = PAD + TITLE_H;
    const int sW = W - 2 * PAD;
    const int sH = H - (sidebarVisible ? CTRL_H : 0) - sY - PAD;

    // ── Spectrogram ────────────────────────────────────────────────────────
    g.drawImage(spectroImage, sX, sY, sW, sH, 0, 0, IMG_W, IMG_H);

    // ── Glow border ────────────────────────────────────────────────────────
    for (int i = 5; i >= 1; --i)
    {
        g.setColour(gradHigh.withAlpha(0.028f * float(i)));
        g.drawRect(sX - i, sY - i, sW + 2 * i, sH + 2 * i, 1);
    }
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(sX, sY, sW, sH, 1);

    // ── Frequency grid ─────────────────────────────────────────────────────
    const float fLow = float(sliderLow.getValue());
    const float fHigh = float(sliderHigh.getValue());
    const float logLow = std::log2(fLow);
    const float logHigh = std::log2(fHigh);

    // Nyquist line
    const double effNyq = processor.getEffectiveSampleRate() * 0.5;
    if (effNyq < double(fHigh) && effNyq > double(fLow))
    {
        const float norm = (std::log2(float(effNyq)) - logLow) / (logHigh - logLow);
        const int   y = sY + int((1.f - norm) * sH);
        g.setColour(juce::Colour(0xff884400));
        g.drawHorizontalLine(y, float(sX), float(sX + sW));
        g.setFont(9.f);
        g.setColour(juce::Colour(0xff885522));
        g.drawText("Nyquist", sX + 4, y - 11, 48, 10, juce::Justification::centredLeft);
    }

    g.setFont(9.5f);
    for (const auto& entry : GRID)
    {
        const float f = entry.freq;
        if (f < fLow || f > fHigh) continue;
        const float norm = (std::log2(f) - logLow) / (logHigh - logLow);
        const int   y = sY + int((1.f - norm) * sH);

        g.setColour(entry.major ? juce::Colour(0xff282828) : juce::Colour(0xff1c1c1c));
        g.drawHorizontalLine(y, float(sX), float(sX + sW));

        g.setColour(entry.major ? juce::Colour(0xff606060) : juce::Colour(0xff484848));
        g.drawText(entry.label, sX + 4, y - 7, 60, 13, juce::Justification::centredLeft);
    }

    // ── Title bar ──────────────────────────────────────────────────────────
    g.setColour(bgColour.darker(0.3f));
    g.fillRect(0, 0, W, TITLE_H + PAD);

    g.setFont(juce::Font(11.f));
    g.setColour(gradHigh.withAlpha(0.6f));
    g.drawText("SpectralResolve  |  Phase Reassignment Spectogram",
        sX, 4, sW, TITLE_H - 4, juce::Justification::centred);

    // ── Control panel (glassy gradient) — only when sidebar is visible ─────
    if (sidebarVisible)
    {
        const int ctrlY = H - CTRL_H;
        // Main panel fill
        juce::ColourGradient panelFill(bgColour.darker(0.05f), 0.f, float(ctrlY),
            bgColour.darker(0.6f), 0.f, float(H), false);
        g.setGradientFill(panelFill);
        g.fillRect(0, ctrlY, W, CTRL_H);

        // Top separator line
        g.setColour(juce::Colour(0xff3c3c3c));
        g.drawHorizontalLine(ctrlY, 0.f, float(W));

        // Accent colour bloom just below separator
        juce::ColourGradient bloom(gradHigh.withAlpha(0.08f), 0.f, float(ctrlY),
            juce::Colours::transparentBlack, 0.f, float(ctrlY + 38), false);
        g.setGradientFill(bloom);
        g.fillRect(0, ctrlY, W, 38);

        // Glass sheen (thin bright line at top of panel)
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(0, ctrlY, W, 2);
    }

    // ── Sidechain activity indicator ───────────────────────────────────────
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    if (hasScLastFrame && (nowMs - scLastFrameTimeMs < SILENCE_TIMEOUT_MS))
    {
        g.setFont(9.f);
        g.setColour(gradHigh.withAlpha(0.7f));
        g.drawText("SC", sX + sW - 22, sY + 4, 18, 10, juce::Justification::centredRight);
    }
}

//==============================================================================
void SpectralResolverEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();

    updateSliderColours();
    syncHexBoxes();

    // ── Border resizer fills the whole component (used when sidebar is hidden) ──
    if (borderResizer != nullptr)
        borderResizer->setBounds(getLocalBounds());

    // ── Toggle button: right side of title bar, always visible ───────────────
    const int btnW = 82;
    const int btnH = TITLE_H - 2;
    btnToggleSidebar.setBounds(W - PAD - btnW, 1, btnW, btnH);

    if (!sidebarVisible)
        return;   // no controls to lay out

    const int ctrlY = H - CTRL_H;
    const int labelH = 13;
    const int comboH = 20;
    const int rowPad = 8;

    // Knob size: capped at 54px, but shrinks on narrow windows
    const int slotW = (W - 2 * PAD) / 5;
    const int knobSize = juce::jmin(54, slotW - 20);
    const int knobTbH = 14;                           // textbox below
    const int knobTotH = knobSize + knobTbH;

    // ── Row 1: 5 rotary knobs (centred within each slot) ──────────────────
    const int row1Y = ctrlY + rowPad;

    juce::Slider* sliders[] = { &sliderLow, &sliderHigh, &sliderThresh,
                                  &sliderReassign, &sliderSpeed };
    juce::Label* slabels[] = { &labelLow,  &labelHigh,  &labelThresh,
                                  &labelReassign, &labelSpeed };

    for (int i = 0; i < 5; ++i)
    {
        const int slotX = PAD + i * slotW;
        const int knobX = slotX + (slotW - knobSize) / 2;
        slabels[i]->setBounds(slotX, row1Y, slotW - 2, labelH);
        sliders[i]->setBounds(knobX, row1Y + labelH + 2, knobSize, knobTotH);
    }

    // ── Row 2: 5 combos + 3 hex inputs ────────────────────────────────────
    const int row2Y = row1Y + labelH + knobTotH + 10;
    const int comboSlotW = (W - 2 * PAD) / 8;

    juce::ComboBox* combos[] = { &comboFFT, &comboHop, &comboWindow,
                                   &comboDecim, &comboInterp };
    juce::Label* clabels[] = { &labelFFT, &labelHop, &labelWindow,
                                   &labelDecim, &labelInterp };

    for (int i = 0; i < 5; ++i)
    {
        const int x = PAD + i * comboSlotW;
        clabels[i]->setBounds(x, row2Y, comboSlotW - 2, labelH);
        combos[i]->setBounds(x, row2Y + labelH + 1, comboSlotW - 4, comboH);
    }

    juce::TextEditor* hexes[] = { &hexBgColor, &hexGradLow, &hexGradHigh };
    juce::Label* hlabels[] = { &lblBgColor, &lblGradLow, &lblGradHigh };

    for (int i = 0; i < 3; ++i)
    {
        const int x = PAD + (5 + i) * comboSlotW;
        hlabels[i]->setBounds(x, row2Y, comboSlotW - 2, labelH);
        hexes[i]->setBounds(x, row2Y + labelH + 1, comboSlotW - 4, comboH);
    }
}