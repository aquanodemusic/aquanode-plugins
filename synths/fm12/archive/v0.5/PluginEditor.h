#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class FM12SynthAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    FM12SynthAudioProcessorEditor(FM12SynthAudioProcessor&);
    ~FM12SynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FM12SynthAudioProcessor& processor;

    static constexpr int numOperators = 12;

    // Operator Knobs (12 Ops * 8 Parameter)
    std::vector<std::unique_ptr<juce::Slider>> opKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> opKnobAttachments;

    // Routing Matrix (12 * 12 Checkboxen)
    std::vector<std::unique_ptr<juce::ToggleButton>> matrixButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> matrixAttachments;
    
    std::unique_ptr<juce::ToggleButton> adsrFMToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> adsrFMAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM12SynthAudioProcessorEditor)
};
