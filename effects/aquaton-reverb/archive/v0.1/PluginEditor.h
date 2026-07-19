#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class AquatonAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AquatonAudioProcessorEditor(AquatonAudioProcessor&);
    ~AquatonAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;

    AquatonAudioProcessor& audioProcessor;

    //------------------------------------------------------------------
    // Knobs – Row 1 (core reverb)
    juce::Slider sizeSlider;
    juce::Slider gravitySlider;
    juce::Slider lpSlider;
    juce::Slider hpSlider;
    juce::Slider mixSlider;

    // Knobs – Row 2 (character)
    juce::Slider preDiffSlider;
    juce::Slider tankDiffSlider;
    juce::Slider modRateSlider;
    juce::Slider modDepthSlider;
    juce::Slider spreadSlider;

    // Horizontal slider – Tank Stages
    juce::Slider tankStagesSlider;

    // Buttons
    juce::TextButton randomizeBtn;
    juce::TextButton wetOnlyBtn;

    //------------------------------------------------------------------
    std::vector<std::unique_ptr<SliderAttach>> sliderAttachments;
    std::unique_ptr<ButtonAttach>              wetOnlyAttach;

    //------------------------------------------------------------------
    void setupKnob(juce::Slider& s, const juce::String& paramId,
                   const juce::String& label);
    void drawSectionLabel(juce::Graphics& g, const juce::String& text,
                          juce::Rectangle<int> bounds);

    // Label strings – kept parallel to the knob array for easy paint()
    struct KnobInfo { juce::Slider* slider; juce::String label; };
    std::array<KnobInfo, 10> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AquatonAudioProcessorEditor)
};
