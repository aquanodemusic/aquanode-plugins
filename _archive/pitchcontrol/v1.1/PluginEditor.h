/*
  ==============================================================================
    PitchControl – IIR Bell Filter Plugin
    Editor Header
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Custom LookAndFeel for the dark studio aesthetic
//==============================================================================
class PitchControlLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PitchControlLookAndFeel();

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override;

    void drawLabel (juce::Graphics& g, juce::Label& label) override;
};

//==============================================================================
// Piano keyboard component – one octave (C to B) with proper black keys
//==============================================================================
class PianoKeyboard : public juce::Component
{
public:
    explicit PianoKeyboard (PitchControlAudioProcessor& processor);

    void paint      (juce::Graphics& g) override;
    void resized    ()                  override;
    void mouseDown  (const juce::MouseEvent& e) override;

    // Returns the note class (0–11) hit at mouse position, or -1
    int  getNoteAtPosition (int x, int y) const;

private:
    PitchControlAudioProcessor& m_processor;

    static constexpr int kNumWhite = 7;
    static constexpr int kNumBlack = 5;

    // Semitone index within octave for each white/black key
    static constexpr int   kWhiteNotes[7]       = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int   kBlackNotes[5]        = { 1, 3, 6, 8, 10 };
    // Black key centre positions in units of white-key-widths
    static constexpr float kBlackKeyOffsets[5]   = { 0.67f, 1.67f, 3.67f, 4.67f, 5.67f };

    float m_whiteKeyWidth  { 0.0f };
    float m_blackKeyWidth  { 0.0f };
    float m_whiteKeyHeight { 0.0f };
    float m_blackKeyHeight { 0.0f };

    float blackKeyCentreX (int blackIdx) const noexcept;
    bool  hitTestBlack    (int blackIdx, int x, int y) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoKeyboard)
};

//==============================================================================
// Rotary knob with APVTS attachment and a top label
//==============================================================================
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob (const juce::String& labelText,
                  const juce::String& paramID,
                  PitchControlAudioProcessor& processor,
                  juce::LookAndFeel* laf = nullptr);

    void resized() override;

    juce::Slider slider;
    juce::Label  label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_attach;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabelledKnob)
};

//==============================================================================
// Combo-box note range selector with APVTS int attachment
//==============================================================================
class NoteRangeSelector : public juce::Component
{
public:
    NoteRangeSelector (const juce::String& labelText,
                       const juce::String& paramID,
                       PitchControlAudioProcessor& processor);

    void resized() override;

private:
    juce::Label    m_label;
    juce::ComboBox m_combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_attach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteRangeSelector)
};

//==============================================================================
// Main editor
//==============================================================================
class PitchControlAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit PitchControlAudioProcessorEditor (PitchControlAudioProcessor&);
    ~PitchControlAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized ()                override;

private:
    PitchControlAudioProcessor& audioProcessor;

    PitchControlLookAndFeel m_laf;

    PianoKeyboard      m_keyboard;
    LabelledKnob       m_depthKnob;
    LabelledKnob       m_qKnob;
    LabelledKnob       m_boostKnob;
    LabelledKnob       m_boostQKnob;
    NoteRangeSelector  m_rangeFrom;
    NoteRangeSelector  m_rangeTo;

    juce::TextButton m_wetOnlyButton{ "Wet Only" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_wetOnlyAttach;

    juce::Label m_titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchControlAudioProcessorEditor)
};
