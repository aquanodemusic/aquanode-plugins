#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace Palette
{
    const juce::Colour background{ 0xffffffff };
    const juce::Colour controlBg{ 0xff0c0c18 };
    const juce::Colour grid{ 0xff252540 };
    const juce::Colour barKept{ 0xff4ec08a };   // green  – passes gate
    const juce::Colour barGated{ 0xff2e6ea0 };   // blue   – gated out
    const juce::Colour freqBar{ 0xff00d8d8 };   // cyan   – interval markers
    const juce::Colour gateCurve{ 0xfff5c518 };   // amber  – drawable threshold
    const juce::Colour labelText{ 0xffccddee };
    const juce::Colour gridLabel{ 0xff445566 };
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
SpectralGateAudioProcessorEditor::SpectralGateAudioProcessorEditor(SpectralGateAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(820, 560);

    // ── FFT size label ────────────────────────────────────────────────────────
    fftSizeLabel.setText("FFT Size", juce::dontSendNotification);
    fftSizeLabel.setJustificationType(juce::Justification::centredRight);
    fftSizeLabel.setColour(juce::Label::textColourId, Palette::labelText);
    addAndMakeVisible(fftSizeLabel);

    // ── FFT size combo ────────────────────────────────────────────────────────
    // ComboBox item IDs map order → id: order 9→id 1, 10→2, 11→3, 12→4
    fftSizeCombo.addItem("512", 1);
    fftSizeCombo.addItem("1024", 2);
    fftSizeCombo.addItem("2048", 3);
    fftSizeCombo.addItem("4096", 4);
    fftSizeCombo.setSelectedId(audioProcessor.getFFTOrder() - 8,
        juce::dontSendNotification);
    fftSizeCombo.onChange = [this]
        {
            int orderId = fftSizeCombo.getSelectedId();   // 1–4
            audioProcessor.setFFTOrder(orderId + 8);     // 9–12
            // Re-read in case setFFTOrder clamped the value
            fftSizeCombo.setSelectedId(audioProcessor.getFFTOrder() - 8,
                juce::dontSendNotification);
        };
    addAndMakeVisible(fftSizeCombo);

    // ── Helper: style a toggle button ────────────────────────────────────────
    auto styleToggle = [](juce::TextButton& btn)
        {
            btn.setClickingTogglesState(true);
            btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a40));
            btn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::darkorange);
            btn.setColour(juce::TextButton::textColourOffId, Palette::labelText);
            btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        };

    // ── Inv. Interval button ──────────────────────────────────────────────────
    // OFF → mute everything OUTSIDE [lower, upper]   (normal bandpass-style)
    // ON  → mute everything INSIDE  [lower, upper]   (notch-style)
    styleToggle(invertIntervalButton);
    invertIntervalButton.onClick = [this]
        {
            auto* param = audioProcessor.parameters.getParameter("invertInterval");
            if (param)
                param->setValueNotifyingHost(invertIntervalButton.getToggleState() ? 1.0f : 0.0f);
        };
    addAndMakeVisible(invertIntervalButton);

    // ── Inv. Gate button ──────────────────────────────────────────────────────
    // OFF → mute bins whose magnitude is BELOW the gate curve  (normal gate)
    // ON  → mute bins whose magnitude is ABOVE the gate curve  (ducking / inverted gate)
    styleToggle(invertGateButton);
    invertGateButton.onClick = [this]
        {
            auto* param = audioProcessor.parameters.getParameter("invertGate");
            if (param)
                param->setValueNotifyingHost(invertGateButton.getToggleState() ? 1.0f : 0.0f);
        };
    addAndMakeVisible(invertGateButton);

    startTimerHz(60);
}

