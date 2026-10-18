#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Lush tropical green look and feel
class VibrantLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VibrantLookAndFeel();

    void setGlowEnabled(bool enabled) { glowEnabled = enabled; }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;

    juce::Label* createSliderTextBox(juce::Slider& slider) override;

    void drawLabel(juce::Graphics& g, juce::Label& label) override;

private:
    // Lush natural bamboo color palette
    juce::Colour bgDark{ 12, 38, 18 };
    juce::Colour bgMid{ 20, 58, 28 };
    juce::Colour emeraldGreen{ 0, 201, 87 };
    juce::Colour limeGreen{ 50, 255, 50 };
    juce::Colour forestGreen{ 34, 139, 34 };
    juce::Colour mintGreen{ 152, 255, 152 };
    juce::Colour yellowGreen{ 154, 205, 50 };
    juce::Colour aquaGreen{ 127, 255, 212 };

    bool glowEnabled = true;
};

//==============================================================================
// EQ Curve Analyzer Component - shows real-time frequency response
class EQCurveComponent : public juce::Component, private juce::Timer
{
public:
    EQCurveComponent(PhorestAudioProcessor& p);
    void paint(juce::Graphics& g) override;
    void timerCallback() override;
    void resized() override;

    void setGlowEnabled(bool enabled) { glowEnabled = enabled; repaint(); }

private:
    PhorestAudioProcessor& processor;
    bool glowEnabled = true;

    // Frequency grid
    void drawFrequencyGrid(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawGainGrid(juce::Graphics& g, juce::Rectangle<float> bounds);

    // Helper to convert frequency to x position
    float frequencyToX(float freq, float width);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQCurveComponent)
};

//==============================================================================
// Custom rotary slider with modern design
class CustomRotarySlider : public juce::Component
{
public:
    CustomRotarySlider(const juce::String& labelText, const juce::String& paramID,
        juce::AudioProcessorValueTreeState& apvts, bool isToggle = false);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    juce::Label valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool isToggleParam;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomRotarySlider)
};

//==============================================================================
class PhorestAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::AudioProcessorValueTreeState::Listener,
    private juce::Timer
{
public:
    PhorestAudioProcessorEditor(PhorestAudioProcessor&);
    ~PhorestAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

    // Parameter listener callback
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Animation
    void timerCallback() override;

private:
    PhorestAudioProcessor& audioProcessor;
    VibrantLookAndFeel vibrantLookAndFeel;

    // UI Components
    CustomRotarySlider sweepFreqSlider;
    CustomRotarySlider minDepthSlider;
    CustomRotarySlider maxDepthSlider;
    CustomRotarySlider freqRangeSlider;
    CustomRotarySlider stereoSlider;
    CustomRotarySlider stagesSlider;
    CustomRotarySlider feedbackSlider;
    CustomRotarySlider dryWetSlider;
    CustomRotarySlider lfoShapeSlider;
    CustomRotarySlider manualPosSlider;
    CustomRotarySlider detuneSlider;
    CustomRotarySlider dryCancelSlider;

    EQCurveComponent eqCurveDisplay;

    juce::Label titleLabel;

    // Glow toggle button — drawn manually, top-right corner
    bool glowEnabled = true;
    juce::TextButton glowButton;

    // Animated background circle phases (radians, incremented each timer tick)
    float circlePhase0 = 0.0f;
    float circlePhase1 = 1.1f;  // offset so they start at different positions
    float circlePhase2 = 2.4f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhorestAudioProcessorEditor)
};