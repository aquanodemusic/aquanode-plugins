#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// This is the main editor class for the EQ Resonator plugin.
// It handles the GUI controls (sliders, buttons), drawing the visualizer,
// and updating the interface regularly using a timer.
//==============================================================================
class EQResonatorAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    // Constructor: stores a reference to the processor so we can access parameters
    EQResonatorAudioProcessorEditor(EQResonatorAudioProcessor&);

    // Destructor
    ~EQResonatorAudioProcessorEditor() override;

    // Called when the GUI needs to repaint itself
    void paint(juce::Graphics&) override;

    // Called when the GUI size changes. Here we position sliders, buttons, etc.
    void resized() override;

    // Called regularly based on the timer interval.
    // Used to update the visualizer and check parameter changes.
    void timerCallback() override;

    // Draws the frequency visualizer inside the editor
    // MUST remain public because it might be called externally
    void drawFrequencyVisualizer(juce::Graphics& g);

private:
    // Reference to the processor to access parameters and state
    EQResonatorAudioProcessor& audioProcessor;

    // --- GLOBAL CONTROLS ---

    // Wet gain mix slider
    juce::Slider wetGainSlider;
    juce::Label wetGainLabel; // Label for the wet gain slider

    juce::ToggleButton wetOnlyButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetOnlyAttachment;

    // Attenuate toggle button
    // When active, it reduces the output gain of the resonators
    juce::ToggleButton attenuateButton;


    // --- OCTAVE-SPECIFIC CONTROLS ---

    // Toggle buttons to activate/deactivate each of the 10 octaves
    juce::ToggleButton octaveButtons[10];

    // Q (resonance) sliders for each octave
    juce::Slider octaveQSliders[10];
    juce::Label octaveQLabels[10]; // Labels for each Q slider

    // --- NOTE BUTTONS ---

    // Natural notes (C, D, E, F, G, A, B)
    juce::ToggleButton naturalButtons[7];

    // Sharp notes (C#, D#, F#, G#, A#)
    juce::ToggleButton sharpButtons[5];

    // --- PARAMETER ATTACHMENTS ---

    // Connects GUI controls to the processor's parameters
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attenuateAttachment;

    // Store attachments for the octave Q sliders and octave buttons
    // Using vectors to manage dynamic number of attachments
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> qAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    // JUCE macro to prevent copying and detect memory leaks
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQResonatorAudioProcessorEditor)
};
