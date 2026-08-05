#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralGateAudioProcessorEditor::SpectralGateAudioProcessorEditor (SpectralGateAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (800, 500);
    fftDisplayData.fill(0.0f);
    startTimerHz(60); // 60 FPS refresh
}

SpectralGateAudioProcessorEditor::~SpectralGateAudioProcessorEditor()
{
    stopTimer();
}

void SpectralGateAudioProcessorEditor::paint (juce::Graphics& g)
{
    // White background
    g.fillAll(juce::Colours::white);
    
    // Draw FFT
    drawFFT(g);
    
    auto bounds = getLocalBounds();
    float lowerFreqBin = audioProcessor.lowerFreqBin.load();
    float upperFreqBin = audioProcessor.upperFreqBin.load();
    float magThreshold = audioProcessor.magnitudeThreshold.load();
    
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
    
    // Light green color
    g.setColour(juce::Colour(0xffa0d0b0));
    
    // Draw FFT with logarithmic frequency scale
    const float freqPerBin = nyquistFreq / fftDisplaySize;
    
    for (int i = 0; i < fftDisplaySize; ++i)
    {
        float magnitude = fftDisplayData[i];
        
        // Calculate frequency for this bin
        float binFreq = i * freqPerBin;
        if (binFreq < minFreq) binFreq = minFreq; // Clamp to minimum
        
        // Convert to dB scale
        float dB = 20.0f * std::log10(magnitude / maxLevel + 0.00001f);
        
        // Normalize to 0.0 to 1.0 (now using -60 dB to +6 dB range = 66 dB total)
        float normalized = (dB + 60.0f) / 66.0f;
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
    
    // Update the appropriate control
    if (draggedControl == LowerFreq)
    {
        audioProcessor.lowerFreqBin = xToBin(event.x);
    }
    else if (draggedControl == UpperFreq)
    {
        audioProcessor.upperFreqBin = xToBin(event.x);
    }
    else if (draggedControl == Magnitude)
    {
        // Invert Y (0 at bottom, 1 at top)
        float normalized = juce::jlimit(0.0f, 1.0f, 
                                        1.0f - static_cast<float>(event.y) / bounds.getHeight());
        audioProcessor.magnitudeThreshold = normalized;
    }
}
