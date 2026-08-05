#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Colour palette
//==============================================================================
static const juce::Colour kBackground       = juce::Colour(0xffffffff);  // white
static const juce::Colour kGridLine         = juce::Colour(0xff1e2a35);  // dark teal-grey
static const juce::Colour kBarBoost         = juce::Colour(0xff4ff3a1);  // teal-green (boosted)
static const juce::Colour kBarNormal        = juce::Colour(0xff2a7a9c);  // muted blue (unaffected)
static const juce::Colour kBarOutside       = juce::Colour(0xff1e3a4a);  // dark blue (outside range)
static const juce::Colour kThresholdLine    = juce::Colour(0xff00dd77);  // blue threshold
static const juce::Colour kFreqBar          = juce::Colour(0xff00dddd);  // cyan freq markers
static const juce::Colour kTextColour       = juce::Colour(0xffe0e8f0);  // light grey text
static const juce::Colour kAttenuateBar     = juce::Colour(0xffef5350);  // red (attenuated bins)

//==============================================================================
SpectralEnhanceAudioProcessorEditor::SpectralEnhanceAudioProcessorEditor(SpectralEnhanceAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(800, 500);
    fftDisplayData.fill(0.0f);

    // Attenuate toggle button
    attenuateButton.setButtonText("Attenuate");
    attenuateButton.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff1e2a35));
    attenuateButton.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xffef5350));
    attenuateButton.setColour(juce::TextButton::textColourOffId,   kTextColour);
    attenuateButton.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
    attenuateButton.setClickingTogglesState(true);

    attenuateButton.onClick = [this]() {
        auto* param = audioProcessor.parameters.getParameter("attenuate");
        if (param != nullptr)
        {
            float newValue = attenuateButton.getToggleState() ? 1.0f : 0.0f;
            param->setValueNotifyingHost(newValue);
        }
    };

    addAndMakeVisible(attenuateButton);

    // Slope knob
    slopeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slopeSlider.setRange(-1.0, 1.0, 0.01);
    slopeSlider.setValue(0.0);
    slopeSlider.setDoubleClickReturnValue(true, 0.0);
    slopeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slopeSlider.setColour(juce::Slider::rotarySliderFillColourId,  kThresholdLine);
    slopeSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff1e2a35));
    slopeSlider.setColour(juce::Slider::thumbColourId,             kThresholdLine);

    slopeSlider.onValueChange = [this]() {
        auto* param = audioProcessor.parameters.getParameter("slope");
        if (param != nullptr)
        {
            float normalizedValue = (static_cast<float>(slopeSlider.getValue()) + 1.0f) / 2.0f;
            param->setValueNotifyingHost(normalizedValue);
        }
    };
    addAndMakeVisible(slopeSlider);

    slopeLabel.setText("Slope", juce::dontSendNotification);
    slopeLabel.setJustificationType(juce::Justification::centred);
    slopeLabel.setColour(juce::Label::textColourId, kTextColour);
    addAndMakeVisible(slopeLabel);

    startTimerHz(60);
}

