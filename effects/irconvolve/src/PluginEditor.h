#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Forward declaration
class IRVisualizer;

class IRConvolverAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    IRConvolverAudioProcessorEditor(IRConvolverAudioProcessor&);
    ~IRConvolverAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    void loadButtonClicked();
    void updateIRDisplay();

    IRConvolverAudioProcessor& audioProcessor;

    juce::TextButton loadButton;
    juce::Label irLabel;
    juce::Label mixLabel;
    juce::Label gainLabel;

    juce::Slider mixSlider;
    juce::Slider gainSlider;

    std::unique_ptr<IRVisualizer> irVisualizer;
    
    bool needsIRUpdate = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRConvolverAudioProcessorEditor)
};
