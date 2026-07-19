/*
  ==============================================================================
    PitchControl – IIR Bell Filter + FFT Spectral Shift Plugin
    Editor Header
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class PitchControlLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PitchControlLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
        float sliderPos, float startAngle, float endAngle,
        juce::Slider&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
};

//==============================================================================
// SpectrumView
//==============================================================================
class SpectrumView : public juce::Component,
    private juce::Timer
{
public:
    explicit SpectrumView(PitchControlAudioProcessor& p);
    ~SpectrumView() override;

    void paint(juce::Graphics&) override;
    void resized()                override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // Coordinate helpers
    float binToX(int bin)    const;
    float freqToX(float hz)   const;
    float magToY(float norm) const;
    float xToFreq(float x)    const;

    // Drawing sub-routines
    void drawBackground(juce::Graphics&);
    void drawSpectrum(juce::Graphics&);
    void drawNoteLines(juce::Graphics&);
    void drawDiffOverlay(juce::Graphics&);   // "Show Differences" overlay
    void drawLabels(juce::Graphics&);
    void drawTooltip(juce::Graphics&);
    void drawToggleButton(juce::Graphics&);

    // EQ curve helpers (filter mode diff)
    // Returns the combined gain in dB at a given frequency from all active filters
    float computeFilterGainDB(float hz) const;

    // FFT bin-shift helpers (FFT mode diff)
    // Returns the destination x position for a source frequency under current params
    float computeShiftedX(float hz) const;

    PitchControlAudioProcessor& m_proc;

    std::array<float, kNumFFTBins> m_displayData{};
    float   m_hoverX{ -1.0f };
    float   m_silenceAlpha{ 0.0f };  // 0 = silent/fade-out, 1 = active audio
    bool    m_showDiff{ false }; // "Show Differences" toggle state

    // Bounding rect of the toggle button (for hit-testing in mouseDown)
    juce::Rectangle<int> m_toggleBtnRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumView)
};

//==============================================================================
// Piano keyboard
//==============================================================================
class PianoKeyboard : public juce::Component
{
public:
    explicit PianoKeyboard(PitchControlAudioProcessor&);
    void paint(juce::Graphics&)          override;
    void resized()                         override;
    void mouseDown(const juce::MouseEvent&) override;
    int  getNoteAtPosition(int x, int y)  const;

private:
    PitchControlAudioProcessor& m_processor;
    static constexpr int   kNumWhite = 7;
    static constexpr int   kNumBlack = 5;
    static constexpr int   kWhiteNotes[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int   kBlackNotes[5] = { 1, 3, 6, 8, 10 };
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
        PitchControlAudioProcessor& processor, juce::LookAndFeel* laf = nullptr);
    void resized() override;
    void setLabelText(const juce::String& text);
    void reattach(const juce::String& newParamID,
                  juce::AudioProcessorValueTreeState& apvts);
    juce::Slider slider;
    juce::Label  label;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_attach;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabelledKnob)
};

//==============================================================================
class NoteRangeSelector : public juce::Component
{
public:
    NoteRangeSelector(const juce::String& labelText, const juce::String& paramID,
        PitchControlAudioProcessor& processor);
    void resized() override;
private:
    juce::Label    m_label;
    juce::ComboBox m_combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_attach;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteRangeSelector)
};

//==============================================================================
class PitchControlAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit PitchControlAudioProcessorEditor(PitchControlAudioProcessor&);
    ~PitchControlAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized()                override;

private:
    void timerCallback() override { repaint(); }
    void updateModeUI(bool fftMode);

    PitchControlAudioProcessor& audioProcessor;
    PitchControlLookAndFeel     m_laf;

    // ---- Spectrum view (top of GUI) ----
    SpectrumView m_spectrumView;

    // ---- Piano keyboard ----
    PianoKeyboard m_keyboard;

    // ---- Knobs ----
    LabelledKnob m_depthKnob;
    LabelledKnob m_qKnob;
    LabelledKnob m_boostKnob;
    LabelledKnob m_boostQKnob;
    LabelledKnob m_shiftStrengthKnob;

    NoteRangeSelector m_rangeFrom;
    NoteRangeSelector m_rangeTo;

    juce::TextButton m_modeButton{ "Filter Mode" };
    juce::TextButton m_wetOnlyButton{ "Wet Only" };
    juce::TextButton m_dampenButton{ "Dampen Off-Notes" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_modeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_wetOnlyAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_dampenAttach;

    juce::Label m_titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchControlAudioProcessorEditor)
};