SpectralEnhanceAudioProcessorEditor::~SpectralEnhanceAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SpectralEnhanceAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);

    drawFFT(g);

    auto bounds = getLocalBounds();
    float lowerFreqBin = audioProcessor.getLowerFreqBin();
    float upperFreqBin = audioProcessor.getUpperFreqBin();
    float magThreshold = audioProcessor.getMagnitudeThreshold();
    float slopeValue   = audioProcessor.getSlope();   // -1 to +1
    bool  attenuate    = audioProcessor.isAttenuateMode();

    // Sync controls to parameter values (handles automation)
    attenuateButton.setToggleState(attenuate, juce::dontSendNotification);
    slopeSlider.setValue(slopeValue, juce::dontSendNotification);

    // Frequency / log-scale helpers
    const float nyquistFreq = 22050.0f;
    const float minFreq     = 20.0f;
    const float logMin      = std::log10(minFreq);
    const float logMax      = std::log10(nyquistFreq);

    auto binToFreq = [&](float bin) -> float {
        return std::pow(10.0f, logMin + bin * (logMax - logMin));
    };

    auto freqToX = [&](float freq) -> int {
        float lf = std::log10(juce::jmax(freq, minFreq));
        float n  = (lf - logMin) / (logMax - logMin);
        return static_cast<int>(n * bounds.getWidth());
    };

    // Frequency boundary bars
    float lowerFreq = binToFreq(lowerFreqBin);
    float upperFreq = binToFreq(upperFreqBin);
    int   lowerX    = freqToX(lowerFreq);
    int   upperX    = freqToX(upperFreq);

    // Shaded active region overlay
    g.setColour(attenuate
        ? juce::Colour(0x22ef5350)   // subtle red tint in attenuate mode
        : juce::Colour(0x224fc3a1)); // subtle teal tint in boost mode
    g.fillRect(lowerX, 0, upperX - lowerX, bounds.getHeight());

    // Vertical frequency markers
    g.setColour(kFreqBar);
    g.fillRect(lowerX - 2, 0, 4, bounds.getHeight());
    g.fillRect(upperX - 2, 0, 4, bounds.getHeight());

    // Sloped threshold line
    float baseThresholdDB  = magThreshold * 63.0f - 60.0f;
    const float slopeRangeDB = 30.0f;

    float leftThresholdDB  = baseThresholdDB + slopeValue * slopeRangeDB * (-0.5f);
    float rightThresholdDB = baseThresholdDB + slopeValue * slopeRangeDB * ( 0.5f);

    auto dbToNormalized = [](float db) -> float {
        return (db + 60.0f) / 63.0f;
    };

    int leftY  = bounds.getHeight() - static_cast<int>(dbToNormalized(leftThresholdDB)  * bounds.getHeight());
    int rightY = bounds.getHeight() - static_cast<int>(dbToNormalized(rightThresholdDB) * bounds.getHeight());

    g.setColour(kThresholdLine);
    for (int offset = -2; offset <= 2; ++offset)
        g.drawLine(0, leftY + offset, bounds.getWidth(), rightY + offset, 1.0f);

    // Labels
    g.setFont(juce::Font(11.0f));
    g.setColour(kTextColour);

    // Lower freq label
    juce::String lowerFreqLabel = (lowerFreq < 1000.0f)
        ? juce::String(static_cast<int>(lowerFreq)) + " Hz"
        : juce::String(lowerFreq / 1000.0f, 1) + " kHz";
    if (lowerX > 10)
        g.drawText(lowerFreqLabel, lowerX - 60, 10, 120, 18, juce::Justification::centred);

    // Upper freq label
    juce::String upperFreqLabel = (upperFreq < 1000.0f)
        ? juce::String(static_cast<int>(upperFreq)) + " Hz"
        : juce::String(upperFreq / 1000.0f, 1) + " kHz";
    if (upperX < bounds.getWidth() - 10)
        g.drawText(upperFreqLabel, upperX - 60, 10, 120, 18, juce::Justification::centred);

    // Threshold dB label
    int centerY = (leftY + rightY) / 2;
    int labelY  = centerY - 20;
    if (labelY < 10) labelY = centerY + 5;
    juce::String magLabel = juce::String(baseThresholdDB, 1) + " dB";
    g.drawText(magLabel, 10, labelY, 100, 18, juce::Justification::left);

    // Mode label (bottom-left)
    g.setFont(juce::Font(13.0f).boldened());
    g.setColour(attenuate ? juce::Colour(0xffef5350) : kBarBoost);
    g.drawText(attenuate ? "ATTENUATE" : "BOOST", 10, bounds.getHeight() - 24, 120, 18,
               juce::Justification::left);

    // Plugin name (top-left)
    g.setFont(juce::Font(14.0f).boldened());
    g.setColour(kThresholdLine);
    g.drawText("SpectralEnhance", 10, 10, 160, 18, juce::Justification::left);
}

