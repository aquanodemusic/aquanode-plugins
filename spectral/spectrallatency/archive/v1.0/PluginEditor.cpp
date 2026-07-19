#include "PluginProcessor.h"
#include "PluginEditor.h"

// Width of the right-side control panel in pixels
static constexpr int kRightPanelWidth = 82;

//==============================================================================
SpectralLatencyAudioProcessorEditor::SpectralLatencyAudioProcessorEditor (SpectralLatencyAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (960, 540);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);

    // Pre-size display buffer to the largest possible number of bins
    spectrum.assign (SpectralLatencyAudioProcessor::maxBins, 0.0f);
    delayCurve.fill (0.0f);

    //==========================================================================
    // FFT Size combo
    fftSizeLabel.setText ("FFT Size:", juce::dontSendNotification);
    fftSizeLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    fftSizeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (fftSizeLabel);

    fftSizeCombo.addItem ("32",   5);
    fftSizeCombo.addItem ("128",  6);
    fftSizeCombo.addItem ("256",  7);
    fftSizeCombo.addItem ("512",  8);
    fftSizeCombo.addItem ("1024", 1);
    fftSizeCombo.addItem ("2048", 2);
    fftSizeCombo.addItem ("4096", 3);
    fftSizeCombo.addItem ("8192", 4);

    // Select the correct entry for the processor's current FFT size
    switch (audioProcessor.fftSize)
    {
        case 32:   fftSizeCombo.setSelectedId (5); break;
        case 128:  fftSizeCombo.setSelectedId (6); break;
        case 256:  fftSizeCombo.setSelectedId (7); break;
        case 512:  fftSizeCombo.setSelectedId (8); break;
        case 1024: fftSizeCombo.setSelectedId (1); break;
        default:
        case 2048: fftSizeCombo.setSelectedId (2); break;
        case 4096: fftSizeCombo.setSelectedId (3); break;
        case 8192: fftSizeCombo.setSelectedId (4); break;
    }

    fftSizeCombo.onChange = [this]
    {
        int newSize = 2048;
        switch (fftSizeCombo.getSelectedId())
        {
            case 1: newSize = 1024; break;
            case 2: newSize = 2048; break;
            case 3: newSize = 4096; break;
            case 4: newSize = 8192; break;
            case 5: newSize = 32;   break;
            case 6: newSize = 128;  break;
            case 7: newSize = 256;  break;
            case 8: newSize = 512;  break;
        }
        audioProcessor.setFFTSize (newSize);
    };
    addAndMakeVisible (fftSizeCombo);

    randomizeButton.setButtonText("Randomize");
    randomizeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333344));
    randomizeButton.onClick = [this]
        {
            audioProcessor.randomize();
            // Update the slider UI to match the new random value from the processor
            maxLatencySlider.setValue(audioProcessor.getMaxLatency(), juce::sendNotification);
        };
    addAndMakeVisible(randomizeButton);

    //==========================================================================
    // Max-latency slider  (0.01 s … 10 s, skewed to feel natural)
    maxLatencyLabel.setText ("Max Latency", juce::dontSendNotification);
    maxLatencyLabel.setJustificationType (juce::Justification::centred);
    maxLatencyLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    addAndMakeVisible (maxLatencyLabel);

    maxLatencySlider.setSliderStyle (juce::Slider::LinearVertical);
    maxLatencySlider.setRange (0.01, 10.0, 0.0);
    maxLatencySlider.setSkewFactorFromMidPoint (0.5);   // 0.5 s at the centre of travel
    maxLatencySlider.setValue (static_cast<double> (audioProcessor.getMaxLatency()),
                               juce::dontSendNotification);
    maxLatencySlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kRightPanelWidth - 4, 20);
    maxLatencySlider.setNumDecimalPlacesToDisplay (2);
    maxLatencySlider.setTextValueSuffix (" s");

    maxLatencySlider.onValueChange = [this]
    {
        audioProcessor.setMaxLatency (static_cast<float> (maxLatencySlider.getValue()));
        repaint();  // grid labels depend on max latency
    };
    addAndMakeVisible (maxLatencySlider);

    startTimerHz (30);
}

