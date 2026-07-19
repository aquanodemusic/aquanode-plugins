#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class PitchControlEQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PitchControlEQLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
        float sliderPos, float startAngle, float endAngle,
        juce::Slider&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
};

//==============================================================================
class PianoKeyboard : public juce::Component
{
public:
    explicit PianoKeyboard(PitchControlEQAudioProcessor&);
    void paint(juce::Graphics&)            override;
    void resized()                          override;
    void mouseDown(const juce::MouseEvent&) override;
    int  getNoteAtPosition(int x, int y)   const;

private:
    PitchControlEQAudioProcessor& m_processor;
    static constexpr int   kNumWhite = 7;
    static constexpr int   kNumBlack = 5;
    static constexpr int   kWhiteNotes[7]      = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int   kBlackNotes[5]      = { 1, 3, 6, 8, 10 };
    static constexpr float kBlackKeyOffsets[5] = { 0.67f, 1.67f, 3.67f, 4.67f, 5.67f };
    float m_whiteKeyWidth{}, m_blackKeyWidth{}, m_whiteKeyHeight{}, m_blackKeyHeight{};
    float blackKeyCentreX(int b) const noexcept;
    bool  hitTestBlack(int b, int x, int y) const noexcept;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoKeyboard)
};

//==============================================================================
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob(const juce::String& labelText, const juce::String& paramID,
        PitchControlEQAudioProcessor& processor, juce::LookAndFeel* laf = nullptr);
    void resized() override;
    juce::Slider slider;
    juce::Label  label;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_attach;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabelledKnob)
};

//==============================================================================
class RangeSlider : public juce::Component
{
public:
    RangeSlider(const juce::String& labelText, const juce::String& paramID,
                PitchControlEQAudioProcessor& processor);
    void resized() override;

private:
    juce::Label  m_label;
    juce::Slider m_slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_attach;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RangeSlider)
};

//==============================================================================
class PitchControlEQAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit PitchControlEQAudioProcessorEditor(PitchControlEQAudioProcessor&);
    ~PitchControlEQAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized()               override;

private:
    void timerCallback() override { repaint(); }

    PitchControlEQAudioProcessor& audioProcessor;
    PitchControlEQLookAndFeel     m_laf;

    PianoKeyboard m_keyboard;

    // Row 1: per-note dampen + boost + output gain
    LabelledKnob m_depthKnob;
    LabelledKnob m_qKnob;
    LabelledKnob m_boostKnob;
    LabelledKnob m_boostQKnob;
    LabelledKnob m_outputGainKnob;

    // Row 2: global bell + chorus
    LabelledKnob m_bellDBKnob;
    LabelledKnob m_bellBWKnob;
    LabelledKnob m_bellFreqKnob;
    LabelledKnob m_chorusRateKnob;
    LabelledKnob m_chorusDepthKnob;
    LabelledKnob m_chorusMixKnob;

    // Range sliders (shared column)
    RangeSlider m_rangeFrom;
    RangeSlider m_rangeTo;

    juce::Label m_titleLabel;
    juce::Label m_row1Label;
    juce::Label m_row2Label;

    juce::TextButton m_midiModeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_midiModeAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchControlEQAudioProcessorEditor)
};
