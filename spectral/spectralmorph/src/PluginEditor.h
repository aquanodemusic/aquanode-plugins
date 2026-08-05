#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "PluginProcessor.h"
#include "SpectrogramComponent.h"

//==============================================================================
class MorphLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MorphLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;
};

//==============================================================================
class SpectralMorphAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SpectralMorphAudioProcessorEditor (SpectralMorphAudioProcessor&);
    ~SpectralMorphAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void addKnob (Knob&, const juce::String& paramID, const juce::String& text);
    void addToggle (juce::ToggleButton&,
                    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>&,
                    const juce::String& paramID, const juce::String& text);

    SpectralMorphAudioProcessor& proc;
    MorphLookAndFeel lnf;

    SpectrogramComponent spectrogram;

    juce::ComboBox fftBox, overlapBox, morphModeBox;
    juce::Label    fftLabel, overlapLabel, morphModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftAtt, overlapAtt, morphModeAtt;

    Knob morph, clarity, smooth, maxBoost, dynamics, mix, outGain;
    Knob attack, release, flatten, sibilance, fill, fold, glide, lock, peakFloor;

    // knobs shown for the currently selected morph mode, in display order
    std::vector<Knob*> activeKnobs;
    juce::Label modeInfoLabel;

    void updateModeUI();

    juce::ToggleButton flipButton, freezeButton, bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        flipAtt, freezeAtt, bypassAtt;

    // not an APVTS parameter - purely a UI/CPU convenience toggle
    juce::ToggleButton spectrogramButton;

    // HD Visuals: switches the rolling spectrogram to the frequency-reassigned
    // render path (proc.engine does the extra display-only analysis for it).
    // Not an APVTS parameter, same reasoning as spectrogramButton above -
    // it's a rendering choice, not a plugin parameter that needs recall.
    juce::ToggleButton hdVisualsButton;

    juce::Label routingLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralMorphAudioProcessorEditor)
};