SpectralLatencyAudioProcessorEditor::~SpectralLatencyAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SpectralLatencyAudioProcessorEditor::timerCallback()
{
    audioProcessor.getDelayCurve (delayCurve);
    audioProcessor.getSpectrumData (spectrum.data(), audioProcessor.numBins);
    repaint();
}

//==============================================================================
// Coordinate helpers
//==============================================================================

float SpectralLatencyAudioProcessorEditor::xToFreq (float x) const
{
    const float nyquist = static_cast<float> (audioProcessor.currentSampleRate) * 0.5f;
    const float plotW   = static_cast<float> (getWidth() - kRightPanelWidth);
    const float logMin  = std::log10 (20.0f);
    const float logMax  = std::log10 (nyquist);

    float norm = juce::jlimit (0.0f, 1.0f, x / plotW);
    return std::pow (10.0f, logMin + norm * (logMax - logMin));
}

float SpectralLatencyAudioProcessorEditor::freqToX (float freq) const
{
    const float nyquist = static_cast<float> (audioProcessor.currentSampleRate) * 0.5f;
    const float plotW   = static_cast<float> (getWidth() - kRightPanelWidth);
    const float logMin  = std::log10 (20.0f);
    const float logMax  = std::log10 (nyquist);

    freq = juce::jlimit (20.0f, nyquist, freq);
    float norm = (std::log10 (freq) - logMin) / (logMax - logMin);
    return norm * plotW;
}

// y = 0 (top)    → delay = +1.0  (delayed by maxLatency)
// y = h/2 (mid)  → delay =  0.0  (no additional delay)
// y = h (bottom) → delay = -1.0  (sounds earlier by maxLatency)
float SpectralLatencyAudioProcessorEditor::yToDelay (float y) const
{
    const float h = static_cast<float> (getHeight());
    float norm = 1.0f - juce::jlimit (0.0f, 1.0f, y / h);
    return norm * 2.0f - 1.0f;
}

float SpectralLatencyAudioProcessorEditor::delayToY (float delay) const
{
    const float h = static_cast<float> (getHeight());
    float norm = (juce::jlimit (-1.0f, 1.0f, delay) + 1.0f) * 0.5f;
    return h * (1.0f - norm);
}

//==============================================================================
// Mouse interaction
//==============================================================================

void SpectralLatencyAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.x >= getWidth() - kRightPanelWidth)
        return;

    isDragging = true;
    lastDragX  = static_cast<float> (e.x);
    lastDragY  = static_cast<float> (e.y);

    int bin = juce::jlimit (0, audioProcessor.numBins - 1,
        static_cast<int> (xToFreq (lastDragX) /
                          (audioProcessor.currentSampleRate * 0.5f) *
                          (audioProcessor.numBins - 1)));
    float val = yToDelay (lastDragY);
    audioProcessor.setDelayCurveRange (bin, bin, val, val);
}

void SpectralLatencyAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDragging)
        return;

    float x = static_cast<float> (e.x);
    float y = static_cast<float> (e.y);

    const float nyquist = static_cast<float> (audioProcessor.currentSampleRate) * 0.5f;

    int startBin = juce::jlimit (0, audioProcessor.numBins - 1,
        static_cast<int> (xToFreq (lastDragX) / nyquist * (audioProcessor.numBins - 1)));
    int endBin   = juce::jlimit (0, audioProcessor.numBins - 1,
        static_cast<int> (xToFreq (x)         / nyquist * (audioProcessor.numBins - 1)));

    audioProcessor.setDelayCurveRange (startBin, endBin,
                                       yToDelay (lastDragY),
                                       yToDelay (y));
    lastDragX = x;
    lastDragY = y;
}

void SpectralLatencyAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    isDragging = false;
}

void SpectralLatencyAudioProcessorEditor::mouseMove (const juce::MouseEvent&)
{
    repaint();   // keeps tooltip live
}

void SpectralLatencyAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent&)
{
    audioProcessor.resetDelayCurve();
}

//==============================================================================
// Paint
//==============================================================================

void SpectralLatencyAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark background
    g.fillAll (juce::Colour (0xff16161e));

    // Thin separator between plot area and right panel
    g.setColour (juce::Colour (0xff333344));
    g.fillRect (getWidth() - kRightPanelWidth, 0, 1, getHeight());

    drawGrid       (g);
    drawSpectrum   (g);
    drawDelayCurve (g);

    if (isMouseOver (true))
    {
        auto pos = getMouseXYRelative();
        if (pos.x < getWidth() - kRightPanelWidth)
            drawTooltip (g, static_cast<float> (pos.x),
                            static_cast<float> (pos.y));
    }
}

//==============================================================================
void SpectralLatencyAudioProcessorEditor::drawGrid (juce::Graphics& g)
{
    const float nyquist = static_cast<float> (audioProcessor.currentSampleRate) * 0.5f;
    const float plotW   = static_cast<float> (getWidth() - kRightPanelWidth);
    const float h       = static_cast<float> (getHeight());
    const float maxLat  = audioProcessor.getMaxLatency();

    g.setFont (10.0f);

    //--------------------------------------------------------------------------
    // Horizontal delay-level lines  (–1.0, –0.5, 0.0, +0.5, +1.0)
    //--------------------------------------------------------------------------
    const float delayMarks[] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };

    for (float dv : delayMarks)
    {
        float y = delayToY (dv);

        // Centre line slightly brighter
        if (dv == 0.0f)
        {
            g.setColour (juce::Colour (0xff555566));
            g.drawHorizontalLine (static_cast<int> (y), 0.0f, plotW);
            // "0" label
            g.setColour (juce::Colour (0xff888899));
            g.drawText ("0 ms", static_cast<int> (plotW) - 42, static_cast<int> (y) - 7,
                        40, 14, juce::Justification::right);
        }
        else
        {
            g.setColour (juce::Colour (0xff2e2e3e));
            g.drawHorizontalLine (static_cast<int> (y), 0.0f, plotW);

            // Convert normalised value → actual time in ms or s
            float ms = dv * maxLat * 1000.0f;
            juce::String label;
            if (std::abs (ms) >= 1000.0f)
                label = juce::String (ms / 1000.0f, 1) + " s";
            else
                label = juce::String (static_cast<int> (std::round (ms))) + " ms";
            if (ms > 0.0f)
                label = "+" + label;

            g.setColour (juce::Colour (0xff666677));
            g.drawText (label, static_cast<int> (plotW) - 42,
                        static_cast<int> (y) - 7, 40, 14,
                        juce::Justification::right);
        }
    }

    // Subtle upward / downward region tint so the user knows which side is which
    const float zeroY = delayToY (0.0f);

    // Positive-delay region (above centre) — very faint warm tint
    g.setColour (juce::Colour (0x08ffaa44));
    g.fillRect (0.0f, 0.0f, plotW, zeroY);

    // Negative-delay region (below centre) — very faint cool tint
    g.setColour (juce::Colour (0x0844aaff));
    g.fillRect (0.0f, zeroY, plotW, h - zeroY);

    //--------------------------------------------------------------------------
    // Vertical frequency lines
    //--------------------------------------------------------------------------
    const float freqs[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                             1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    for (float f : freqs)
    {
        if (f > nyquist) continue;
        float x = freqToX (f);

        g.setColour (juce::Colour (0xff2a2a3a));
        g.drawVerticalLine (static_cast<int> (x), 0.0f, h);

        juce::String label = (f < 1000.0f) ? juce::String (static_cast<int> (f))
                                           : juce::String (f / 1000.0f, 1) + "k";
        g.setColour (juce::Colour (0xff555566));
        g.drawText (label, static_cast<int> (x) - 16,
                    static_cast<int> (h) - 14, 32, 12,
                    juce::Justification::centred);
    }

    //--------------------------------------------------------------------------
    // "Delay ↑" / "Earlier ↓" corner labels
    //--------------------------------------------------------------------------
    g.setFont (9.5f);
    g.setColour (juce::Colour (0xff556677));
    g.drawText ("later",   4, 4,  80, 12, juce::Justification::left);
    g.drawText ("earlier", 4, static_cast<int> (h) - 18, 80, 12,
                juce::Justification::left);
}

