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

    // Controls
    juce::Slider modulationIndexSlider;
    juce::Label modulationIndexLabel;

    juce::Slider modulatorGainSlider;
    juce::Label modulatorGainLabel;

    juce::Slider carrierGainSlider;
    juce::Label carrierGainLabel;

    juce::Slider dryWetSlider;
    juce::Label dryWetLabel;

    juce::ComboBox fmTypeCombo;
    juce::Label fmTypeLabel;

    // Info label
    juce::Label infoLabel;

    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnyFMAudioProcessorEditor)
};