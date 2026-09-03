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

static juce::String freqToNoteString(float freqHz)
{
    if (freqHz <= 0.f) return "--";
    const float midiNote = 12.f * std::log2(freqHz / 440.f) + 69.f;
    const int noteNum = juce::roundToInt(midiNote);
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int noteIdx = (noteNum % 12 + 12) % 12;
    const int octave = (noteNum / 12) - 1;
    return juce::String(noteNames[noteIdx]) + juce::String(octave);
}

//==============================================================================
SpectralResolverEditor::SpectralResolverEditor(SpectralResolverProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&laf);
    setResizable(true, true);
    setResizeLimits(700, 460, 2400, 1600);
    setSize(1100, 680);

    prevMainPixMags.assign(IMG_H, -200.f);
    prevScPixMags.assign(IMG_H, -200.f);
    prevMainPixAlphas.assign(IMG_H, 0.f);
    prevScPixAlphas.assign(IMG_H, 0.f);

    auto styleLabel = [&](juce::Label& l, const char* text)
        {
            l.setText(text, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
            l.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
            l.setFont(juce::Font(10.f));
            addAndMakeVisible(l);
        };

    auto styleSlider = [&](juce::Slider& s, juce::Label& l, const char* name)
        {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 14);
            s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff111111));
            s.setColour(juce::Slider::thumbColourId, juce::Colour(0xfffcffa4));
            addAndMakeVisible(s);
            styleLabel(l, name);
        };

    auto styleCombo = [&](juce::ComboBox& c, juce::Label& l, const char* name)
        {
            c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111111));
            c.setColour(juce::ComboBox::textColourId, juce::Colour(0xfffcffa4));
            c.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2a2a2a));
            c.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xfffcffa4));
            addAndMakeVisible(c);
            styleLabel(l, name);
        };

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

    // ── Show Freq Toggle Button ─────────────────────────────────────────────
    btnShowFreq.setButtonText("Show Freq");
    btnShowFreq.setClickingTogglesState(true);
    btnShowFreq.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181818));
    btnShowFreq.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a2a1a));
    btnShowFreq.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
    btnShowFreq.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfffcffa4));
    addAndMakeVisible(btnShowFreq);
    attachShowFreq = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (processor.apvts, "SHOW_FREQ", btnShowFreq);

    btnToggleSidebar.setButtonText("Hide Controls");
    btnToggleSidebar.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181818));
    btnToggleSidebar.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfffcffa4));
    btnToggleSidebar.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfffcffa4));
    btnToggleSidebar.onClick = [this] { toggleSidebar(); };
    addAndMakeVisible(btnToggleSidebar);

    btnHfWeight.setButtonText("HF Weight");
    btnHfWeight.setClickingTogglesState(true);
    btnHfWeight.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181818));
    btnHfWeight.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a2a1a));
    btnHfWeight.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
    btnHfWeight.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfffcffa4));
    addAndMakeVisible(btnHfWeight);
    attachHfWeightOn = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (processor.apvts, "HF_WEIGHT_ON", btnHfWeight);

    hfWeightThreshSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    hfWeightThreshSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, TITLE_H - 2);
    hfWeightThreshSlider.setNormalisableRange(juce::NormalisableRange<double>(50.0, 5000.0, 1.0, 0.35));
    hfWeightThreshSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff111111));
    hfWeightThreshSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xfffcffa4));
    hfWeightThreshSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xfffcffa4));
    hfWeightThreshSlider.setTextValueSuffix(" Hz");
    addAndMakeVisible(hfWeightThreshSlider);
    attachHfWeightCutoff = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, "HF_WEIGHT_CUTOFF", hfWeightThreshSlider);

    hfWeightThreshLabel.setText("HF Cutoff", juce::dontSendNotification);
    hfWeightThreshLabel.setJustificationType(juce::Justification::centredRight);
    hfWeightThreshLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    hfWeightThreshLabel.setFont(juce::Font(10.f));
    addAndMakeVisible(hfWeightThreshLabel);

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

    juce::Slider* sliders[] = { &sliderLow, &sliderHigh,
                                  &sliderThresh, &sliderReassign, &sliderSpeed };
    for (auto* s : sliders)
    {
        s->setColour(juce::Slider::backgroundColourId, gradLow);
        s->setColour(juce::Slider::thumbColourId, gradHigh);
        s->repaint();
    }

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

    juce::ComboBox* combos[] = { &comboFFT, &comboHop, &comboWindow,
                                  &comboDecim, &comboInterp };
    for (auto* c : combos)
    {
        c->setColour(juce::ComboBox::textColourId, gradHigh);
        c->setColour(juce::ComboBox::arrowColourId, gradHigh);
        c->repaint();
    }

    juce::TextEditor* hexes[] = { &hexBgColor, &hexGradLow, &hexGradHigh };
    for (auto* te : hexes)
    {
        te->setColour(juce::TextEditor::textColourId, gradHigh);
        te->repaint();
    }

    btnToggleSidebar.setColour(juce::TextButton::textColourOffId, gradHigh);
    btnToggleSidebar.setColour(juce::TextButton::textColourOnId, gradHigh);
    btnToggleSidebar.repaint();

    btnShowFreq.setColour(juce::TextButton::textColourOffId, gradHigh);
    btnShowFreq.setColour(juce::TextButton::textColourOnId, gradHigh);
    btnShowFreq.repaint();
}