//==============================================================================
void SpectralLatencyAudioProcessorEditor::drawSpectrum (juce::Graphics& g)
{
    const float plotW  = static_cast<float> (getWidth() - kRightPanelWidth);
    const float plotH  = static_cast<float> (getHeight());

    // Bars grow upward from the bottom; cap at 45 % of window height so they
    // sit below the centre line and don't crowd the delay-curve area.
    const float maxBarH = plotH * 0.45f;
    const float baseY   = plotH;   // bars start at the very bottom

    for (int bin = 1; bin < audioProcessor.numBins; ++bin)
    {
        float freq = static_cast<float> (bin) *
                     (audioProcessor.currentSampleRate * 0.5f) /
                     static_cast<float> (audioProcessor.numBins - 1);

        if (freq < 20.0f) continue;

        float x0 = freqToX (freq);
        if (x0 >= plotW) break;

        // Bar width = distance to the next bin's x-position (min 1 px)
        float nextFreq = static_cast<float> (bin + 1) *
                         (audioProcessor.currentSampleRate * 0.5f) /
                         static_cast<float> (audioProcessor.numBins - 1);
        float x1  = (bin + 1 < audioProcessor.numBins) ? freqToX (nextFreq) : plotW;
        float barW = juce::jmax (1.0f, x1 - x0 - 0.5f);

        // dB scale: floor at −80 dB, map to bar height
        float dB    = juce::Decibels::gainToDecibels (spectrum[bin], -80.0f);
        float normH = juce::jlimit (0.0f, 1.0f, (dB + 80.0f) / 80.0f);
        float barH  = normH * maxBarH;

        if (barH < 0.5f) continue;

        // Colour: blue at low level, brighter cyan-blue at high level
        float bright = normH;
        juce::Colour barColour = juce::Colour::fromHSV (0.58f, 0.7f, 0.25f + bright * 0.55f, 0.75f);

        g.setColour (barColour);
        g.fillRect (x0, baseY - barH, barW, barH);
    }
}

