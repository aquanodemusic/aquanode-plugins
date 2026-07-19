#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class SampleFieldLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SampleFieldLookAndFeel();

    static constexpr uint32_t colBackground = 0xFFEAF9FC;
    static constexpr uint32_t colSurface = 0xFFFFFFFF;
    static constexpr uint32_t colCyan = 0xFF00C8E0;
    static constexpr uint32_t colCyanLight = 0xFFB0EEF5;
    static constexpr uint32_t colCyanDark = 0xFF0099B0;
    static constexpr uint32_t colText = 0xFF1A3A40;
    static constexpr uint32_t colTextMid = 0xFF4A7A85;
    static constexpr uint32_t colKnobTrack = 0xFFD0EFF5;
    static constexpr uint32_t colKnobFill = 0xFF00C8E0;
    static constexpr uint32_t colKnobThumb = 0xFFFFFFFF;

    void drawRotarySlider(juce::Graphics& g,
        int x, int y, int width, int height,
        float sliderPosProportional,
        float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool isMouseOverButton,
        bool isButtonDown) override;

    void drawButtonText(juce::Graphics& g,
        juce::TextButton& button,
        bool isMouseOverButton,
        bool isButtonDown) override;

    juce::Font getLabelFont(juce::Label&) override;
};

//==============================================================================
// Formats a parameter value for display
static inline juce::String formatParamValue(const juce::String& paramID, float value)
{
    if (paramID == "pan")
    {
        if (std::abs(value) < 0.005f) return "C";
        return juce::String(value, 2);
    }
    if (paramID == "rate")    return juce::String(value, 2) + "x";
    if (paramID == "vol")     return juce::String(value, 2);
    if (paramID == "time")
    {
        if (value >= 9.999f) return "Full";
        return juce::String(value, 2) + "s";
    }
    if (paramID == "panRnd" || paramID == "rateRnd" ||
        paramID == "volRnd" || paramID == "skip")
        return juce::String(juce::roundToInt(value * 100)) + "%";
    return juce::String(value, 2);
}

//==============================================================================
// Knob + name label + value box
class LabelledKnob : public juce::Component,
    private juce::Slider::Listener
{
public:
    LabelledKnob(const juce::String& labelText,
        const juce::String& parameterID,
        SampleFieldLookAndFeel& laf);
    ~LabelledKnob() override;

    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::String paramID;
    void sliderValueChanged(juce::Slider*) override;
    void updateValueLabel();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabelledKnob)
};

//==============================================================================
class SectionHeader : public juce::Component
{
public:
    explicit SectionHeader(const juce::String& t) : title(t) {}
    void paint(juce::Graphics& g) override;
    void resized() override {}
private:
    juce::String title;
};

//==============================================================================
// Small toggle button that cycles through tempo-lock steps: Off, 1/8 .. 8/8
class TempoLockButton : public juce::Component
{
public:
    TempoLockButton();

    // Returns 0 (off) or 1..8 (number of 8th notes)
    int  getCurrentSteps() const { return currentSteps; }
    void setCurrentSteps(int s);

    std::function<void(int)> onStepChanged;   // called when the user clicks

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    int  currentSteps = 0;   // 0 = off
    bool isOver = false;

    juce::String labelText() const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoLockButton)
};

//==============================================================================
class SampleFieldAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit SampleFieldAudioProcessorEditor(SampleFieldAudioProcessor&);
    ~SampleFieldAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SampleFieldAudioProcessor& audioProcessor;
    SampleFieldLookAndFeel     laf;

    juce::TextButton loadButton{ "LOAD SAMPLES" };
    juce::TextButton unloadButton{ "UNLOAD ALL" };
    juce::Label      sampleCountLabel;

    // Global knobs
    LabelledKnob knobPan{ "Pan",    "pan",  laf };
    LabelledKnob knobRate{ "Rate",   "rate", laf };
    LabelledKnob knobVol{ "Volume", "vol",  laf };
    LabelledKnob knobTime{ "Time",   "time", laf };

    // Tempo-lock toggle (sits below the Time knob)
    TempoLockButton tempoLockBtn;

    // Randomisation knobs
    LabelledKnob knobPanRnd{ "Pan Rnd",  "panRnd",  laf };
    LabelledKnob knobRateRnd{ "Rate Rnd", "rateRnd", laf };
    LabelledKnob knobVolRnd{ "Vol Rnd",  "volRnd",  laf };
    LabelledKnob knobSkip{ "Skip",     "skip",    laf };

    SectionHeader headerGlobal{ "GLOBAL" };
    SectionHeader headerRandom{ "RANDOM" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> attPan, attRate, attVol, attTime;
    std::unique_ptr<SliderAttachment> attPanRnd, attRateRnd, attVolRnd;
    std::unique_ptr<SliderAttachment> attSkip;

    std::unique_ptr<juce::FileChooser> fileChooser;

    void timerCallback() override;
    void updateSampleCountLabel();
    void paintBackground(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleFieldAudioProcessorEditor)
};