static void binToRowAlpha(float f, float mag, float binHz, float threshDB,
    float logLow, float logHigh, int imgH,
    bool hfWeightOn, float hfThresholdHz,
    int& rowTop, int& rowBottom, float& alpha);

void SpectralResolverEditor::frameToPixelMagsAlphas(const SpectralFrame& frame,
    float freqLow, float freqHigh,
    float threshDB,
    std::vector<float>& outMags,
    std::vector<float>& outAlphas) const
{
    outMags.assign(IMG_H, -200.f);
    outAlphas.assign(IMG_H, 0.f);
    if (freqLow >= freqHigh) return;

    const float logLow = std::log2(freqLow);
    const float logHigh = std::log2(freqHigh);
    const float binHz = float(processor.getBinHz());
    const bool  hfOn = btnHfWeight.getToggleState();
    const float hfCut = float(hfWeightThreshSlider.getValue());

    for (size_t i = 0; i < frame.freqHz.size(); ++i)
    {
        const float f = frame.freqHz[i];
        if (f < freqLow || f > freqHigh) continue;
        const float mag = frame.magDB[i];

        int rowTop, rowBottom; float alpha;
        binToRowAlpha(f, mag, binHz, threshDB, logLow, logHigh, IMG_H,
            hfOn, hfCut, rowTop, rowBottom, alpha);

        for (int r = rowTop; r <= rowBottom; ++r)
        {
            if (mag > outMags[r])
            {
                outMags[r] = mag;
                outAlphas[r] = alpha;
            }
        }
    }
}

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

static constexpr float GRAD_GAMMA = 1.8f;

static inline float magToT(float magDB, float threshDB) noexcept
{
    const float t = juce::jlimit(0.f, 1.f, (magDB - threshDB) / (-threshDB));
    return std::pow(t, GRAD_GAMMA);
}

static void binToRowAlpha(float f, float mag, float binHz, float threshDB,
    float logLow, float logHigh, int imgH,
    bool hfWeightOn, float hfThresholdHz,
    int& rowTop, int& rowBottom, float& alpha)
{
    const float range = logHigh - logLow;
    const float norm = (std::log2(f) - logLow) / range;
    const float rowF = (1.f - norm) * float(imgH - 1);

    if (!hfWeightOn || f <= hfThresholdHz || binHz <= 0.f)
    {
        rowTop = rowBottom = juce::jlimit(0, imgH - 1, (int)rowF);
        alpha = 1.f;
        return;
    }

    const float fTop = f + binHz;
    const float normTop = (std::log2(fTop) - logLow) / range;
    const float binHPx = std::max(0.01f, (normTop - norm) * float(imgH - 1));

    if (binHPx >= 1.f)
    {
        rowBottom = juce::jlimit(0, imgH - 1, (int)rowF);
        rowTop = juce::jlimit(0, imgH - 1, rowBottom - (int)binHPx);
        alpha = 1.f;
    }
    else
    {
        rowTop = rowBottom = juce::jlimit(0, imgH - 1, (int)(rowF + 0.5f));
        const float t = magToT(mag, threshDB);
        alpha = juce::jlimit(0.f, 1.f, binHPx + t * (1.f - binHPx));
    }
}

