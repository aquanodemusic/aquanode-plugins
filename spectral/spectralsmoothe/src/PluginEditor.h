/*
  ==============================================================================
    SpectralSmooth — minimal editor
    Plain rotary sliders + combo boxes + a big Freeze toggle. No custom
    look-and-feel; this is meant as a functional starting point.
  ==============================================================================
*/

#pragma once

#include "PluginProcessor.h"

class SpectralSmoothAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SpectralSmoothAudioProcessorEditor(SpectralSmoothAudioProcessor&);
    ~SpectralSmoothAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SpectralSmoothAudioProcessor& processor;

    juce::Label titleLabel;
    juce::Label infoLabel;

    // freeze
    juce::TextButton freezeButton{ "FREEZE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    // 6 modes: Static/Evolving/Continuous x Hold/Follow
    juce::ComboBox modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    juce::Label modeLabel{ {}, "Mode" };

    juce::ComboBox fftBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftAttachment;
    juce::Label fftLabel{ {}, "FFT Size" };

    // rotary knobs, built uniformly
    struct Knob
    {
        juce::Slider slider{ juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    Knob characterKnob, evolveKnob, diffusionKnob, stretchKnob, mixKnob;

    juce::ToggleButton gainMatchToggle{ "Gain Match" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gainMatchAttachment;

    void setupKnob(Knob& k, const juce::String& paramId, const juce::String& labelText);
    void updateModeDependentEnablement();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralSmoothAudioProcessorEditor)
};