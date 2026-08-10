#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class DropletsAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    DropletsAudioProcessorEditor(DropletsAudioProcessor&);
    ~DropletsAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    DropletsAudioProcessor& audioProcessor;

    // --- UI Components ---
    // Top Row - Main Parameters
    juce::Slider radiusSlider, radiusBWSlider;
    juce::Slider depthSlider, depthBWSlider;
    juce::Slider pitchRiseSlider;
    juce::Slider rateSlider, rateBWSlider;
    juce::Slider brightnessSlider, widthSlider, fieldSlider;
    
    // Bottom Row - Modulation
    juce::Slider modAmountSlider;
    juce::Slider modAttackSlider, modDecaySlider, modSustainSlider, modReleaseSlider;
    juce::Slider maxDropletsSlider;
    juce::Slider volumeSlider;
    juce::ToggleButton secondaryEventToggle;
    
    // New controls - Global Volume ADSR and advanced params
    juce::Slider volAttackSlider, volDecaySlider, volSustainSlider, volReleaseSlider;
    juce::Slider secondaryProbSlider, secondaryDelaySlider, ampScaleSlider;
    juce::Slider phaseOffsetSlider;

    // --- Attachments ---
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> radiusAttach, radiusBWAttach;
    std::unique_ptr<SliderAttachment> depthAttach, depthBWAttach;
    std::unique_ptr<SliderAttachment> pitchRiseAttach;
    std::unique_ptr<SliderAttachment> rateAttach, rateBWAttach;
    std::unique_ptr<SliderAttachment> brightnessAttach, widthAttach, fieldAttach;
    std::unique_ptr<SliderAttachment> modAmountAttach;
    std::unique_ptr<SliderAttachment> modAttackAttach, modDecayAttach, modSustainAttach, modReleaseAttach;
    std::unique_ptr<SliderAttachment> maxDropletsAttach;
    std::unique_ptr<SliderAttachment> volumeAttach;
    std::unique_ptr<ButtonAttachment> secondaryEventAttach;
    
    // New attachments
    std::unique_ptr<SliderAttachment> volAttackAttach, volDecayAttach, volSustainAttach, volReleaseAttach;
    std::unique_ptr<SliderAttachment> secondaryProbAttach, secondaryDelayAttach, ampScaleAttach;
    std::unique_ptr<SliderAttachment> phaseOffsetAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DropletsAudioProcessorEditor)
};