static void paintFrameBatch(juce::Image& img,
    const SpectralFrame& frame,
    int                  speed,
    float                freqLow,
    float                freqHigh,
    float                threshDB,
    juce::Colour         gradLow,
    juce::Colour         gradHigh,
    float                binHz,
    bool                 hfWeightOn,
    float                hfThresholdHz)
{
    if (freqLow >= freqHigh || speed <= 0) return;

    const int   imgW = img.getWidth();
    const int   imgH = img.getHeight();
    const int   batchStart = imgW - speed;
    const float logLow = std::log2(freqLow);
    const float logHigh = std::log2(freqHigh);
    const bool  hasTime = !frame.timeOffsetHops.empty();

    static constexpr float EMPTY = -200.f;

    std::vector<std::vector<float>> colMags(speed, std::vector<float>(imgH, EMPTY));
    std::vector<std::vector<float>> colAlphas(speed, std::vector<float>(imgH, 0.f));

    for (size_t i = 0; i < frame.freqHz.size(); ++i)
    {
        const float f = frame.freqHz[i];
        if (f < freqLow || f > freqHigh) continue;
        const float mag = frame.magDB[i];

        int batchCol = speed - 1;
        if (hasTime && i < frame.timeOffsetHops.size())
        {
            const int pixOff = juce::roundToInt(frame.timeOffsetHops[i] * float(speed));
            batchCol = juce::jlimit(0, speed - 1, (speed - 1) + pixOff);
        }

        int rowTop, rowBottom; float alpha;
        binToRowAlpha(f, mag, binHz, threshDB, logLow, logHigh, imgH,
            hfWeightOn, hfThresholdHz, rowTop, rowBottom, alpha);

        for (int r = rowTop; r <= rowBottom; ++r)
        {
            if (mag > colMags[batchCol][r])
            {
                colMags[batchCol][r] = mag;
                colAlphas[batchCol][r] = alpha;
            }
        }
    }

    for (int c = 0; c < speed; ++c)
    {
        const int imgCol = batchStart + c;
        if (imgCol < 0 || imgCol >= imgW) continue;

        for (int y = 0; y < imgH; ++y)
        {
            if (colMags[c][y] <= EMPTY + 1.f) continue;
            const float t = magToT(colMags[c][y], threshDB);
            const juce::Colour paint(
                (uint8_t)(gradLow.getRed() + t * (gradHigh.getRed() - gradLow.getRed())),
                (uint8_t)(gradLow.getGreen() + t * (gradHigh.getGreen() - gradLow.getGreen())),
                (uint8_t)(gradLow.getBlue() + t * (gradHigh.getBlue() - gradLow.getBlue())));

            const float a = colAlphas[c][y];
            if (a >= 0.999f)
                img.setPixelAt(imgCol, y, paint);
            else
                img.setPixelAt(imgCol, y, img.getPixelAt(imgCol, y).interpolatedWith(paint, (uint8_t)(a * 255.f)));
        }
    }
}

static void paintSingleColumn(juce::Image& img,
    const std::vector<float>& pixMags,
    const std::vector<float>& pixAlphas,
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
        const juce::Colour paint(
            (uint8_t)(gradLow.getRed() + t * (gradHigh.getRed() - gradLow.getRed())),
            (uint8_t)(gradLow.getGreen() + t * (gradHigh.getGreen() - gradLow.getGreen())),
            (uint8_t)(gradLow.getBlue() + t * (gradHigh.getBlue() - gradLow.getBlue())));

        const float a = (y < (int)pixAlphas.size()) ? pixAlphas[y] : 1.f;
        if (a >= 0.999f)
            img.setPixelAt(col, y, paint);
        else
            img.setPixelAt(col, y, img.getPixelAt(col, y).interpolatedWith(paint, (uint8_t)(a * 255.f)));
    }
}

