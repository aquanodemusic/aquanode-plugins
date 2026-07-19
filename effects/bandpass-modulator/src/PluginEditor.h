#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

class BandpassModulatorAudioProcessorEditor
    : public juce::AudioProcessorEditor,
    public juce::Timer
{
    public:
        BandpassModulatorAudioProcessorEditor(BandpassModulatorAudioProcessor& processor);

        ~BandpassModulatorAudioProcessorEditor() override;

        void paint(juce::Graphics& g) override;

        void resized() override;

        void timerCallback() override;

    private:
        void drawFilterCurve(juce::Graphics& g);

        BandpassModulatorAudioProcessor& audioProcessor;

        juce::Slider minFreqSlider, maxFreqSlider, glideTimeSlider, stayTimeSlider,
            panningSlider, dryWetSlider, widthSlider, wetGainSlider;
        juce::Label minFreqLabel, maxFreqLabel, glideTimeLabel, stayTimeLabel,
            panningLabel, dryWetLabel, widthLabel, wetGainLabel;
        juce::ComboBox modeSelector;
        juce::ToggleButton panningLfoSwitch;
        juce::ToggleButton noteLockSwitch;
        juce::ToggleButton btnC, btnD, btnE, btnF, btnG, btnA, btnB, btnCsharp, btnDsharp, btnFsharp, btnGsharp, btnAsharp;
        juce::Label brandingLabel;

        juce::DropShadowEffect minFreqShadow, maxFreqShadow, glideTimeShadow, stayTimeShadow,
            panningShadow, dryWetShadow, widthShadow, wetGainShadow,
            toggleShadow, brandingShadow;

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        std::unique_ptr<SliderAttachment> minFreqAttachment;
        std::unique_ptr<SliderAttachment> maxFreqAttachment;
        std::unique_ptr<SliderAttachment> glideTimeAttachment;
        std::unique_ptr<SliderAttachment> stayTimeAttachment;
        std::unique_ptr<SliderAttachment> panningAttachment;
        std::unique_ptr<SliderAttachment> dryWetAttachment;
        std::unique_ptr<SliderAttachment> widthAttachment;
        std::unique_ptr<SliderAttachment> wetGainAttachment;

        std::unique_ptr<ButtonAttachment> lfoActiveAttachment;
        std::unique_ptr<ButtonAttachment> noteLockAttachment;
        std::unique_ptr<ButtonAttachment> attC, attD, attE, attF, attG, attA, attB, attCsharp, attDsharp, attFsharp, attGsharp, attAsharp;

        std::unique_ptr<ComboBoxAttachment> modeAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandpassModulatorAudioProcessorEditor)
};