//==============================================================================
void SpectralLatencyAudioProcessorEditor::drawDelayCurve (juce::Graphics& g)
{
    const float plotW = static_cast<float> (getWidth() - kRightPanelWidth);
    const float zeroY = delayToY (0.0f);

    //--------------------------------------------------------------------------
    // Filled area between the curve and the centre (zero) line
    //--------------------------------------------------------------------------
    juce::Path fillPath;
    bool started = false;

    for (int bin = 0; bin < audioProcessor.numBins; ++bin)
    {
        float freq = static_cast<float> (bin) *
                     (audioProcessor.currentSampleRate * 0.5f) /
                     static_cast<float> (audioProcessor.numBins - 1);

        if (freq < 20.0f) continue;

        float x = freqToX (freq);
        if (x > plotW) break;

        float y = delayToY (delayCurve[bin]);

        if (! started)
        {
            fillPath.startNewSubPath (x, zeroY);
            fillPath.lineTo          (x, y);
            started = true;
        }
        else
        {
            fillPath.lineTo (x, y);
        }
    }

    if (started)
    {
        // Walk back along the zero line to close the shape
        for (int bin = audioProcessor.numBins - 1; bin >= 0; --bin)
        {
            float freq = static_cast<float> (bin) *
                         (audioProcessor.currentSampleRate * 0.5f) /
                         static_cast<float> (audioProcessor.numBins - 1);
            if (freq < 20.0f) break;

            float x = freqToX (freq);
            if (x > plotW) continue;

            fillPath.lineTo (x, zeroY);
            break;
        }
        fillPath.closeSubPath();
    }

    g.setColour (juce::Colours::cyan.withAlpha (0.12f));
    g.fillPath (fillPath);

    //--------------------------------------------------------------------------
    // Curve line
    //--------------------------------------------------------------------------
    juce::Path curvePath;
    started = false;

    for (int bin = 0; bin < audioProcessor.numBins; ++bin)
    {
        float freq = static_cast<float> (bin) *
                     (audioProcessor.currentSampleRate * 0.5f) /
                     static_cast<float> (audioProcessor.numBins - 1);

        if (freq < 20.0f) continue;

        float x = freqToX (freq);
        if (x > plotW) break;

        float y = delayToY (delayCurve[bin]);

        if (! started)
        {
            curvePath.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            curvePath.lineTo (x, y);
        }
    }

    g.setColour (juce::Colours::cyan);
    g.strokePath (curvePath, juce::PathStrokeType (2.5f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
}

//==============================================================================
void SpectralLatencyAudioProcessorEditor::drawTooltip (juce::Graphics& g,
                                                        float mouseX,
                                                        float mouseY)
{
    float freq = xToFreq (mouseX);
    int bin = juce::jlimit (0, audioProcessor.numBins - 1,
        static_cast<int> (freq /
                          (audioProcessor.currentSampleRate * 0.5f) *
                          (audioProcessor.numBins - 1)));

    float delayVal = delayCurve[bin];
    float maxLat   = audioProcessor.getMaxLatency();
    float ms       = delayVal * maxLat * 1000.0f;

    juce::String freqStr = (freq < 1000.0f)
        ? juce::String (static_cast<int> (freq)) + " Hz"
        : juce::String (freq / 1000.0f, 1) + " kHz";

    juce::String delayStr;
    if (std::abs (ms) >= 1000.0f)
        delayStr = juce::String (ms / 1000.0f, 2) + " s";
    else
        delayStr = juce::String (ms, 1) + " ms";
    if (ms > 0.0f) delayStr = "+" + delayStr;

    juce::String tooltip = freqStr + " " + delayStr;

    float labelX = mouseX + 12.0f;
    float labelY = mouseY - 22.0f;

    const float boxW = 168.0f;
    const float boxH = 18.0f;
    if (labelX + boxW > static_cast<float> (getWidth() - kRightPanelWidth))
        labelX = mouseX - boxW - 4.0f;
    if (labelY < 4.0f)
        labelY = mouseY + 8.0f;

    g.setColour (juce::Colour (0xd0101018));
    g.fillRoundedRectangle (labelX - 4.0f, labelY - 2.0f, boxW, boxH, 4.0f);
    g.setColour (juce::Colour (0xffddddee));
    g.setFont (11.0f);
    g.drawText (tooltip, static_cast<int> (labelX), static_cast<int> (labelY),
                static_cast<int> (boxW) - 4, 16, juce::Justification::left);
}

//==============================================================================
void SpectralLatencyAudioProcessorEditor::resized()
{
    const int w   = getWidth();
    const int h   = getHeight();
    const int rpX = w - kRightPanelWidth;

    //--------------------------------------------------------------------------
    // FFT size controls — sit at the very top of the right panel
    //--------------------------------------------------------------------------
    fftSizeLabel.setBounds (rpX - 70, 8, 64, 22);
    fftSizeCombo .setBounds (rpX,      8, kRightPanelWidth - 4, 22);

    randomizeButton.setBounds(rpX, 36, kRightPanelWidth - 4, 22);

    //--------------------------------------------------------------------------
    // Max-latency slider fills the rest of the right panel
    //--------------------------------------------------------------------------
    constexpr int labelH   = 18;
    constexpr int topGap   = 68;     // space below the FFT combo
    constexpr int bottomGap = 4;

    maxLatencyLabel .setBounds (rpX,     topGap,          kRightPanelWidth, labelH);
    maxLatencySlider.setBounds (rpX + 2, topGap + labelH, kRightPanelWidth - 4,
                                h - topGap - labelH - bottomGap);
}