//==============================================================================
void SpectralResolverEditor::toggleSidebar()
{
    sidebarVisible = !sidebarVisible;

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
        borderResizer.reset();
        setResizable(true, true);
        setResizeLimits(700, 460, 2400, 1600);
        setSize(getWidth(), getHeight() + CTRL_H);
    }
    else
    {
        setResizable(true, false);
        borderResizer = std::make_unique<juce::ResizableBorderComponent>(this, &resizeConstrainer);
        addAndMakeVisible(*borderResizer);
        borderResizer->toBack();
        setResizeLimits(200, 80, 2400, 1600);
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

        std::fill(prevMainPixMags.begin(), prevMainPixMags.end(), -200.f);
        std::fill(prevScPixMags.begin(), prevScPixMags.end(), -200.f);
    }

    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    const bool        smoothOn = (int)processor.apvts.getRawParameterValue("INTERPOLATE")->load() > 0;

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

    // ── Peak frequency tracking over time ────────────────────────────────
    //if (hasLastFrame && !lastFrame.freqHz.empty())
    //{
    //    float maxMag = -200.f;
    //    float peakF = 0.f;
    //    for (size_t i = 0; i < lastFrame.freqHz.size(); ++i)
    //    {
    //        if (lastFrame.magDB[i] > maxMag)
    //        {
    //            maxMag = lastFrame.magDB[i];
    //            peakF = lastFrame.freqHz[i];
    //        }
    //    }
    //    if (peakF > 0.f)
    //    {
    //        if (smoothedPeakFreq <= 0.f)
    //            smoothedPeakFreq = peakF;
    //        else
    //            smoothedPeakFreq += 0.25f * (peakF - smoothedPeakFreq);
    //    }
    //}

    const bool mainActive = hasLastFrame && (nowMs - lastFrameTimeMs < SILENCE_TIMEOUT_MS);
    const bool scActive = hasScLastFrame && (nowMs - scLastFrameTimeMs < SILENCE_TIMEOUT_MS);

    const float freqLow = float(sliderLow.getValue());
    const float freqHigh = float(sliderHigh.getValue());
    const float threshDB = float(sliderThresh.getValue());
    const bool  hfOn = btnHfWeight.getToggleState();
    const float hfCut = float(hfWeightThreshSlider.getValue());

    scrollImage(spectroImage, speed, bgColour);

    if (mainActive)
    {
        if (smoothOn)
        {
            std::vector<float> currMags, currAlphas;
            frameToPixelMagsAlphas(lastFrame, freqLow, freqHigh, threshDB, currMags, currAlphas);
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
                freqLow, freqHigh, threshDB, gradLow, gradHigh,
                float(processor.getBinHz()), hfOn, hfCut);
        }
    }

    if (scActive)
    {
        if (smoothOn)
        {
            std::vector<float> currMags, currAlphas;
            frameToPixelMagsAlphas(scLastFrame, freqLow, freqHigh, threshDB, currMags, currAlphas);
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
                freqLow, freqHigh, threshDB, gradHigh, gradLow,
                float(processor.getBinHz()), hfOn, hfCut);
        }
    }

    if (mainActive || scActive || gotNew || gotSCNew || btnShowFreq.getToggleState())
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

    g.drawImage(spectroImage, sX, sY, sW, sH, 0, 0, IMG_W, IMG_H);

    for (int i = 5; i >= 1; --i)
    {
        g.setColour(gradHigh.withAlpha(0.028f * float(i)));
        g.drawRect(sX - i, sY - i, sW + 2 * i, sH + 2 * i, 1);
    }
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(sX, sY, sW, sH, 1);

    const float fLow = float(sliderLow.getValue());
    const float fHigh = float(sliderHigh.getValue());
    const float logLow = std::log2(fLow);
    const float logHigh = std::log2(fHigh);

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

    // ── Show Frequency Box Overlay ──────────────────────────────────────────
    if (btnShowFreq.getToggleState())
    {
        const auto mousePos = getMouseXYRelative();
        const bool mouseInVis = (mousePos.x >= sX && mousePos.x <= sX + sW &&
            mousePos.y >= sY && mousePos.y <= sY + sH);

        float targetFreq = 0.f;
        int boxX = 0;
        int boxY = 0;

        if (mouseInVis)
        {
            const float normY = 1.0f - juce::jlimit(
                0.0f, 1.0f,
                float(mousePos.y - sY) / float(sH));

            const float mouseFreq = std::pow(
                2.0f,
                logLow + normY * (logHigh - logLow));

            float closestF = 0.f;
            float minDiff = 1e9f;

            for (float f : lastFrame.freqHz)
            {
                if (f > 0.f)
                {
                    const float diff = std::abs(f - mouseFreq);

                    if (diff < minDiff)
                    {
                        minDiff = diff;
                        closestF = f;
                    }
                }
            }

            // Keep this fallback if there is no frequency data available.
            targetFreq = (closestF > 0.f) ? closestF : mouseFreq;

            const float targetNormY =
                (std::log2(targetFreq) - logLow) / (logHigh - logLow);

            boxY = sY + int((1.0f - targetNormY) * float(sH));
            boxX = mousePos.x;
        }

        /*
        // NOT NEEDED:
        // This is the fallback that shows the overall loudest/peak frequency
        // when the mouse is outside the spectrogram.

        else if (smoothedPeakFreq > 0.f)
        {
            targetFreq = smoothedPeakFreq;

            const float targetNormY =
                (std::log2(targetFreq) - logLow) / (logHigh - logLow);

            boxY = sY + int((1.0f - targetNormY) * float(sH));
            boxX = sX + sW - 70;
        }
        */

        // This will only run when the mouse is inside the spectrogram,
        // because targetFreq remains 0 when mouseInVis is false.
        if (targetFreq >= fLow && targetFreq <= fHigh)
        {
            // Convert the selected frequency to its nearest musical note.
            juce::String noteStr = freqToNoteString(targetFreq);

            juce::String text = juce::String(targetFreq, 1) + " Hz | " + noteStr;

            g.setFont(juce::Font(11.0f, juce::Font::bold));

            const int textW =
                g.getCurrentFont().getStringWidth(text) + 12;

            const int textH = 18;

            int drawX = juce::jlimit(
                sX + 2,
                sX + sW - textW - 2,
                boxX + 10);

            int drawY = juce::jlimit(
                sY + 2,
                sY + sH - textH - 2,
                boxY - textH / 2);

            g.setColour(juce::Colour(0xdd111111));
            g.fillRoundedRectangle(
                float(drawX), float(drawY),
                float(textW), float(textH), 4.0f);

            g.setColour(gradHigh);
            g.drawRoundedRectangle(
                float(drawX), float(drawY),
                float(textW), float(textH), 4.0f, 1.0f);

            g.setColour(gradHigh);
            g.drawText(
                text,
                drawX, drawY,
                textW, textH,
                juce::Justification::centred);

            // Marker showing the nearest frequency position.
            g.setColour(gradHigh.withAlpha(0.8f));
            g.fillEllipse(
                float(boxX - 3),
                float(boxY - 3),
                6.0f, 6.0f);
        }
    }

    g.setColour(bgColour.darker(0.3f));
    g.fillRect(0, 0, W, TITLE_H + PAD);

    g.setFont(juce::Font(11.f));
    g.setColour(gradHigh.withAlpha(0.6f));
    g.drawText("SpectralResolve  |  Phase Reassignment Spectogram",
        sX, 4, sW, TITLE_H - 4, juce::Justification::centred);

    if (sidebarVisible)
    {
        const int ctrlY = H - CTRL_H;
        juce::ColourGradient panelFill(bgColour.darker(0.05f), 0.f, float(ctrlY),
            bgColour.darker(0.6f), 0.f, float(H), false);
        g.setGradientFill(panelFill);
        g.fillRect(0, ctrlY, W, CTRL_H);

        g.setColour(juce::Colour(0xff3c3c3c));
        g.drawHorizontalLine(ctrlY, 0.f, float(W));

        juce::ColourGradient bloom(gradHigh.withAlpha(0.08f), 0.f, float(ctrlY),
            juce::Colours::transparentBlack, 0.f, float(ctrlY + 38), false);
        g.setGradientFill(bloom);
        g.fillRect(0, ctrlY, W, 38);

        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(0, ctrlY, W, 2);
    }

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

    if (borderResizer != nullptr)
        borderResizer->setBounds(getLocalBounds());

    const int btnW = 82;
    const int btnH = TITLE_H - 2;
    btnToggleSidebar.setBounds(W - PAD - btnW, 1, btnW, btnH);

    const int showFreqBtnW = 76;
    btnShowFreq.setBounds(PAD, 1, showFreqBtnW, btnH);

    const int hfBtnW = 96;
    btnHfWeight.setBounds(W - PAD - btnW - PAD - hfBtnW, 1, hfBtnW, btnH);

    const int hfSliderW = 150;
    const int hfLabelW = 58;
    const int hfGroupX = W - PAD - btnW - PAD - hfBtnW - PAD - hfLabelW - hfSliderW;
    hfWeightThreshLabel.setBounds(hfGroupX, 1, hfLabelW, btnH);
    hfWeightThreshSlider.setBounds(hfGroupX + hfLabelW + 4, 1, hfSliderW, btnH);

    if (!sidebarVisible)
        return;

    const int ctrlY = H - CTRL_H;
    const int labelH = 13;
    const int comboH = 20;
    const int rowPad = 8;

    const int slotW = (W - 2 * PAD) / 5;
    const int knobSize = juce::jmin(54, slotW - 20);
    const int knobTbH = 14;
    const int knobTotH = knobSize + knobTbH;

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