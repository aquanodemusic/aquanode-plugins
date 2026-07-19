#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralEnhanceAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    SpectralEnhanceAudioProcessorEditor (SpectralEnhanceAudioProcessor&);
    ~SpectralEnhanceAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void drawFFT(juce::Graphics& g);
    void updateControlFromMouse(const juce::MouseEvent& event);

    SpectralEnhanceAudioProcessor& audioProcessor;

    static constexpr int fftDisplaySize = SpectralEnhanceAudioProcessor::fftSize / 2;
    std::array<float, fftDisplaySize> fftDisplayData;

    juce::Rectangle<int> fftArea;
    juce::TextButton attenuateButton;
    juce::Slider     slopeSlider;
    juce::Label      slopeLabel;

    enum DraggedControl
    {
        None      = -1,
        LowerFreq =  0,
        UpperFreq =  1,
        Magnitude =  2
    };

    DraggedControl draggedControl = None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralEnhanceAudioProcessorEditor)
};
