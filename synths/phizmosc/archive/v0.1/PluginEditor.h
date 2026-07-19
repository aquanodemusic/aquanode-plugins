#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Custom LookAndFeel: pink-blue gradient, glowing knobs
class PhismOscLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PhismOscLookAndFeel();

    void drawRotarySlider(juce::Graphics& g,
        int x, int y, int width, int height,
        float sliderPosProportional,
        float rotaryStartAngle,
        float rotaryEndAngle,
        juce::Slider& slider) override;

    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    juce::Font getLabelFont(juce::Label&) override;

private:
    const juce::Colour bg1{ 0xff1a0a2e };
    const juce::Colour bg2{ 0xff0d1f4f };
    const juce::Colour accent1{ 0xffff6ec7 };
    const juce::Colour accent2{ 0xff6ec6ff };
    const juce::Colour knobFace{ 0xff2a1a4a };
    const juce::Colour knobRim{ 0xff9940c8 };
};

//==============================================================================
// A labelled knob that holds an APVTS attachment
class PhismOscKnob : public juce::Component
{
public:
    PhismOscKnob(const juce::String& label,
        juce::AudioProcessorValueTreeState& apvts,
        const juce::String& paramID,
        PhismOscLookAndFeel& laf);
    ~PhismOscKnob() override = default;

    void resized() override;

private:
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhismOscKnob)
};

//==============================================================================
// Real-time Waveform Display
class WavetableDisplay : public juce::Component, private juce::Timer
{
public:
    WavetableDisplay(TranswaveAudioProcessor& p);
    ~WavetableDisplay() override;

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }
    TranswaveAudioProcessor& proc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableDisplay)
};

//==============================================================================
class TranswaveAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    TranswaveAudioProcessorEditor(TranswaveAudioProcessor&);
    ~TranswaveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void loadWavetableClicked();

    TranswaveAudioProcessor& audioProcessor;
    PhismOscLookAndFeel laf;

    //==========================================================================
    // Knobs (Row 1)
    PhismOscKnob knobPosition;
    PhismOscKnob knobGain;
    PhismOscKnob knobEvolution;
    PhismOscKnob knobEvoLFORate;
    PhismOscKnob knobEvoLFODepth;
    PhismOscKnob knobPosLFORate;
    PhismOscKnob knobPosLFODepth;
    PhismOscKnob knobDetune;
    PhismOscKnob knobPitchLFO;
    PhismOscKnob knobPitchLFORate;

    // Knobs (Row 2)
    PhismOscKnob knobAttack;
    PhismOscKnob knobDecay;
    PhismOscKnob knobSustain;
    PhismOscKnob knobRelease;
    PhismOscKnob knobBitCrush;
    PhismOscKnob knobGrit;
    PhismOscKnob knobScanStyle;
    PhismOscKnob knobScanJump;
    PhismOscKnob knobFilterFreq;
    PhismOscKnob knobFilterRes;

    //==========================================================================
    WavetableDisplay wtDisplay;

    // File loading UI
    juce::TextButton   loadButton;
    juce::Label        filenameLabel;
    juce::Label        cycleSizeLabel;
    juce::TextEditor   cycleSizeEditor;
    juce::Label        infoLabel;

    // Section labels
    juce::Label sectionWT, sectionEvo, sectionPitch, sectionADSR;
    juce::Label sectionGrit, sectionScan, sectionFilter;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranswaveAudioProcessorEditor)
};