#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CenterCombAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    CenterCombAudioProcessorEditor(CenterCombAudioProcessor&);
    ~CenterCombAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    CenterCombAudioProcessor& audioProcessor;

    juce::Slider gainSlider, freqSlider, amountSlider, spreadSlider, qSlider, dampSlider;
    juce::Label gainLabel, freqLabel, amountLabel, spreadLabel, qLabel, dampLabel;

    juce::TextButton linearButton{ "Linear View Mode" };

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<Attachment> gainAttachment, freqAttachment, amountAttachment, spreadAttachment, qAttachment, dampAttachment;
    std::unique_ptr<ButtonAttachment> linearAttachment;

    juce::Label brandingLabel;
    juce::Label subtitleLabel;

    void setupSlider(juce::Slider& s, juce::Label& l, juce::String name);
    void drawResponseCurve(juce::Graphics& g, juce::Rectangle<int> area);

    juce::TextButton spreadModeButton{ "Multiplicative Spread" };
    std::unique_ptr<ButtonAttachment> spreadModeAttachment;

    juce::TextButton wetOnlyButton{ "Wet Only" };

    juce::TextButton hardLimiterButton{ "Hard Limiter" };
    std::unique_ptr<ButtonAttachment> hardLimiterAttachment;

    std::unique_ptr<Attachment> spreadHzAttachment;
    std::unique_ptr<Attachment> spreadRatioAttachment;
    std::unique_ptr<ButtonAttachment> wetOnlyAttachment;

    juce::ComboBox freqModeBox;
    juce::Label freqModeLabel;

    using ChoiceAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ChoiceAttachment> freqModeAttachment;

    juce::Slider smoothSlider;
    juce::Label  smoothLabel;

    std::unique_ptr<Attachment> smoothAttachment; // or SliderAttachment

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CenterCombAudioProcessorEditor)
};