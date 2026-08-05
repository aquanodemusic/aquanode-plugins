#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class TapElectricAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::Timer // Allows startTimerHz to work
{
public:
    TapElectricAudioProcessorEditor(TapElectricAudioProcessor&);
    ~TapElectricAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Timer callback for redrawing the EQ pads
    void timerCallback() override;

    // Mouse overrides for the EQ pads
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:

    juce::ComboBox freqModeBox;

    juce::TextButton randomizeButton;

    // The glue that keeps the UI and Processor in sync
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> freqModeAttachment;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Helper for EQ visuals
    struct EQBandControls {
        juce::Rectangle<float> padBounds;
        juce::Label freqLabel;
        juce::Slider qSlider;
        juce::Label qLabel;
        std::unique_ptr<SliderAttachment> qAttachment;
        bool isDragging = false;
    };

    // UI Components
    juce::Slider mixSlider, humVolumeSlider, humPanSlider, humStereoSlider, humDriftSlider;
    juce::Slider harmonic1Slider, harmonic2Slider, harmonic3Slider, harmonic4Slider, harmonic5Slider, harmonic6Slider;
    juce::Slider harmonic1RandAmtSlider, harmonic2RandAmtSlider, harmonic3RandAmtSlider, 
                 harmonic4RandAmtSlider, harmonic5RandAmtSlider, harmonic6RandAmtSlider;
    juce::Slider harmonic1RandSpdSlider, harmonic2RandSpdSlider, harmonic3RandSpdSlider,
                 harmonic4RandSpdSlider, harmonic5RandSpdSlider, harmonic6RandSpdSlider;
    juce::Slider noiseVolumeSlider, noisePanSlider, noiseStereoSlider;
    juce::Slider inputVolumeSlider, inputDriftSlider, inputWobbleSlider;

 

    // Labels
    juce::Label mixLabel, humVolumeLabel, humPanLabel, humStereoLabel, humDriftLabel, freqModeLabel;
    juce::Label harmonic1Label, harmonic2Label, harmonic3Label, harmonic4Label, harmonic5Label, harmonic6Label;
    juce::Label harmonic1RandAmtLabel, harmonic2RandAmtLabel, harmonic3RandAmtLabel,
                harmonic4RandAmtLabel, harmonic5RandAmtLabel, harmonic6RandAmtLabel;
    juce::Label harmonic1RandSpdLabel, harmonic2RandSpdLabel, harmonic3RandSpdLabel,
                harmonic4RandSpdLabel, harmonic5RandSpdLabel, harmonic6RandSpdLabel;
    juce::Label noiseVolumeLabel, noisePanLabel, noiseStereoLabel;
    juce::Label inputVolumeLabel, inputDriftLabel, inputWobbleLabel;

    // APVTS Attachments
    std::unique_ptr<SliderAttachment> mixAttachment, humVolumeAttachment, humPanAttachment, humStereoAttachment, humDriftAttachment;
    std::unique_ptr<SliderAttachment> h1Att, h2Att, h3Att, h4Att, h5Att, h6Att;
    std::unique_ptr<SliderAttachment> h1RandAmtAtt, h2RandAmtAtt, h3RandAmtAtt, h4RandAmtAtt, h5RandAmtAtt, h6RandAmtAtt;
    std::unique_ptr<SliderAttachment> h1RandSpdAtt, h2RandSpdAtt, h3RandSpdAtt, h4RandSpdAtt, h5RandSpdAtt, h6RandSpdAtt;
    std::unique_ptr<SliderAttachment> noiseVolAtt, noisePanAtt, noiseStereoAtt;
    std::unique_ptr<SliderAttachment> inputVolAtt, inputDriftAtt, inputWobbleAtt;


    // EQ Specifics
    EQBandControls eq1, eq2, eq3;
    juce::Rectangle<int> eqAreaBounds;

    void handleEQPadMouse(const juce::MouseEvent& e, EQBandControls& eq, const juce::String& fID, const juce::String& gID);
    void paintEQPad(juce::Graphics& g, EQBandControls& eq, const juce::String& fID, const juce::String& gID, const juce::Colour& color, const juce::String& label);

    TapElectricAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapElectricAudioProcessorEditor)
};