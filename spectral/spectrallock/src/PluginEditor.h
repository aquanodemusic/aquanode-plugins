#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/** House look: dark glass, thin strokes, prismatic accents. */
class SpectralLockLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SpectralLockLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;

    static juce::Colour prism (float t);       // 0..1 -> spectrum colour
    static const juce::Colour bg, panel, line, text, dim, accent;
};

/** A rotary knob with its caption and value readout baked in. */
class SpectralLockKnob : public juce::Component
{
public:
    SpectralLockKnob (juce::AudioProcessorValueTreeState& state,
               const juce::String& paramID,
               const juce::String& caption);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String captionText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralLockKnob)
};

/** The 12 x 12 routing matrix plus the root-note strip above it.

    Columns, left to right  = incoming note (C .. B)
    Rows,    bottom to top  = outgoing note (C .. B)
*/
class MatrixComponent : public juce::Component,
                        private juce::Timer
{
public:
    explicit MatrixComponent (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    void applyScale (int scaleIndex);

private:
    void timerCallback() override;
    void handleMouse (juce::Point<int>);
    juce::Rectangle<float> cellBounds (int col, int rowFromBottom) const;
    juce::Rectangle<float> rootBounds (int col) const;

    juce::AudioProcessorValueTreeState& apvts;
    std::array<int, 12> current { { 0,1,2,3,4,5,6,7,8,9,10,11 } };
    int root = 0;
    int hoverCol = -1, hoverRow = -1;

    static constexpr int rootStripHeight = 30;
    static constexpr int gap = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatrixComponent)
};

/** Two-handle frequency range selector with note readouts and band mutes. */
class RangeSelector : public juce::Component,
                      private juce::Timer
{
public:
    explicit RangeSelector (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void pushValues();
    static juce::String hzToNote (float hz);

    juce::AudioProcessorValueTreeState& apvts;
    juce::Slider range { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
    juce::TextButton muteLow  { "LO" }, muteHigh { "HI" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aLow, aHigh;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RangeSelector)
};

/** Live view of the oscillator bank - one stripe per band, coloured by pitch. */
class BandView : public juce::Component,
                 private juce::Timer
{
public:
    explicit BandView (SpectralLockAudioProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    SpectralLockAudioProcessor& proc;
    std::array<float, SpectralLockAudioProcessor::numDisplayBins> smoothed {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandView)
};

class SpectralLockAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit SpectralLockAudioProcessorEditor (SpectralLockAudioProcessor&);
    ~SpectralLockAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SpectralLockAudioProcessor& proc;
    SpectralLockLookAndFeel lnf;

    BandView        bandView;
    MatrixComponent matrix;
    RangeSelector   rangeSel;

    juce::ComboBox   scaleBox;
    juce::TextButton midiButton   { "MIDI" };
    juce::TextButton freezeButton { "FREEZE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aMidi, aFreeze;

    juce::OwnedArray<SpectralLockKnob> knobs;
    SpectralLockKnob* addKnob (const juce::String& id, const juce::String& caption);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralLockAudioProcessorEditor)
};
