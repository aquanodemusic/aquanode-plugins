#include "PluginEditor.h"
#include <cmath>

static constexpr int kDefaultWidth  = 1024;
static constexpr int kDefaultHeight = 600;

// Knob panel dimensions (top-right corner)
static constexpr int kKnobSize   = 46;
static constexpr int kKnobGap    = 10;
static constexpr int kLabelH     = 13;
static constexpr int kPanelPad   = 8;

//==============================================================================
SpectrogramEditor::SpectrogramEditor (SpectrogramProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (kDefaultWidth, kDefaultHeight);
    setResizable (true, false);

    spectrogramImage = juce::Image (juce::Image::ARGB,
        kDefaultWidth, kDefaultHeight, true);
    spectrogramImage.clear (spectrogramImage.getBounds(), juce::Colours::black);

    lastReadHead = proc.writeHead.load (std::memory_order_acquire);

    // --- GATE knob  (−70 … −20 dB, default −40 dB) ---
    setupKnob (gateKnob, gateLabel, "GATE",
               -70.0, -20.0, -40.0,
               [this](double v)
               {
                   proc.paramGateDeltaDB.store (static_cast<float> (v),
                       std::memory_order_relaxed);
               });

    // --- BRIGHT knob  (gamma 0.30 … 1.20, default 0.65) ---
    setupKnob (brightKnob, brightLabel, "BRIGHT",
               0.30, 1.20, 0.65,
               [this](double v)
               {
                   proc.paramBrightGamma.store (static_cast<float> (v),
                       std::memory_order_relaxed);
               });

    // --- DECAY knob
    //     Displayed as 0.0 (fast) … 1.0 (slow), mapped to decay ∈ [0.9990, 1.0]
    //     decay = 0.9990 + displayValue * (1.0 − 0.9990) / 1.0
    setupKnob (decayKnob, decayLabel, "DECAY",
               0.0, 1.0, 0.5,
               [this](double v)
               {
                   const float decay = 0.9990f + static_cast<float> (v) * 0.0010f;
                   proc.paramPeakDecay.store (decay, std::memory_order_relaxed);
               });

    startTimerHz (60);
}

SpectrogramEditor::~SpectrogramEditor()
{
    stopTimer();
}

//==============================================================================
void SpectrogramEditor::setupKnob (juce::Slider& s, juce::Label& l,
                                    const juce::String& labelText,
                                    double minV, double maxV, double defV,
                                    std::function<void(double)> onChange)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRange (minV, maxV);
    s.setValue (defV, juce::dontSendNotification);
    s.setDoubleClickReturnValue (true, defV);
    s.setPopupDisplayEnabled (true, true, this);

    s.onValueChange = [cb = std::move (onChange), &s]()
    {
        cb (s.getValue());
    };

    // Style: semi-transparent dark background ring
    s.setColour (juce::Slider::rotarySliderFillColourId,
                 juce::Colour (0xFF3399FF));
    s.setColour (juce::Slider::rotarySliderOutlineColourId,
                 juce::Colour (0xFF223355));
    s.setColour (juce::Slider::thumbColourId,
                 juce::Colours::white);

    addAndMakeVisible (s);

    l.setText (labelText, juce::dontSendNotification);
    l.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f,
                           juce::Font::plain));
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    addAndMakeVisible (l);
}

//==============================================================================
void SpectrogramEditor::timerCallback()
{
    const int curHead = proc.writeHead.load (std::memory_order_acquire);
    if (curHead == lastReadHead) return;

    drawNewColumns (lastReadHead, curHead);
    lastReadHead = curHead;
    repaint();
}

//==============================================================================
void SpectrogramEditor::drawNewColumns (int from, int to)
{
    const int W       = spectrogramImage.getWidth();
    const int H       = spectrogramImage.getHeight();
    const int maxCols = SpectrogramProcessor::MAX_COLUMNS;

    int numNew = (to - from + maxCols) % maxCols;
    if (numNew == 0) return;
    numNew = juce::jmin (numNew, W);

    // Update running peak
    for (int col = 0; col < numNew; ++col)
    {
        const int ri = (from + col) % maxCols;
        for (float v : proc.ringBuffer[ri])
            if (v > runningPeak) runningPeak = v;
    }
    // Apply decay using the user-controlled rate
    const float decay = proc.paramPeakDecay.load (std::memory_order_relaxed);
    runningPeak = std::max (runningPeak * decay, 1e-7f);

    const float peakInv     = 1.0f / runningPeak;
    const float displayBins = static_cast<float> (SpectrogramProcessor::DISPLAY_BINS);
    const float gamma       = proc.paramBrightGamma.load (std::memory_order_relaxed);

    // Y → display-bin lookup (log-frequency axis)
    juce::HeapBlock<int> yToBin (H);
    for (int y = 0; y < H; ++y)
    {
        const float t   = 1.0f - static_cast<float> (y) / static_cast<float> (H - 1);
        const int   bin = static_cast<int> (t * (displayBins - 1.0f) + 0.5f);
        yToBin[y] = juce::jlimit (0, SpectrogramProcessor::DISPLAY_BINS - 1, bin);
    }

    {
        juce::Image::BitmapData bmp (spectrogramImage,
            juce::Image::BitmapData::readWrite);

        // Shift image left
        const int shiftBytes = numNew * bmp.pixelStride;
        const int keepBytes  = (W - numNew) * bmp.pixelStride;
        for (int y = 0; y < H; ++y)
        {
            uint8_t* line = bmp.getLinePointer (y);
            std::memmove (line, line + shiftBytes, static_cast<size_t> (keepBytes));
        }

        // Paint new columns on the right edge
        for (int col = 0; col < numNew; ++col)
        {
            const int x      = W - numNew + col;
            const int ri     = (from + col) % maxCols;
            const auto& column = proc.ringBuffer[ri];

            for (int y = 0; y < H; ++y)
            {
                const float mag      = column[yToBin[y]];
                const float normAmp  = mag * peakInv;
                const float dB       = 20.0f * std::log10 (normAmp + 1e-9f);
                float intensity = juce::jmap (dB, -70.0f, 0.0f, 0.0f, 1.0f);
                intensity = juce::jlimit (0.0f, 1.0f, intensity);
                intensity = std::pow (intensity, gamma);   // user-controlled gamma

                const juce::Colour c = spectralColour (intensity);

                uint8_t* px = bmp.getPixelPointer (x, y);
                px[0] = c.getBlue();
                px[1] = c.getGreen();
                px[2] = c.getRed();
                px[3] = 0xFF;
            }
        }
    }
}

