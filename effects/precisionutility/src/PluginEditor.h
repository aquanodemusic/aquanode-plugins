/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class PrecisionUtilityAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    PrecisionUtilityAudioProcessorEditor (PrecisionUtilityAudioProcessor&);
    ~PrecisionUtilityAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    PrecisionUtilityAudioProcessor& audioProcessor;
    
    // UI Components
    juce::Slider delaySlider;
    juce::Label delayLabel;
    juce::TextEditor delayValueBox;
    
    juce::Slider phaseSlider;
    juce::Label phaseLabel;
    
    juce::Slider volLeftSlider;
    juce::Label volLeftLabel;
    
    juce::Slider volRightSlider;
    juce::Label volRightLabel;
    
    juce::Slider panLeftSlider;
    juce::Label panLeftLabel;
    
    juce::Slider panRightSlider;
    juce::Label panRightLabel;
    
    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> phaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volLeftAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volRightAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panLeftAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panRightAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrecisionUtilityAudioProcessorEditor)
};
