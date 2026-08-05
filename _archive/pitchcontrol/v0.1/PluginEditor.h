#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CyanLookAndFeel.h"

//==============================================================================
class PitchColorAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    PitchColorAudioProcessorEditor (PitchColorAudioProcessor&);
    ~PitchColorAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PitchColorAudioProcessor& audioProcessor;
    
    // Custom look and feel
    CyanLookAndFeel cyanLookAndFeel;
    
    // UI Components
    juce::Label titleLabel;
    
    // 12 gain knobs (one per note)
    std::array<juce::Slider, 12> gainSliders;
    std::array<juce::Label, 12> gainLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 12> gainAttachments;
    
    // 12 Q knobs (one per note)
    std::array<juce::Slider, 12> qSliders;
    std::array<juce::Label, 12> qLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 12> qAttachments;
    
    // Start/End note controls
    juce::Slider startNoteSlider;
    juce::Label startNoteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> startNoteAttachment;
    
    juce::Slider endNoteSlider;
    juce::Label endNoteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> endNoteAttachment;
    
    // Wet only button
    juce::ToggleButton wetOnlyButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetOnlyAttachment;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchColorAudioProcessorEditor)
};
