#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpringerAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    SpringerAudioProcessorEditor(SpringerAudioProcessor&);
    ~SpringerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupKnob(juce::Slider& slider);

    // 1. Processor Reference first
    SpringerAudioProcessor& audioProcessor;

    // 2. UI Components
    juce::Slider widthSlider, resonanceSlider, couplingSlider, wetSlider, dampingSlider, lfoRateSlider, lfoDepthSlider;
    juce::ToggleButton muteDryButton;
    juce::TextButton randomizeButton;

    // 3. Attachments (Declared AFTER the sliders/buttons they control)
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> widthAttachment, resonanceAttachment, couplingAttachment, wetAttachment, dampingAttachment, lfoRateAttachment, lfoDepthAttachment;

    // Fixed: Only keep the attachment for Mute Dry (the toggle), not Randomize
    std::unique_ptr<ButtonAttachment> muteDryAttachment;

    juce::Slider densityASlider;
    juce::Slider densityBSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> densityAAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> densityBAttachment;

    juce::Slider numStagesSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> numStagesAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpringerAudioProcessorEditor)
};