//==============================================================================
void SpectrogramEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (spectrogramImage, 0, 0);
    drawCNoteOverlay (g);

    // Subtle plugin title
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.5f,
               juce::Font::plain));
    g.drawText ("SPECTRA \xe2\x80\x94 TF REASSIGNMENT",
        getWidth() - 340, 6, 200, 14,
        juce::Justification::right, false);
}

//==============================================================================
void SpectrogramEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();

    // Rebuild canvas
    juce::Image newImg (juce::Image::ARGB, W, H, true);
    newImg.clear (newImg.getBounds(), juce::Colours::black);
    if (spectrogramImage.isValid())
    {
        juce::Graphics g (newImg);
        g.drawImage (spectrogramImage, 0, 0, W, H,
                     0, 0,
                     spectrogramImage.getWidth(),
                     spectrogramImage.getHeight());
    }
    spectrogramImage = newImg;

    // --- Layout three knobs in top-right corner ---
    // Each knob unit: kKnobSize wide, (kKnobSize + kLabelH) tall
    const int unitW    = kKnobSize;
    const int unitH    = kKnobSize + kLabelH;
    const int totalW   = 3 * unitW + 2 * kKnobGap + 2 * kPanelPad;
    const int panelX   = W - totalW;
    const int panelY   = kPanelPad;

    auto placeKnob = [&](juce::Slider& s, juce::Label& l, int slotIndex)
    {
        const int x = panelX + kPanelPad + slotIndex * (unitW + kKnobGap);
        s.setBounds (x, panelY, unitW, kKnobSize);
        l.setBounds (x, panelY + kKnobSize, unitW, kLabelH);
    };

    placeKnob (gateKnob,   gateLabel,   0);
    placeKnob (brightKnob, brightLabel, 1);
    placeKnob (decayKnob,  decayLabel,  2);
}

//==============================================================================
void SpectrogramEditor::drawCNoteOverlay (juce::Graphics& g) const
{
    static constexpr float kC0 = 16.3516f;

    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                           9.5f, juce::Font::plain));

    for (int octave = 0; octave <= 19; ++octave)
    {
        const float freq = kC0 * std::pow (2.0f, static_cast<float> (octave));
        if (freq < SpectrogramProcessor::FREQ_MIN ||
            freq > SpectrogramProcessor::FREQ_MAX) continue;

        const int y = static_cast<int> (freqToY (freq));

        g.setColour (juce::Colours::white.withAlpha (0.22f));
        for (int x = 0; x < getWidth(); x += 7)
            g.drawHorizontalLine (y,
                static_cast<float> (x),
                static_cast<float> (x + 3));

        g.setColour (juce::Colours::white.withAlpha (0.50f));
        g.drawText ("C" + juce::String (octave),
            4, y - 10, 22, 10,
            juce::Justification::left, false);
    }
}

//==============================================================================
float SpectrogramEditor::freqToY (float freqHz) const noexcept
{
    const float H = static_cast<float> (getHeight());
    if (freqHz <= SpectrogramProcessor::FREQ_MIN) return H - 1.0f;
    if (freqHz >= SpectrogramProcessor::FREQ_MAX) return 0.0f;
    const float t = std::log (freqHz / SpectrogramProcessor::FREQ_MIN)
                  / std::log (SpectrogramProcessor::FREQ_MAX
                              / SpectrogramProcessor::FREQ_MIN);
    return (1.0f - t) * (H - 1.0f);
}

//==============================================================================
juce::Colour SpectrogramEditor::spectralColour (float t) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);

    struct Stop { float pos; uint8_t r, g, b; };
    static constexpr Stop stops[] = {
        { 0.00f,   0,   0,   0 },
        { 0.20f,   0,   0,  60 },
        { 0.40f,   0,  20, 210 },
        { 0.62f,  10, 130, 255 },
        { 0.80f,  20, 230, 255 },
        { 1.00f, 180, 255, 255 },
    };
    static constexpr int kNumStops = static_cast<int> (sizeof (stops) / sizeof (stops[0]));

    int lo = 0;
    for (int i = 0; i < kNumStops - 1; ++i)
    {
        if (t <= stops[i + 1].pos) { lo = i; break; }
        lo = i;
    }
    const int hi = juce::jmin (lo + 1, kNumStops - 1);

    const float range = stops[hi].pos - stops[lo].pos;
    const float s     = (range > 0.0f) ? (t - stops[lo].pos) / range : 0.0f;

    auto lerp8 = [](uint8_t a, uint8_t b, float alpha) -> uint8_t
    {
        return static_cast<uint8_t> (
            static_cast<int>(a) + static_cast<int> (
                (static_cast<float> (static_cast<int>(b) - static_cast<int>(a))) * alpha));
    };

    return juce::Colour (lerp8 (stops[lo].r, stops[hi].r, s),
                         lerp8 (stops[lo].g, stops[hi].g, s),
                         lerp8 (stops[lo].b, stops[hi].b, s));
}
