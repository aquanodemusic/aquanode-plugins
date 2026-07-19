#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
 * Editor for SpectralLatency.
 *
 * The central horizontal line represents 0 delay.
 * Drawing the cyan curve ABOVE centre = positive delay (bin sounds later).
 * Drawing the cyan curve BELOW centre = negative delay (bin sounds earlier;
 *   the processor compensates by reporting global latency to the DAW).
 *
 * Double-click resets the curve to flat (0 delay everywhere).
 */
class SpectralLatencyAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit SpectralLatencyAudioProcessorEditor (SpectralLatencyAudioProcessor&);
    ~SpectralLatencyAudioProcessorEditor() override;

    void paint    (juce::Graphics&) override;
    void resized  () override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    //==========================================================================
    void timerCallback() override;

    // Coordinate conversions (log-freq X, linear-delay Y)
    float xToFreq   (float x)     const;
    float freqToX   (float freq)  const;
    float yToDelay  (float y)     const;
    float delayToY  (float delay) const;

    // Drawing helpers
    void drawGrid       (juce::Graphics&);
    void drawSpectrum   (juce::Graphics&);
    void drawDelayCurve (juce::Graphics&);
    void drawTooltip    (juce::Graphics&, float mouseX, float mouseY);

    // Controls
    juce::ComboBox fftSizeCombo;
    juce::Label    fftSizeLabel;

    juce::Slider   maxLatencySlider;
    juce::Label    maxLatencyLabel;

    juce::TextButton randomizeButton;

    // Data buffers
    std::array<float, SpectralLatencyAudioProcessor::maxBins> delayCurve;
    std::vector<float> spectrum;   // size = numBins, linear magnitudes

    // Mouse state
    bool  isDragging = false;
    float lastDragX  = 0.0f;
    float lastDragY  = 0.0f;

    // Reference to processor
    SpectralLatencyAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralLatencyAudioProcessorEditor)
};