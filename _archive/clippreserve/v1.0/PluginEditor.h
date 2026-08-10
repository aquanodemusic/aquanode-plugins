#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// A clean custom rotary knob with label underneath
class CPKnob : public juce::Component
{
public:
    juce::Slider   slider;
    juce::Label    label;

    CPKnob(const juce::String& labelText, const juce::String& suffix = "")
    {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
        slider.setTextValueSuffix(suffix);
        addAndMakeVisible(slider);

        label.setText(labelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(juce::FontOptions("Courier New", 11.f, juce::Font::plain)));
        addAndMakeVisible(label);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds(b.removeFromBottom(16));
        slider.setBounds(b);
    }
};

//==============================================================================
class ClipPreserveAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    ClipPreserveAudioProcessorEditor(ClipPreserveAudioProcessor&);
    ~ClipPreserveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawLevelMeter(juce::Graphics& g, juce::Rectangle<float> area, float level);

    ClipPreserveAudioProcessor& audioProcessor;

    CPKnob driveKnob{ "DRIVE",    " dB" };
    CPKnob threshKnob{ "THRESH",   " dB" };
    CPKnob preserveKnob{ "PRESERVE", "%" };
    CPKnob hpFreqKnob{ "HP FREQ",  " Hz" };
    CPKnob outputKnob{ "OUTPUT",   " dB" };

    using APVTS = juce::AudioProcessorValueTreeState;
    APVTS::SliderAttachment driveAtt, threshAtt, preserveAtt, hpFreqAtt, outputAtt;

    // Metering
    float meterInL = 0.f, meterInR = 0.f;
    float meterOutL = 0.f, meterOutR = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipPreserveAudioProcessorEditor)
};

