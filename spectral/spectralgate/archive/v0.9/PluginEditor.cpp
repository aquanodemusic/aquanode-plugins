#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralGateAudioProcessorEditor::SpectralGateAudioProcessorEditor(SpectralGateAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(800, 500);
    fftDisplayData.fill(0.0f);

    // Setup invert button
    invertButton.setButtonText("Invert");
    invertButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    invertButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
    invertButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    invertButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    invertButton.setClickingTogglesState(true);

    invertButton.onClick = [this]() {
        auto* param = audioProcessor.parameters.getParameter("invert");
        if (param != nullptr)
        {
            float newValue = invertButton.getToggleState() ? 1.0f : 0.0f;
            param->setValueNotifyingHost(newValue);
        }
        };

    addAndMakeVisible(invertButton);

    // Setup slope slider
    slopeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slopeSlider.setRange(-1.0, 1.0, 0.01);
    slopeSlider.setValue(0.0);
    slopeSlider.setDoubleClickReturnValue(true, 0.5);  // Enable double-click reset to center
    slopeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slopeSlider.onValueChange = [this]() {
        auto* param = audioProcessor.parameters.getParameter("slope");
        if (param != nullptr)
        {
            float normalizedValue = (slopeSlider.getValue() + 1.0f) / 2.0f; // Map -1 to 1 -> 0 to 1
            param->setValueNotifyingHost(normalizedValue);
        }
        };
    addAndMakeVisible(slopeSlider);

    // Setup slope label
    slopeLabel.setText("Slope", juce::dontSendNotification);
    slopeLabel.setJustificationType(juce::Justification::centred);
    slopeLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    addAndMakeVisible(slopeLabel);

    startTimerHz(60); // 60 FPS refresh
}

SpectralGateAudioProcessorEditor::~SpectralGateAudioProcessorEditor()
{
    stopTimer();
}

void SpectralGateAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xffffffff));

    // Draw FFT
    drawFFT(g);

    auto bounds = getLocalBounds();
    float lowerFreqBin = audioProcessor.getLowerFreqBin();
    float upperFreqBin = audioProcessor.getUpperFreqBin();
    float magThreshold = audioProcessor.getMagnitudeThreshold();
    float slopeValue = audioProcessor.getSlope();  // Already -1 to +1

    // Update button state to match parameter (for automation)
    invertButton.setToggleState(audioProcessor.isInverted(), juce::dontSendNotification);

    // Update slider to match parameter (for automation)
    slopeSlider.setValue(slopeValue, juce::dontSendNotification);

    // Logarithmic frequency scale helpers
    const float nyquistFreq = 22050.0f;
    const float minFreq = 20.0f;
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(nyquistFreq);

    // Convert bin position (0-1) to frequency using logarithmic scale
    auto binToFreq = [&](float bin) -> float {
        float logFreq = logMin + bin * (logMax - logMin);
        return std::pow(10.0f, logFreq);
        };

    // Convert frequency to X position using logarithmic scale
    auto freqToX = [&](float freq) -> int {
        float logFreq = std::log10(juce::jmax(freq, minFreq));
        float normalized = (logFreq - logMin) / (logMax - logMin);
        return static_cast<int>(normalized * bounds.getWidth());
        };

    // Calculate X positions for vertical bars (logarithmic frequency)
    float lowerFreq = binToFreq(lowerFreqBin);
    float upperFreq = binToFreq(upperFreqBin);
    int lowerX = freqToX(lowerFreq);
    int upperX = freqToX(upperFreq);

    // Draw vertical frequency limit bars (cyan, thicker)
    g.setColour(juce::Colours::cyan);
    g.fillRect(lowerX - 4, 0, 8, bounds.getHeight());
    g.fillRect(upperX - 4, 0, 8, bounds.getHeight());

    // Draw sloped threshold line
    // Base threshold in dB: -60 dB to +3 dB (63 dB range)
    float baseThresholdDB = (magThreshold * 63.0f - 60.0f);
    const float slopeRangeDB = 30.0f;

    // Calculate Y positions at left (x=0) and right (x=width)
    // At position 0.0 (left): threshold - slope * range / 2
    // At position 1.0 (right): threshold + slope * range / 2
    float leftThresholdDB = baseThresholdDB + slopeValue * slopeRangeDB * (-0.5f);
    float rightThresholdDB = baseThresholdDB + slopeValue * slopeRangeDB * (0.5f);

    // Convert to Y coordinates (0.0 to 1.0, where 1.0 is top +3 dB, 0.0 is bottom -60 dB)
    auto dbToNormalized = [](float db) -> float {
        return (db + 60.0f) / 63.0f;
        };

    int leftY = bounds.getHeight() - static_cast<int>(dbToNormalized(leftThresholdDB) * bounds.getHeight());
    int rightY = bounds.getHeight() - static_cast<int>(dbToNormalized(rightThresholdDB) * bounds.getHeight());

    // Draw thick sloped line
    g.setColour(juce::Colours::cyan);
    for (int offset = -4; offset <= 4; ++offset)
    {
        g.drawLine(0, leftY + offset, bounds.getWidth(), rightY + offset, 1.0f);
    }

    // Draw labels
    g.setColour(juce::Colours::black);
    g.setFont(12.0f);

    // Lower frequency label with Hz value
    juce::String lowerFreqLabel;
    if (lowerFreq < 1000.0f)
        lowerFreqLabel = juce::String(static_cast<int>(lowerFreq)) + " Hz";
    else
        lowerFreqLabel = juce::String(lowerFreq / 1000.0f, 1) + " kHz";

    if (lowerX > 10)
        g.drawText(lowerFreqLabel, lowerX - 60, 10, 120, 20, juce::Justification::centred);

    // Upper frequency label with Hz value
    juce::String upperFreqLabel;
    if (upperFreq < 1000.0f)
        upperFreqLabel = juce::String(static_cast<int>(upperFreq)) + " Hz";
    else
        upperFreqLabel = juce::String(upperFreq / 1000.0f, 1) + " kHz";

    if (upperX < bounds.getWidth() - 10)
        g.drawText(upperFreqLabel, upperX - 60, 10, 120, 20, juce::Justification::centred);

    // Magnitude threshold label at center of screen
    int centerY = (leftY + rightY) / 2;
    int labelY = centerY - 20;
    if (labelY < 10)
        labelY = centerY + 5;

    juce::String magLabel = juce::String(baseThresholdDB, 1) + " dB";
    g.drawText(magLabel, 10, labelY, 100, 20, juce::Justification::left);
}

