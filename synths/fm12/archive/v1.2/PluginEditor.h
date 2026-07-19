#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class FM12SynthAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    FM12SynthAudioProcessorEditor(FM12SynthAudioProcessor&);
    ~FM12SynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FM12SynthAudioProcessor& processor;

    static constexpr int numOperators = 12;

    // Operator Knobs (12 Ops * 7 Parameters)
    std::vector<std::unique_ptr<juce::Slider>> opKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> opKnobAttachments;

    // Routing Matrix (12 * 12 Checkboxen, excluding diagonal)
    std::vector<std::unique_ptr<juce::ToggleButton>> matrixButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> matrixAttachments;

    // Feedback knobs (diagonal of matrix - 12 knobs)
    std::vector<std::unique_ptr<juce::Slider>> feedbackKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> feedbackAttachments;

    std::unique_ptr<juce::ToggleButton> adsrFMToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> adsrFMAttachment;

    std::unique_ptr<juce::TextButton> randomizeButton;
    std::unique_ptr<juce::TextButton> stabButton;

    // Preset Save/Load buttons
    std::unique_ptr<juce::TextButton> saveButton;
    std::unique_ptr<juce::TextButton> loadButton;
    std::unique_ptr<juce::TextButton> halveModsButton;

    // EXP FB — anyFM-style delay-feedback mode for diagonal knobs
    std::unique_ptr<juce::ToggleButton> expFeedbackToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> expFeedbackAttachment;

    std::unique_ptr<juce::Slider> chorusAmountKnob;
    std::unique_ptr<juce::Slider> chorusWidthKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusWidthAttachment;

    std::unique_ptr<juce::Slider> nyquistSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> nyquistAttachment;

    void randomizeMatrix();
    void randomizeStab();
    void halveModulators();
    void savePreset();
    void loadPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM12SynthAudioProcessorEditor)
};