//==============================================================================
void SpectralEnhanceAudioProcessorEditor::drawFFT(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    fftArea = bounds;

    audioProcessor.getFFTData(fftDisplayData.data(), fftDisplaySize);

    float lowerFreqBin = audioProcessor.getLowerFreqBin();
    float upperFreqBin = audioProcessor.getUpperFreqBin();
    float magThreshold = audioProcessor.getMagnitudeThreshold();
    float slopeValue   = audioProcessor.getSlope();
    bool  attenuate    = audioProcessor.isAttenuateMode();

    const float nyquistFreq = 22050.0f;
    const float minFreq     = 20.0f;
    const float logMin      = std::log10(minFreq);
    const float logMax      = std::log10(nyquistFreq);

    auto freqToX = [&](float freq) -> float {
        float lf = std::log10(juce::jmax(freq, minFreq));
        return ((lf - logMin) / (logMax - logMin)) * bounds.getWidth();
    };

    // Grid lines
    g.setColour(kGridLine);
    float gridFreqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    for (float freq : gridFreqs)
        g.drawVerticalLine(static_cast<int>(freqToX(freq)), 0.0f, static_cast<float>(bounds.getHeight()));

    auto dbToY = [&](float dB) -> int {
        float normalized = (dB + 60.0f) / 63.0f;
        return bounds.getHeight() - static_cast<int>(normalized * bounds.getHeight());
    };
    g.drawHorizontalLine(dbToY(  0.0f), 0.0f, static_cast<float>(bounds.getWidth()));
    g.drawHorizontalLine(dbToY(-20.0f), 0.0f, static_cast<float>(bounds.getWidth()));
    g.drawHorizontalLine(dbToY(-40.0f), 0.0f, static_cast<float>(bounds.getWidth()));

    // Grid labels
    g.setFont(juce::Font(9.0f));
    g.setColour(kGridLine.brighter(0.5f));
    g.drawText("0dB",   2, dbToY(  0.0f) + 2, 30, 12, juce::Justification::left);
    g.drawText("-20",   2, dbToY(-20.0f) + 2, 30, 12, juce::Justification::left);
    g.drawText("-40",   2, dbToY(-40.0f) + 2, 30, 12, juce::Justification::left);
    g.drawText("100",   static_cast<int>(freqToX(100.0f))   + 2, bounds.getHeight() - 14, 30, 12, juce::Justification::left);
    g.drawText("1k",    static_cast<int>(freqToX(1000.0f))  + 2, bounds.getHeight() - 14, 20, 12, juce::Justification::left);
    g.drawText("10k",   static_cast<int>(freqToX(10000.0f)) + 2, bounds.getHeight() - 14, 30, 12, juce::Justification::left);

    // FFT bin metrics
    float maxLevel = 0.001f;
    for (int i = 0; i < fftDisplaySize; ++i)
        maxLevel = juce::jmax(maxLevel, fftDisplayData[i]);

    auto binToFreq = [&](float t) -> float {
        return std::pow(10.0f, logMin + t * (logMax - logMin));
    };

    float lowerFreq  = binToFreq(lowerFreqBin);
    float upperFreq  = binToFreq(upperFreqBin);
    const float freqPerBin = nyquistFreq / fftDisplaySize;
    int lowerBinIndex = static_cast<int>(lowerFreq / freqPerBin);
    int upperBinIndex = static_cast<int>(upperFreq / freqPerBin);

    float baseThresholdDB  = magThreshold * 63.0f - 60.0f;
    const float slopeRangeDB = 30.0f;
    int numBins = fftDisplaySize;

    for (int i = 0; i < fftDisplaySize; ++i)
    {
        float magnitude = fftDisplayData[i];
        float binFreq   = i * freqPerBin;
        if (binFreq < minFreq) binFreq = minFreq;

        // Per-bin threshold
        float binPosition    = static_cast<float>(i) / (numBins - 1);
        float slopeAdjustDB  = slopeValue * slopeRangeDB * (binPosition - 0.5f);
        float binThresholdDB = baseThresholdDB + slopeAdjustDB;
        float thresholdMag   = maxLevel * std::pow(10.0f, binThresholdDB / 20.0f);

        bool insideRange = (i >= lowerBinIndex && i <= upperBinIndex);

        // Colour logic mirrors the processor:
        juce::Colour barColour;
        if (!insideRange)
        {
            barColour = kBarOutside;
        }
        else if (!attenuate)
        {
            // Boost mode — below threshold = will be boosted (highlight)
            barColour = (magnitude < thresholdMag) ? kBarBoost : kBarNormal;
        }
        else
        {
            // Attenuate mode — above threshold = will be attenuated (highlight)
            barColour = (magnitude > thresholdMag) ? kAttenuateBar : kBarNormal;
        }

        g.setColour(barColour);

        float dB         = 20.0f * std::log10(magnitude / maxLevel + 0.00001f);
        float normalized = juce::jlimit(0.0f, 1.0f, (dB + 60.0f) / 63.0f);
        int   barHeight  = static_cast<int>(normalized * bounds.getHeight());

        float x1       = freqToX(binFreq);
        float x2       = freqToX((i + 1) * freqPerBin);
        float barWidth = x2 - x1;
        int   y        = bounds.getHeight() - barHeight;

        g.fillRect(static_cast<int>(x1), y, juce::jmax(1, static_cast<int>(barWidth)), barHeight);
    }
}

