#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class AnyFMAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::Timer
{
public:
    AnyFMAudioProcessorEditor(AnyFMAudioProcessor&);
    ~AnyFMAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    AnyFMAudioProcessor& audioProcessor;

    // Sliders + labels
    juce::Slider modulationIndexSlider;
    juce::Label  modulationIndexLabel;
    juce::Slider modulatorGainSlider;
    juce::Label  modulatorGainLabel;
    juce::Slider carrierGainSlider;
    juce::Label  carrierGainLabel;
    juce::Slider dryWetSlider;
    juce::Label  dryWetLabel;

    // Combos + labels
    juce::ComboBox fmTypeCombo;
    juce::Label    fmTypeLabel;
    juce::ComboBox routingCombo;
    juce::Label    routingLabel;

    // APVTS attachments – own the connection between widget and parameter
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> modIndexAttachment;
    std::unique_ptr<SliderAttachment> modGainAttachment;
    std::unique_ptr<SliderAttachment> carGainAttachment;
    std::unique_ptr<SliderAttachment> dryWetAttachment;
    std::unique_ptr<ComboAttachment>  fmTypeAttachment;
    std::unique_ptr<ComboAttachment>  routingAttachment;

    // Info footer
    juce::Label infoLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnyFMAudioProcessorEditor)
};
