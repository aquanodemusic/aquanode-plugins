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
    // Aliase für weniger Tipparbeit
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    SpringerAudioProcessor& audioProcessor;

    // 1. Zuerst die UI-Elemente
    juce::Slider widthSlider, resonanceSlider, couplingSlider, dampingSlider, spreadSlider;
    juce::Slider numCoilsSlider, chirpSlider, lfoRateSlider, lfoDepthSlider, wetSlider;
    juce::Slider densityASlider, densityBSlider;
    juce::Slider numStagesSlider, pitchSlider;

    std::array<juce::Slider, 7> coilSliders;

    juce::TextButton randomizeButton;
    juce::TextButton muteDryButton;

    // 2. Dann die Attachments (MÜSSEN nach den Slidern kommen!)
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::unique_ptr<ButtonAttachment> muteDryAttachment;

    void setupKnob(juce::Slider& slider);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpringerAudioProcessorEditor)
};