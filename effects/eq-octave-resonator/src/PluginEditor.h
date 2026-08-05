#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class EQResonatorAudioProcessorEditor
    : public juce::AudioProcessorEditor,
    public juce::Timer
{
public:
    EQResonatorAudioProcessorEditor(EQResonatorAudioProcessor&);
    ~EQResonatorAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    void drawFrequencyVisualizer(juce::Graphics& g);

private:
    EQResonatorAudioProcessor& audioProcessor;

    // Global
    juce::ToggleButton wetOnlyButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetOnlyAttachment;

    juce::ToggleButton subtractModeButton; // <-- NEW
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> subtractModeAttachment; // <-- NEW

    // Octaves
    juce::ToggleButton octaveButtons[10];

    juce::Slider octaveQSliders[10];
    juce::Label  octaveQLabels[10];

    juce::Slider octaveGainSliders[10];
    juce::Label  octaveGainLabels[10];

    // Notes
    juce::ToggleButton naturalButtons[7];
    juce::ToggleButton sharpButtons[5];

    // Attachments
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> qAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> gainAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQResonatorAudioProcessorEditor)
};
