#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class WaveformDisplay : public juce::Component,
    public juce::FileDragAndDropTarget
{
public:
    WaveformDisplay(SlicerAudioProcessor& p) : processor(p) {}

    void paint(juce::Graphics& g) override;
    void resized() override {}

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    SlicerAudioProcessor& processor;

    int draggedSliceIndex = -1;
    bool isDraggingRegionStart = false;
    bool isDraggingRegionEnd = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
class SlicerAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    SlicerAudioProcessorEditor(SlicerAudioProcessor&);
    ~SlicerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SlicerAudioProcessor& audioProcessor;

    WaveformDisplay waveformDisplay;
    juce::TextButton loadButton;
    juce::TextButton analyzeButton;
    juce::TextButton exportButton;
    juce::TextButton dumpMidiButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> regionStartAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> regionEndAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliceStrengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;

    juce::Slider regionStartSlider;
    juce::Slider regionEndSlider;
    juce::Slider sliceStrengthSlider;
    juce::Slider volumeSlider;

    juce::Label regionStartLabel;
    juce::Label regionEndLabel;
    juce::Label sliceStrengthLabel;
    juce::Label volumeLabel;
    juce::Label infoLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::FileChooser> exportChooser;

    bool shouldReanalyze = false;

    void updateInfoLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicerAudioProcessorEditor)
};