void SpectralGateAudioProcessorEditor::drawFFT(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    fftArea = bounds;

    // Get FFT data from processor
    audioProcessor.getFFTData(fftDisplayData.data(), fftDisplaySize);

    // Get current parameters
    float lowerFreqBin = audioProcessor.getLowerFreqBin();
    float upperFreqBin = audioProcessor.getUpperFreqBin();
    float magThreshold = audioProcessor.getMagnitudeThreshold();
    float slopeValue = audioProcessor.getSlope();  // Already -1 to +1
    bool invert = audioProcessor.isInverted();

    // Draw grid lines first (so they appear behind the spectrum)
    g.setColour(juce::Colour(0xffc0d5e0)); // Slightly darker cyan for grid

    // Frequency grid lines at 10 Hz, 100 Hz, 1000 Hz, 10000 Hz
    const float nyquistFreq = 22050.0f; // Assuming 44.1 kHz sample rate
    const float minFreq = 20.0f; // Minimum frequency for log scale

    // Helper lambda to convert frequency to logarithmic X position
    auto freqToX = [&](float freq) -> float {
        float logMin = std::log10(minFreq);
        float logMax = std::log10(nyquistFreq);
        float logFreq = std::log10(freq);
        float normalized = (logFreq - logMin) / (logMax - logMin);
        return normalized * bounds.getWidth();
        };

    // Draw vertical frequency grid lines
    float gridFreqs[] = { 10.0f, 100.0f, 1000.0f, 10000.0f };
    for (float freq : gridFreqs)
    {
        float x = freqToX(freq);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(bounds.getHeight()));
    }

    // Draw horizontal dB grid lines at 0 dB and -32 dB
    // Map dB to vertical position (range is -60 dB to +3 dB)
    auto dbToY = [&](float dB) -> int {
        float normalized = (dB + 60.0f) / 63.0f; // Map to 0-1
        return bounds.getHeight() - static_cast<int>(normalized * bounds.getHeight());
        };

    g.drawHorizontalLine(dbToY(0.0f), 0.0f, static_cast<float>(bounds.getWidth()));
    g.drawHorizontalLine(dbToY(-32.0f), 0.0f, static_cast<float>(bounds.getWidth()));

    // Find max value for normalization
    float maxLevel = 0.001f;
    for (int i = 0; i < fftDisplaySize; ++i)
    {
        maxLevel = juce::jmax(maxLevel, fftDisplayData[i]);
    }

    // Calculate frequency range for determining which bins are in/out of region
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(nyquistFreq);

    auto binToFreq = [&](float bin) -> float {
        float logFreq = logMin + bin * (logMax - logMin);
        return std::pow(10.0f, logFreq);
        };

    float lowerFreq = binToFreq(lowerFreqBin);
    float upperFreq = binToFreq(upperFreqBin);
    const float freqPerBin = nyquistFreq / fftDisplaySize;
    int lowerBinIndex = static_cast<int>(lowerFreq / freqPerBin);
    int upperBinIndex = static_cast<int>(upperFreq / freqPerBin);

    // Base threshold and slope parameters
    float baseThresholdDB = (magThreshold * 63.0f - 60.0f);
    const float slopeRangeDB = 30.0f;

    // Draw FFT with logarithmic frequency scale
    for (int i = 0; i < fftDisplaySize; ++i)
    {
        float magnitude = fftDisplayData[i];

        // Calculate frequency for this bin
        float binFreq = i * freqPerBin;
        if (binFreq < minFreq) binFreq = minFreq; // Clamp to minimum

        // Calculate per-bin threshold based on frequency position and slope
        float binPosition = static_cast<float>(i) / (fftDisplaySize - 1);
        float slopeAdjustmentDB = slopeValue * slopeRangeDB * (binPosition - 0.5f);
        float binThresholdDB = baseThresholdDB + slopeAdjustmentDB;
        float thresholdMagnitude = maxLevel * std::pow(10.0f, binThresholdDB / 20.0f);

        // Determine if this bin would be kept or gated
        bool insideFreqRange = (i >= lowerBinIndex && i <= upperBinIndex);
        bool aboveThreshold = (magnitude >= thresholdMagnitude);

        bool isKept;
        if (invert)
        {
            // Inverted mode: keep bins OUTSIDE the region that are ABOVE threshold
            isKept = !insideFreqRange && aboveThreshold;
        }
        else
        {
            // Normal mode: keep bins INSIDE the region that are ABOVE threshold
            isKept = insideFreqRange && aboveThreshold;
        }

        // Set color: green for kept bins, blue for gated bins
        if (isKept)
            g.setColour(juce::Colour(0xffa0d0b0)); // Light green
        else
            g.setColour(juce::Colour(0xff40b0d0)); // Light blue

        // Convert to dB scale
        float dB = 20.0f * std::log10(magnitude / maxLevel + 0.00001f);

        // Normalize to 0.0 to 1.0 (now using -60 dB to +3 dB range = 63 dB total)
        float normalized = (dB + 60.0f) / 63.0f;
        normalized = juce::jlimit(0.0f, 1.0f, normalized);

        int barHeight = static_cast<int>(normalized * bounds.getHeight());

        // Calculate logarithmic X positions for this bin and next bin
        float x1 = freqToX(binFreq);
        float x2 = freqToX((i + 1) * freqPerBin);
        float barWidth = x2 - x1;

        int y = bounds.getHeight() - barHeight;

        g.fillRect(static_cast<int>(x1), y, juce::jmax(1, static_cast<int>(barWidth)), barHeight);
    }
}

void SpectralGateAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Position invert button in top right
    invertButton.setBounds(bounds.getWidth() - 90, 10, 80, 30);

    // Position slope slider below invert button
    slopeSlider.setBounds(bounds.getWidth() - 90, 50, 80, 80);
    slopeLabel.setBounds(bounds.getWidth() - 90, 130, 80, 20);

    fftArea = bounds;
}

void SpectralGateAudioProcessorEditor::timerCallback()
{
    repaint();
}

void SpectralGateAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
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
}

void SpectralGateAudioProcessorEditor::updateControlFromMouse(const juce::MouseEvent& event)
{
    auto bounds = getLocalBounds();

    float lowerFreqBin = audioProcessor.getLowerFreqBin();
    float upperFreqBin = audioProcessor.getUpperFreqBin();
    float magThreshold = audioProcessor.getMagnitudeThreshold();

    // Logarithmic frequency scale helpers
    const float nyquistFreq = 22050.0f;
    const float minFreq = 20.0f;
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(nyquistFreq);

    // Convert bin position (0-1) to frequency
    auto binToFreq = [&](float bin) -> float {
        float logFreq = logMin + bin * (logMax - logMin);
        return std::pow(10.0f, logFreq);
        };

    // Convert frequency to X position
    auto freqToX = [&](float freq) -> int {
        float logFreq = std::log10(juce::jmax(freq, minFreq));
        float normalized = (logFreq - logMin) / (logMax - logMin);
        return static_cast<int>(normalized * bounds.getWidth());
        };

    // Convert X position to bin position (0-1)
    auto xToBin = [&](int x) -> float {
        float normalized = static_cast<float>(x) / bounds.getWidth();
        float logFreq = logMin + normalized * (logMax - logMin);
        float freq = std::pow(10.0f, logFreq);
        // Convert back to bin position
        float binPos = (logFreq - logMin) / (logMax - logMin);
        return juce::jlimit(0.0f, 1.0f, binPos);
        };

    // Calculate positions
    int lowerX = freqToX(binToFreq(lowerFreqBin));
    int upperX = freqToX(binToFreq(upperFreqBin));
    int magY = bounds.getHeight() - static_cast<int>(magThreshold * bounds.getHeight());

    // Determine which control to drag
    if (draggedControl == None)
    {
        int distToLowerX = std::abs(event.x - lowerX);
        int distToUpperX = std::abs(event.x - upperX);
        int distToMagY = std::abs(event.y - magY);

        // Check if close to horizontal magnitude bar
        if (distToMagY < 15)
        {
            draggedControl = Magnitude;
        }
        // Check if close to vertical frequency bars
        else if (distToLowerX < 15 && distToLowerX < distToUpperX)
        {
            draggedControl = LowerFreq;
        }
        else if (distToUpperX < 15)
        {
            draggedControl = UpperFreq;
        }
    }

    // Update the appropriate control through the parameter system
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
        // Invert Y (0 at bottom, 1 at top)
        float normalized = juce::jlimit(0.0f, 1.0f,
            1.0f - static_cast<float>(event.y) / bounds.getHeight());
        auto* param = audioProcessor.parameters.getParameter("magnitude");
        if (param != nullptr)
            param->setValueNotifyingHost(normalized);
    }
}