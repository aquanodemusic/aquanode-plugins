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

    // Operator Knobs (12 Ops * 6 Parameters: ATT DEC SUS REL VOL RATIO)
    std::vector<std::unique_ptr<juce::Slider>> opKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> opKnobAttachments;

    // Lock Ratio toggles (one per operator, UI-only — not saved in preset)
    // When active, the ratio knob snaps to multiples of 0.25.
    std::vector<std::unique_ptr<juce::ToggleButton>> lockRatioToggles;

    // Routing Matrix (12 * 12 Checkboxen, excluding diagonal)
    std::vector<std::unique_ptr<juce::ToggleButton>> matrixButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> matrixAttachments;

    // Feedback knobs (diagonal of matrix - 12 knobs)
    std::vector<std::unique_ptr<juce::Slider>> feedbackKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> feedbackAttachments;

    std::unique_ptr<juce::TextButton> randomizeButton;
    std::unique_ptr<juce::TextButton> stabButton;

    // Preset Save/Load buttons
    std::unique_ptr<juce::TextButton> saveButton;
    std::unique_ptr<juce::TextButton> loadButton;
    std::unique_ptr<juce::TextButton> halveModsButton;

    // EXP FB — anyFM-style delay-feedback mode for diagonal knobs
    std::unique_ptr<juce::ToggleButton> expFeedbackToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> expFeedbackAttachment;

    // FM Engine Mode combobox — replaces the old "Real FM" + "Through Zero FM" toggles.
    // Items: Phase Modulation Mode / Linear FM Mode / Lin. FM Through Zero /
    //        Exponential FM Mode / Exp. FM Through Zero
    std::unique_ptr<juce::ComboBox> fmEngineModeComboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fmEngineModeAttachment;

    std::unique_ptr<juce::Slider> chorusAmountKnob;
    std::unique_ptr<juce::Slider> chorusWidthKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusWidthAttachment;

    std::unique_ptr<juce::Slider> nyquistSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> nyquistAttachment;

    // View OP selector — 0 = All, 1–12 = single operator
    std::unique_ptr<juce::ComboBox> viewOpComboBox;
    int currentViewOp = 1; // default: show OP 1 only

    void updateViewLayout();
    void randomizeMatrix();
    void randomizeStab();
    void halveModulators();
    void savePreset();
    void loadPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM12SynthAudioProcessorEditor)
};
