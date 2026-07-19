#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class WaveformDisplay : public juce::Component,
    public juce::FileDragAndDropTarget
{
public:
    WaveformDisplay(GranulateAudioProcessor& p) : processor(p) {}

    void paint(juce::Graphics& g) override;
    void resized() override {}

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    GranulateAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
class GranulateAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    GranulateAudioProcessorEditor(GranulateAudioProcessor&);
    ~GranulateAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    GranulateAudioProcessor& audioProcessor;

    WaveformDisplay waveformDisplay;
    juce::TextButton loadButton;
    juce::ToggleButton modeToggle;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> numGrainsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainPositionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sprayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> windowSizeAttachment;
    
    // Grain ADSR
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainSustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grainReleaseAttachment;
    
    // Note ADSR
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteSustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteReleaseAttachment;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amplitudeModAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amDispersionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchDispersionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stereoSpreadAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;

    juce::Slider numGrainsSlider;
    juce::Slider grainSizeSlider;
    juce::Slider grainPositionSlider;
    juce::Slider spraySlider;
    juce::Slider windowSizeSlider;
    
    // Grain ADSR
    juce::Slider grainAttackSlider;
    juce::Slider grainDecaySlider;
    juce::Slider grainSustainSlider;
    juce::Slider grainReleaseSlider;
    
    // Note ADSR
    juce::Slider noteAttackSlider;
    juce::Slider noteDecaySlider;
    juce::Slider noteSustainSlider;
    juce::Slider noteReleaseSlider;
    
    juce::Slider amplitudeModSlider;
    juce::Slider amDispersionSlider;
    juce::Slider pitchDispersionSlider;
    juce::Slider pitchSlider;
    juce::Slider stereoSpreadSlider;
    juce::Slider volumeSlider;

    juce::Label numGrainsLabel;
    juce::Label grainSizeLabel;
    juce::Label grainPositionLabel;
    juce::Label sprayLabel;
    juce::Label windowSizeLabel;
    
    // Grain ADSR
    juce::Label grainAttackLabel;
    juce::Label grainDecayLabel;
    juce::Label grainSustainLabel;
    juce::Label grainReleaseLabel;
    
    // Note ADSR
    juce::Label noteAttackLabel;
    juce::Label noteDecayLabel;
    juce::Label noteSustainLabel;
    juce::Label noteReleaseLabel;
    
    juce::Label amplitudeModLabel;
    juce::Label amDispersionLabel;
    juce::Label pitchDispersionLabel;
    juce::Label pitchLabel;
    juce::Label stereoSpreadLabel;
    juce::Label volumeLabel;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranulateAudioProcessorEditor)
};