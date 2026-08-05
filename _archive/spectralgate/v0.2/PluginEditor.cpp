#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralGateAudioProcessorEditor::SpectralGateAudioProcessorEditor(SpectralGateAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(800, 500);
    fftDisplayData.fill(0.0f);
    startTimerHz(60); // 60 FPS refresh
}

SpectralGateAudioProcessorEditor::~SpectralGateAudioProcessorEditor()
{
    stopTimer();
}

void SpectralGateAudioProcessorEditor::paint(juce::Graphics& g)
{
    // White background
    g.fillAll(juce::Colours::white);

    // Draw FFT
    drawFFT(g);

    auto bounds = getLocalBounds();
    float lowerFreqBin = audioProcessor.lowerFreqBin.load();
    float upperFreqBin = audioProcessor.upperFreqBin.load();
    float magThreshold = audioProcessor.magnitudeThreshold.load();

    // Calculate X positions for vertical bars (frequency bins)
    int lowerX = static_cast<int>(lowerFreqBin * bounds.getWidth());
    int upperX = static_cast<int>(upperFreqBin * bounds.getWidth());

    // Calculate Y position for horizontal bar (magnitude threshold)
    // magThreshold is now 0.0 to 1.0, where 1.0 is top (+6 dB) and 0.0 is bottom (-60 dB)
    // Map to display: -60 dB to +6 dB = 66 dB range
    int magY = bounds.getHeight() - static_cast<int>(magThreshold * bounds.getHeight());

    // Draw vertical frequency limit bars (cyan, thicker)
    g.setColour(juce::Colours::cyan);
    g.fillRect(lowerX - 4, 0, 8, bounds.getHeight());
    g.fillRect(upperX - 4, 0, 8, bounds.getHeight());

    // Draw horizontal magnitude threshold bar (cyan, thicker)
    g.fillRect(0, magY - 4, bounds.getWidth(), 8);

    // Draw labels
    g.setColour(juce::Colours::black);
    g.setFont(12.0f);

    // Calculate actual frequencies for the frequency bins
    const float nyquistFreq = 22050.0f; // Assuming 44.1 kHz sample rate
    const float freqPerBin = nyquistFreq / fftDisplaySize;

    // Lower frequency label with Hz value
    float lowerFreqHz = lowerFreqBin * fftDisplaySize * freqPerBin;
    juce::String lowerFreqLabel;
    if (lowerFreqHz < 1000.0f)
        lowerFreqLabel = juce::String(static_cast<int>(lowerFreqHz)) + " Hz";
    else
        lowerFreqLabel = juce::String(lowerFreqHz / 1000.0f, 1) + " kHz";

    if (lowerX > 10)
        g.drawText(lowerFreqLabel, lowerX - 60, 10, 120, 20, juce::Justification::centred);

    // Upper frequency label with Hz value
    float upperFreqHz = upperFreqBin * fftDisplaySize * freqPerBin;
    juce::String upperFreqLabel;
    if (upperFreqHz < 1000.0f)
        upperFreqLabel = juce::String(static_cast<int>(upperFreqHz)) + " Hz";
    else
        upperFreqLabel = juce::String(upperFreqHz / 1000.0f, 1) + " kHz";

    if (upperX < bounds.getWidth() - 10)
        g.drawText(upperFreqLabel, upperX - 60, 10, 120, 20, juce::Justification::centred);

    // Magnitude threshold label (now shows -60 dB to +6 dB range)
    int labelY = magY - 20;
    if (labelY < 10)
        labelY = magY + 5;

    float magDB = (magThreshold * 66.0f - 60.0f);  // Map 0-1 to -60 to +6 dB
    juce::String magLabel = juce::String(magDB, 1) + " dB";
    g.drawText(magLabel, 10, labelY, 100, 20, juce::Justification::left);
}

void SpectralGateAudioProcessorEditor::drawFFT(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    fftArea = bounds;

    // Get FFT data from processor
    audioProcessor.getFFTData(fftDisplayData.data(), fftDisplaySize);

    // Draw grid lines first (so they appear behind the spectrum)
    g.setColour(juce::Colour(0xffe0e0e0)); // Faint grey

    // Frequency grid lines at 10 Hz, 100 Hz, 1000 Hz, 10000 Hz
    // Assuming Nyquist frequency is around 22050 Hz (44.1 kHz sample rate)
    // Each bin represents: sampleRate / fftSize frequency range
    // For visualization, map frequency to horizontal position

    const float nyquistFreq = 22050.0f; // Assuming 44.1 kHz sample rate
    const float freqPerBin = nyquistFreq / fftDisplaySize;

    // Draw vertical frequency grid lines
    float gridFreqs[] = { 10.0f, 100.0f, 1000.0f, 10000.0f };
    for (float freq : gridFreqs)
    {
        int binIndex = static_cast<int>(freq / freqPerBin);
        if (binIndex >= 0 && binIndex < fftDisplaySize)
        {
            float x = (static_cast<float>(binIndex) / fftDisplaySize) * bounds.getWidth();
            g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(bounds.getHeight()));
        }
    }

    // Draw horizontal dB grid lines at 0 dB and -32 dB
    // Map dB to vertical position (range is -60 dB to +6 dB)
    auto dbToY = [&](float dB) -> int {
        float normalized = (dB + 60.0f) / 66.0f; // Map to 0-1
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

    // Draw FFT bars
    float barWidth = static_cast<float>(bounds.getWidth()) / fftDisplaySize;

    // Light green color (instead of light blue-grey)
    g.setColour(juce::Colour(0xffa0d0b0));

    for (int i = 0; i < fftDisplaySize; ++i)
    {
        float magnitude = fftDisplayData[i];

        // Convert to dB scale
        float dB = 20.0f * std::log10(magnitude / maxLevel + 0.00001f);

        // Normalize to 0.0 to 1.0 (now using -60 dB to +6 dB range = 66 dB total)
        float normalized = (dB + 60.0f) / 66.0f;
        normalized = juce::jlimit(0.0f, 1.0f, normalized);

        int barHeight = static_cast<int>(normalized * bounds.getHeight());
        float x = i * barWidth;
        int y = bounds.getHeight() - barHeight;

        g.fillRect(static_cast<int>(x), y, juce::jmax(1, static_cast<int>(barWidth)), barHeight);
    }
}

void SpectralGateAudioProcessorEditor::resized()
{
    fftArea = getLocalBounds();
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

    float lowerFreqBin = audioProcessor.lowerFreqBin.load();
    float upperFreqBin = audioProcessor.upperFreqBin.load();
    float magThreshold = audioProcessor.magnitudeThreshold.load();

    // Calculate positions
    int lowerX = static_cast<int>(lowerFreqBin * bounds.getWidth());
    int upperX = static_cast<int>(upperFreqBin * bounds.getWidth());
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

    // Update the appropriate control
    if (draggedControl == LowerFreq)
    {
        float normalized = juce::jlimit(0.0f, 1.0f,
            static_cast<float>(event.x) / bounds.getWidth());
        audioProcessor.lowerFreqBin = normalized;
    }
    else if (draggedControl == UpperFreq)
    {
        float normalized = juce::jlimit(0.0f, 1.0f,
            static_cast<float>(event.x) / bounds.getWidth());
        audioProcessor.upperFreqBin = normalized;
    }
    else if (draggedControl == Magnitude)
    {
        // Invert Y (0 at bottom, 1 at top)
        float normalized = juce::jlimit(0.0f, 1.0f,
            1.0f - static_cast<float>(event.y) / bounds.getHeight());
        audioProcessor.magnitudeThreshold = normalized;
    }
}