//==============================================================================
void SpectralEnhanceAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    attenuateButton.setBounds(bounds.getWidth() - 100, 10, 88, 28);
    slopeSlider.setBounds(bounds.getWidth() - 100, 48, 88, 88);
    slopeLabel.setBounds(bounds.getWidth() - 100, 138, 88, 18);

    fftArea = bounds;
}

void SpectralEnhanceAudioProcessorEditor::timerCallback()
{
    repaint();
}

//==============================================================================
void SpectralEnhanceAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    updateControlFromMouse(event);
}

void SpectralEnhanceAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    updateControlFromMouse(event);
}

void SpectralEnhanceAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    draggedControl = None;
}

void SpectralEnhanceAudioProcessorEditor::updateControlFromMouse(const juce::MouseEvent& event)
{
    auto bounds = getLocalBounds();

    float lowerFreqBin = audioProcessor.getLowerFreqBin();
    float upperFreqBin = audioProcessor.getUpperFreqBin();
    float magThreshold = audioProcessor.getMagnitudeThreshold();

    const float nyquistFreq = 22050.0f;
    const float minFreq     = 20.0f;
    const float logMin      = std::log10(minFreq);
    const float logMax      = std::log10(nyquistFreq);

    auto binToFreq = [&](float bin) -> float {
        return std::pow(10.0f, logMin + bin * (logMax - logMin));
    };

    auto freqToX = [&](float freq) -> int {
        float lf = std::log10(juce::jmax(freq, minFreq));
        float n  = (lf - logMin) / (logMax - logMin);
        return static_cast<int>(n * bounds.getWidth());
    };

    auto xToBin = [&](int x) -> float {
        float n      = static_cast<float>(x) / bounds.getWidth();
        float logFreq = logMin + n * (logMax - logMin);
        float binPos  = (logFreq - logMin) / (logMax - logMin);
        return juce::jlimit(0.0f, 1.0f, binPos);
    };

    int lowerX = freqToX(binToFreq(lowerFreqBin));
    int upperX = freqToX(binToFreq(upperFreqBin));
    int magY   = bounds.getHeight() - static_cast<int>(magThreshold * bounds.getHeight());

    if (draggedControl == None)
    {
        int distToLowerX = std::abs(event.x - lowerX);
        int distToUpperX = std::abs(event.x - upperX);
        int distToMagY   = std::abs(event.y - magY);

        if (distToMagY < 15)
            draggedControl = Magnitude;
        else if (distToLowerX < 15 && distToLowerX < distToUpperX)
            draggedControl = LowerFreq;
        else if (distToUpperX < 15)
            draggedControl = UpperFreq;
    }

    if (draggedControl == LowerFreq)
    {
        auto* param = audioProcessor.parameters.getParameter("lowerFreq");
        if (param != nullptr)
            param->setValueNotifyingHost(xToBin(event.x));
    }
    else if (draggedControl == UpperFreq)
    {
        auto* param = audioProcessor.parameters.getParameter("upperFreq");
        if (param != nullptr)
            param->setValueNotifyingHost(xToBin(event.x));
    }
    else if (draggedControl == Magnitude)
    {
        float normalized = juce::jlimit(0.0f, 1.0f,
            1.0f - static_cast<float>(event.y) / bounds.getHeight());
        auto* param = audioProcessor.parameters.getParameter("magnitude");
        if (param != nullptr)
            param->setValueNotifyingHost(normalized);
    }
}
