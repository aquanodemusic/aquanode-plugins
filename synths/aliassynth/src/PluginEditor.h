#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class AliasSynthAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    AliasSynthAudioProcessorEditor(AliasSynthAudioProcessor&);
    ~AliasSynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AliasSynthAudioProcessor& audioProcessor;

    // --- UI Components ---
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Slider fmAmountSlider, fmRatioSlider;
    juce::Slider nyquistSlider, bitDepthSlider; // Digital Degrade
    juce::Slider volSlider;

    juce::ComboBox modeSelector;
    juce::TextButton loadButton;
    juce::ToggleButton foldToggle;
    juce::ToggleButton srModeToggle;

    // --- Attachments ---
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> attackAttach, decayAttach, sustainAttach, releaseAttach;
    std::unique_ptr<SliderAttachment> fmAmountAttach, fmRatioAttach;
    std::unique_ptr<SliderAttachment> nyquistAttach, bitDepthAttach;
    std::unique_ptr<SliderAttachment> volAttach;
    std::unique_ptr<ComboAttachment>  modeAttach;
    std::unique_ptr<ButtonAttachment> foldAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> srModeAttach;

    juce::Slider transposeSlider;
    std::unique_ptr<SliderAttachment> transposeAttach;

    juce::Slider glideSlider;
    std::unique_ptr<SliderAttachment> glideAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AliasSynthAudioProcessorEditor)
};