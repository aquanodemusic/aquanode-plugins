#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralGateAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    SpectralGateAudioProcessorEditor (SpectralGateAudioProcessor&);
    ~SpectralGateAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void drawFFT(juce::Graphics& g);
    void updateControlFromMouse(const juce::MouseEvent& event);
    
    SpectralGateAudioProcessor& audioProcessor;
    
    static constexpr int fftDisplaySize = SpectralGateAudioProcessor::fftSize / 2;
    std::array<float, fftDisplaySize> fftDisplayData;
    
    juce::Rectangle<int> fftArea;
    
    enum DraggedControl
    {
        None = -1,
        LowerFreq = 0,
        UpperFreq = 1,
        Magnitude = 2
    };
    
    DraggedControl draggedControl = None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralGateAudioProcessorEditor)
};
