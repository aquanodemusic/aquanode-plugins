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
    
    // Calculate X positions for vertical bars (frequency bins)
    int lowerX = static_cast<int>(lowerFreqBin * bounds.getWidth());
    int upperX = static_cast<int>(upperFreqBin * bounds.getWidth());
    
    // Calculate Y position for horizontal bar (magnitude threshold)
    // magThreshold is 0.0 to 1.0, where 1.0 is top (0 dB) and 0.0 is bottom (-60 dB)
    int magY = bounds.getHeight() - static_cast<int>(magThreshold * bounds.getHeight());
    
    // Draw vertical frequency limit bars (cyan)
    g.setColour(juce::Colours::cyan);
    g.fillRect(lowerX - 2, 0, 4, bounds.getHeight());
    g.fillRect(upperX - 2, 0, 4, bounds.getHeight());
    
    // Draw horizontal magnitude threshold bar (cyan)
    g.fillRect(0, magY - 2, bounds.getWidth(), 4);
    
    // Draw labels
    g.setColour(juce::Colours::black);
    g.setFont(12.0f);
    
    // Lower frequency label
    if (lowerX > 10)
        g.drawText("Lower Freq", lowerX - 60, 10, 100, 20, juce::Justification::centred);
    
    // Upper frequency label
    if (upperX < bounds.getWidth() - 10)
        g.drawText("Upper Freq", upperX - 50, 10, 100, 20, juce::Justification::centred);
    
    // Magnitude threshold label
    int labelY = magY - 20;
    if (labelY < 10)
        labelY = magY + 5;
    
    float magDB = (magThreshold * 60.0f - 60.0f);
    juce::String magLabel = juce::String(magDB, 1) + " dB";
    g.drawText(magLabel, 10, labelY, 100, 20, juce::Justification::left);
}

void SpectralGateAudioProcessorEditor::drawFFT(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    fftArea = bounds;
    
    // Get FFT data from processor
    audioProcessor.getFFTData(fftDisplayData.data(), fftDisplaySize);
    
    // Find max value for normalization
    float maxLevel = 0.001f;
    for (int i = 0; i < fftDisplaySize; ++i)
    {
        maxLevel = juce::jmax(maxLevel, fftDisplayData[i]);
    }
    
    // Draw FFT bars
    float barWidth = static_cast<float>(bounds.getWidth()) / fftDisplaySize;
    
    // Light blue-grey color
    g.setColour(juce::Colour(0xffa0b0c0));
    
    for (int i = 0; i < fftDisplaySize; ++i)
    {
        float magnitude = fftDisplayData[i];
        
        // Convert to dB scale
        float dB = 20.0f * std::log10(magnitude / maxLevel + 0.00001f);
        
        // Normalize to 0.0 to 1.0 (assuming -60 dB to 0 dB range)
        float normalized = (dB + 60.0f) / 60.0f;
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
