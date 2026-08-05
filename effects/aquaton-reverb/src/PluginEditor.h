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
    // Row 1 – Core reverb (6 knobs)
    juce::Slider sizeSlider;
    juce::Slider feedbackSlider;
    juce::Slider tailSlider;
    juce::Slider lpSlider;
    juce::Slider hpSlider;
    juce::Slider mixSlider;

    // Row 2 – Input / diffusion / modulation (6 knobs)
    juce::Slider tapAmtSlider;
    juce::Slider preDiffSlider;
    juce::Slider tankDiffSlider;
    juce::Slider apfModSlider;
    juce::Slider modRateSlider;
    juce::Slider modDepthSlider;

    // Row 3 – Spatial / HF / polarity (6 knobs)
    juce::Slider spreadSlider;
    juce::Slider bloomAmtSlider;
    juce::Slider bloomTimeSlider;
    juce::Slider hfWashHPSlider;
    juce::Slider hfWashAmtSlider;
    juce::Slider polarityAmtSlider;

    // Row 4 – Pre-delay knob + FDN/tank knobs + control buttons (3 knobs, 3 buttons)
    juce::Slider predelaySlider;    // -200..+200 ms; knob centre = 0 ms
    juce::Slider fdnOrderSlider;    // was horizontal slider, now knob
    juce::Slider tankStagesSlider;  // was horizontal slider, now knob

    // Buttons
    juce::TextButton randomizeParamBtn;   // randomises all params (excl. FDN/tank)
    juce::TextButton randomizeMatrixBtn;  // randomises Hadamard matrix & delay times
    juce::TextButton freezeBtn;           // freeze toggle — infinite sustain

    //------------------------------------------------------------------
    std::vector<std::unique_ptr<SliderAttach>> sliderAttachments;
    std::unique_ptr<ButtonAttach>              freezeAttach;
    std::unique_ptr<juce::FileChooser>         fileChooser;

    // Header save/load
    juce::TextButton saveBtn;
    juce::TextButton loadBtn;

    //------------------------------------------------------------------
    // 21 knobs: 6 (row1) + 6 (row2) + 6 (row3) + 3 (row4)
    struct KnobInfo { juce::Slider* slider; juce::String label; };
    std::array<KnobInfo, 21> knobs;

    void saveState();
    void loadState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AquatonAudioProcessorEditor)
};