SpectralGateAudioProcessorEditor::~SpectralGateAudioProcessorEditor()
{
    stopTimer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Timer — sync automation + repaint
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessorEditor::timerCallback()
{
    // Sync button states with parameters (for automation / preset recall)
    invertIntervalButton.setToggleState(audioProcessor.isIntervalInverted(),
        juce::dontSendNotification);
    invertGateButton.setToggleState(audioProcessor.isGateInverted(),
        juce::dontSendNotification);

    // Sync FFT size combo in case setStateInformation changed it
    fftSizeCombo.setSelectedId(audioProcessor.getFFTOrder() - 8,
        juce::dontSendNotification);

    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate helpers
// ─────────────────────────────────────────────────────────────────────────────
int SpectralGateAudioProcessorEditor::xToBinIndex(float x, int numBins) const
{
    if (numBins <= 1) return 0;
    const float logMin = std::log10(kMinFreq);
    const float logMax = std::log10(kNyquist);
    const float freqPerBin = kNyquist / (float)(numBins - 1);

    float relX = x - (float)fftArea.getX();
    float norm = juce::jlimit(0.0f, 1.0f, relX / (float)fftArea.getWidth());
    float logFreq = logMin + norm * (logMax - logMin);
    float freq = std::pow(10.0f, logFreq);
    return juce::jlimit(0, numBins - 1, (int)(freq / freqPerBin));
}

float SpectralGateAudioProcessorEditor::binIndexToX(int bin, int numBins) const
{
    if (numBins <= 1 || bin <= 0)
        return (float)fftArea.getX();

    const float logMin = std::log10(kMinFreq);
    const float logMax = std::log10(kNyquist);
    const float freqPerBin = kNyquist / (float)(numBins - 1);

    float freq = juce::jmax(kMinFreq, (float)bin * freqPerBin);
    float norm = (std::log10(freq) - logMin) / (logMax - logMin);
    return (float)fftArea.getX() + norm * (float)fftArea.getWidth();
}

float SpectralGateAudioProcessorEditor::yToGateValue(float y) const
{
    // The display maps -60 dB (bottom) … 0 dB (top) linearly.
    // The processor maps gateCurve value → threshold dB via: threshDB = val * 63 - 60
    // Invert that: val = (threshDB + 60) / 63
    //   bottom pixel → normalised 0 → -60 dB → val = 0   (pass all)
    //   top    pixel → normalised 1 →   0 dB → val = 60/63 ≈ 0.952 (gate ≥ peak)
    float relY = y - (float)fftArea.getY();
    float normalised = juce::jlimit(0.0f, 1.0f,
        1.0f - relY / (float)fftArea.getHeight());
    float dB = normalised * 60.0f - 60.0f;           // -60 … 0 dB
    return juce::jlimit(0.0f, 1.0f, (dB + 60.0f) / 63.0f);
}

float SpectralGateAudioProcessorEditor::gateValueToY(float val) const
{
    // Processor formula: threshDB = val * 63 - 60
    //   val = 0   → -60 dB → normalised 0 → bottom of display
    //   val ≈ 1   → +3 dB  → normalised > 1, clamped → top   of display
    // Display range is -60 … 0 dB, so values that push the threshold above 0 dB
    // (val > 60/63 ≈ 0.952) all map to the top pixel — correct: they gate everything.
    float threshDB = val * 63.0f - 60.0f;
    float normalised = juce::jlimit(0.0f, 1.0f, (threshDB + 60.0f) / 60.0f);
    return (float)fftArea.getBottom() - normalised * (float)fftArea.getHeight();
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFFT
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessorEditor::drawFFT(juce::Graphics& g)
{
    // Pull fresh data from the processor
    audioProcessor.getFFTData(fftDisplayData);
    audioProcessor.getGateCurve(gateCurveData);

    const int numBins = (int)fftDisplayData.size();
    if (numBins <= 1) return;

    const float w = (float)fftArea.getWidth();
    const float h = (float)fftArea.getHeight();
    const float logMin = std::log10(kMinFreq);
    const float logMax = std::log10(kNyquist);
    const float freqPerBin = kNyquist / (float)(numBins - 1);

    // ── Background ────────────────────────────────────────────────────────────
    g.setColour(Palette::background);
    g.fillRect(fftArea);

    // ── Frequency grid + labels ───────────────────────────────────────────────
    {
        const float gridFreqs[] = { 50.0f, 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f };
        g.setFont(10.0f);

        for (float freq : gridFreqs)
        {
            float gx = fftArea.getX() +
                ((std::log10(freq) - logMin) / (logMax - logMin)) * w;

            g.setColour(Palette::grid);
            g.drawVerticalLine((int)gx,
                (float)fftArea.getY(),
                (float)fftArea.getBottom());

            juce::String label = (freq >= 1000.0f)
                ? juce::String((int)(freq / 1000)) + "k"
                : juce::String((int)freq);
            g.setColour(Palette::gridLabel);
            g.drawText(label,
                (int)gx + 2, fftArea.getBottom() - 16, 30, 14,
                juce::Justification::left);
        }

        // dB guide lines at 0 dB and -32 dB
        auto dbToY = [&](float dB) -> float
            {
                float norm = juce::jlimit(0.0f, 1.0f, (dB + 60.0f) / 60.0f);
                return (float)fftArea.getBottom() - norm * h;
            };
        g.setColour(Palette::grid);
        g.drawHorizontalLine((int)dbToY(0.0f), (float)fftArea.getX(), (float)fftArea.getRight());
        g.drawHorizontalLine((int)dbToY(-32.0f), (float)fftArea.getX(), (float)fftArea.getRight());
        g.setColour(Palette::gridLabel);
        g.drawText(" 0 dB", fftArea.getX() + 2, (int)dbToY(0.0f) - 13, 40, 12, juce::Justification::left);
        g.drawText("-32 dB", fftArea.getX() + 2, (int)dbToY(-32.0f) - 13, 44, 12, juce::Justification::left);
    }

    // ── Gate-state info from processor ────────────────────────────────────────
    const bool invertInterval = audioProcessor.isIntervalInverted();
    const bool invertGate = audioProcessor.isGateInverted();

    // Mirror the processor's log-to-bin mapping for the interval markers
    auto logBinToFreq = [&](float b) -> float
        {
            return std::pow(10.0f, logMin + b * (logMax - logMin));
        };
    const int lowerBinIdx = juce::jlimit(0, numBins - 1,
        (int)(logBinToFreq(audioProcessor.getLowerFreqBin()) / freqPerBin));
    const int upperBinIdx = juce::jlimit(0, numBins - 1,
        (int)(logBinToFreq(audioProcessor.getUpperFreqBin()) / freqPerBin));

    // Max magnitude for relative gate-threshold evaluation
    float maxMag = 0.001f;
    for (float v : fftDisplayData)
        maxMag = std::max(maxMag, v);

    // ── Spectrum bars ─────────────────────────────────────────────────────────
    for (int b = 1; b < numBins; ++b)
    {
        float freq = (float)b * freqPerBin;
        if (freq < kMinFreq) continue;

        float x1 = fftArea.getX() +
            ((std::log10(freq) - logMin) / (logMax - logMin)) * w;
        float x2 = fftArea.getX() +
            ((std::log10(std::min(freq + freqPerBin, kNyquist)) - logMin)
                / (logMax - logMin)) * w;
        float barW = juce::jmax(1.0f, x2 - x1);

        float magnitude = fftDisplayData[b];

        // Replicate the processor's exact threshold formula so bar colours
        // match the audio gate decision:  thresh = maxMag * 10^((val*63-60)/20)
        float threshDB = (b < (int)gateCurveData.size())
            ? gateCurveData[b] * 63.0f - 60.0f
            : -60.0f;
        float gateThresh = maxMag * std::pow(10.0f, threshDB / 20.0f);

        // Replicate processor gate logic for colouring
        bool insideInterval = (b >= lowerBinIdx && b <= upperBinIdx);
        bool inActiveRegion = invertInterval ? !insideInterval : insideInterval;
        bool aboveThresh = (magnitude >= gateThresh);
        bool passesGate = invertGate ? !aboveThresh : aboveThresh;
        bool isKept = inActiveRegion && passesGate;

        g.setColour(isKept ? Palette::barKept : Palette::barGated);

        // dB magnitude → bar height
        float dB = 20.0f * std::log10(magnitude / maxMag + 1e-6f);
        float normalized = juce::jlimit(0.0f, 1.0f, (dB + 60.0f) / 60.0f);
        int   barH = (int)(normalized * h);

        g.fillRect((int)x1, fftArea.getBottom() - barH,
            (int)barW, barH);
    }

    // ── Gate curve (amber line) ───────────────────────────────────────────────
    if ((int)gateCurveData.size() == numBins)
    {
        juce::Path curvePath;
        bool started = false;

        for (int b = 1; b < numBins; ++b)
        {
            float freq = (float)b * freqPerBin;
            if (freq < kMinFreq) continue;

            float px = fftArea.getX() +
                ((std::log10(freq) - logMin) / (logMax - logMin)) * w;
            float py = gateValueToY(gateCurveData[b]);

            if (!started) { curvePath.startNewSubPath(px, py); started = true; }
            else            curvePath.lineTo(px, py);
        }

        g.setColour(Palette::gateCurve);
        g.strokePath(curvePath, juce::PathStrokeType(2.0f));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// paint
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessorEditor::paint(juce::Graphics& g)
{
    // ── Spectrum + gate curve ─────────────────────────────────────────────────
    drawFFT(g);

    // ── Interval frequency bars ───────────────────────────────────────────────
    const float logMin = std::log10(kMinFreq);
    const float logMax = std::log10(kNyquist);

    auto logBinToFreq = [&](float b) -> float
        {
            return std::pow(10.0f, logMin + b * (logMax - logMin));
        };
    auto formatFreq = [](float freq) -> juce::String
        {
            return (freq < 1000.0f)
                ? juce::String((int)freq) + " Hz"
                : juce::String(freq / 1000.0f, 1) + " kHz";
        };

    const float lowerBin = audioProcessor.getLowerFreqBin();
    const float upperBin = audioProcessor.getUpperFreqBin();
    const float lowerFreq = logBinToFreq(lowerBin);
    const float upperFreq = logBinToFreq(upperBin);

    const int lowerX = fftArea.getX() + (int)(lowerBin * (float)fftArea.getWidth());
    const int upperX = fftArea.getX() + (int)(upperBin * (float)fftArea.getWidth());

    g.setColour(Palette::freqBar.withAlpha(0.75f));
    g.fillRect(lowerX - 3, fftArea.getY(), 6, fftArea.getHeight());
    g.fillRect(upperX - 3, fftArea.getY(), 6, fftArea.getHeight());

    // Frequency labels just inside the bars
    g.setColour(Palette::freqBar);
    g.setFont(12.0f);
    g.drawText(formatFreq(lowerFreq), lowerX - 50, fftArea.getY() + 6, 100, 16,
        juce::Justification::centred);
    g.drawText(formatFreq(upperFreq), upperX - 50, fftArea.getY() + 6, 100, 16,
        juce::Justification::centred);

    // ── Control bar background ────────────────────────────────────────────────
    g.setColour(Palette::controlBg);
    g.fillRect(0, fftArea.getBottom(), getWidth(), getHeight() - fftArea.getBottom());

    // Dividing line
    g.setColour(Palette::grid);
    g.drawHorizontalLine(fftArea.getBottom(), 0.0f, (float)getWidth());

    // ── Usage hint ────────────────────────────────────────────────────────────
    g.setColour(Palette::gridLabel);
    g.setFont(10.0f);
    g.drawText("Draw gate curve: left-drag | Reset curve: right-click",
        fftArea.getX(), fftArea.getBottom() - 16, fftArea.getWidth(), 14,
        juce::Justification::centred);
}

// ─────────────────────────────────────────────────────────────────────────────
// resized
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Control strip at the bottom
    const int stripH = 56;
    controlArea = bounds.removeFromBottom(stripH);
    fftArea = bounds;   // everything above the strip

    // Layout controls in the strip
    auto strip = controlArea.reduced(10, 10);

    // Left cluster: FFT Size label + combo
    fftSizeLabel.setBounds(strip.removeFromLeft(62).withSizeKeepingCentre(62, 22));
    strip.removeFromLeft(4);
    fftSizeCombo.setBounds(strip.removeFromLeft(84).withSizeKeepingCentre(84, 26));

    strip.removeFromLeft(24);   // gap

    // Right cluster: two invert buttons
    invertIntervalButton.setBounds(strip.removeFromLeft(110).withSizeKeepingCentre(110, 28));
    strip.removeFromLeft(10);
    invertGateButton.setBounds(strip.removeFromLeft(100).withSizeKeepingCentre(100, 28));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse events
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    draggedControl = None;
    prevDragBin = -1;
    updateControlFromMouse(event);
}

void SpectralGateAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    updateControlFromMouse(event);
}

void SpectralGateAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    draggedControl = None;
    prevDragBin = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// updateControlFromMouse
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessorEditor::updateControlFromMouse(const juce::MouseEvent& event)
{
    // Right-click anywhere in the FFT area → reset gate curve to 0 (fully open)
    if (event.mods.isRightButtonDown())
    {
        if (fftArea.contains(event.getPosition()))
            audioProcessor.resetGateCurve(0.0f);
        return;
    }

    if (!fftArea.contains(event.getPosition()))
        return;

    const int numBins = audioProcessor.getNumBins();
    const int curBin = xToBinIndex((float)event.x, numBins);
    const float curVal = yToGateValue((float)event.y);

    // ── First contact: decide what we're dragging ─────────────────────────────
    if (draggedControl == None)
    {
        const float lowerBin = audioProcessor.getLowerFreqBin();
        const float upperBin = audioProcessor.getUpperFreqBin();
        const float lowerX = (float)fftArea.getX() + lowerBin * (float)fftArea.getWidth();
        const float upperX = (float)fftArea.getX() + upperBin * (float)fftArea.getWidth();

        const float distLower = std::abs((float)event.x - lowerX);
        const float distUpper = std::abs((float)event.x - upperX);

        if (distLower < 15.0f && distLower <= distUpper)
        {
            draggedControl = LowerFreq;
        }
        else if (distUpper < 15.0f)
        {
            draggedControl = UpperFreq;
        }
        else
        {
            // Free-hand gate-curve drawing
            draggedControl = GateCurve;
            prevDragBin = curBin;
            prevDragVal = curVal;
        }
    }

    // ── Act ───────────────────────────────────────────────────────────────────
    if (draggedControl == LowerFreq)
    {
        float norm = juce::jlimit(0.0f, 1.0f,
            ((float)event.x - (float)fftArea.getX()) / (float)fftArea.getWidth());
        auto* param = audioProcessor.parameters.getParameter("lowerFreq");
        if (param) param->setValueNotifyingHost(norm);
    }
    else if (draggedControl == UpperFreq)
    {
        float norm = juce::jlimit(0.0f, 1.0f,
            ((float)event.x - (float)fftArea.getX()) / (float)fftArea.getWidth());
        auto* param = audioProcessor.parameters.getParameter("upperFreq");
        if (param) param->setValueNotifyingHost(norm);
    }
    else if (draggedControl == GateCurve)
    {
        // Interpolate a straight segment from the previous sample point to the
        // current one — avoids gaps when the mouse moves quickly.
        if (prevDragBin >= 0)
            audioProcessor.setGateCurveSegment(prevDragBin, prevDragVal, curBin, curVal);
        else
            audioProcessor.setGateCurveSegment(curBin, curVal, curBin, curVal);

        prevDragBin = curBin;
        prevDragVal = curVal;
    }
}