#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class CPKnob : public juce::Component
{
public:
    juce::Slider slider;

    CPKnob (const juce::String& suffix = "")
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        slider.setTextValueSuffix (suffix);
        addAndMakeVisible (slider);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds());
    }
};

//==============================================================================
class CPToggle : public juce::Component
{
public:
    juce::ToggleButton button;

    CPToggle() { addAndMakeVisible (button); }

    void resized() override { button.setBounds (getLocalBounds()); }
};

//==============================================================================
class ClipPreserveAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    ClipPreserveAudioProcessorEditor (ClipPreserveAudioProcessor&);
    ~ClipPreserveAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized() override;

private:
    ClipPreserveAudioProcessor& audioProcessor;

    CPKnob driveKnob    { " dB" };
    CPKnob threshKnob   { " dB" };
    CPKnob preserveKnob { "%"   };
    CPKnob hpFreqKnob   { " Hz" };
    CPKnob lpFreqKnob   { " Hz" };
    CPKnob scGainKnob   { "%"   };
    CPKnob outputKnob   { " dB" };

    CPToggle hpToggle;
    CPToggle lpToggle;
    CPToggle osToggle;

    using APVTS = juce::AudioProcessorValueTreeState;
    APVTS::SliderAttachment driveAtt, threshAtt, preserveAtt,
                            hpFreqAtt, lpFreqAtt, scGainAtt, outputAtt;
    APVTS::ButtonAttachment hpOnAtt, lpOnAtt, oversampleAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipPreserveAudioProcessorEditor)
};
