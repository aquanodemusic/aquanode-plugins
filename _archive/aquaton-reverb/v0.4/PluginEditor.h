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
    // Row 1 – Core reverb
    juce::Slider sizeSlider;
    juce::Slider feedbackSlider;   // was gravitySlider
    juce::Slider lpSlider;
    juce::Slider hpSlider;
    juce::Slider mixSlider;

    // Row 2 – Character / diffusion
    juce::Slider preDiffSlider;
    juce::Slider tankDiffSlider;
    juce::Slider modRateSlider;
    juce::Slider modDepthSlider;
    juce::Slider spreadSlider;

    // Row 3 – Spatial / HF
    juce::Slider bloomAmtSlider;
    juce::Slider bloomTimeSlider;
    juce::Slider tapAmtSlider;
    juce::Slider hfWashHPSlider;
    juce::Slider hfWashAmtSlider;

    // Horizontal slider – Tank Stages (0-256)
    juce::Slider tankStagesSlider;

    // Horizontal slider – FDN Order (1-32)
    juce::Slider fdnOrderSlider;

    // Buttons
    juce::TextButton randomizeBtn;
    juce::TextButton randomizeAllBtn;
    juce::TextButton wetOnlyBtn;
    juce::TextButton saveBtn;
    juce::TextButton loadBtn;

    //------------------------------------------------------------------
    std::vector<std::unique_ptr<SliderAttach>> sliderAttachments;
    std::unique_ptr<ButtonAttach>              wetOnlyAttach;

    // Async file chooser kept alive across the callback
    std::unique_ptr<juce::FileChooser> fileChooser;

    //------------------------------------------------------------------
    struct KnobInfo { juce::Slider* slider; juce::String label; };
    std::array<KnobInfo, 15> knobs;

    void saveState();
    void loadState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AquatonAudioProcessorEditor)
};
