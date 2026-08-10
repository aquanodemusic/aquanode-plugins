#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralStereoizeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SpectralStereoizeAudioProcessorEditor (SpectralStereoizeAudioProcessor&);
    ~SpectralStereoizeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    //==========================================================================
    void timerCallback() override;

    // Coordinate conversions
    float xToFreq (float x) const;
    float freqToX (float freq) const;
    float yToWidth (float y) const;
    float widthToY (float width) const;

    // Drawing helpers
    void drawGrid (juce::Graphics&);
    void drawSpectrum (juce::Graphics&);
    void drawWidthCurve (juce::Graphics&);
    void drawTooltip (juce::Graphics&, float mouseX, float mouseY);

    // FFT size combo
    juce::ComboBox fftSizeCombo;
    juce::Label    fftSizeLabel;

    juce::Slider visualScaleSlider;
    juce::Label visualScaleLabel;

    // Data buffers
    std::array<float, SpectralStereoizeAudioProcessor::maxBins> widthCurve;
    std::vector<float> mainSpectrum;   // size = numBins
    std::vector<float> scSpectrum;     // size = numBins

    // Mouse state
    bool isDragging = false;
    float lastDragX = 0.0f;
    float lastDragY = 0.0f;
    float hoverX = -1.0f;

    // Reference to processor
    SpectralStereoizeAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralStereoizeAudioProcessorEditor)
};