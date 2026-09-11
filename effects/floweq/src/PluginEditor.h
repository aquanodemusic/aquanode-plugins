/*
    FlowEQ - PluginEditor.h

    Layout (900x600):
      - Outer frame: "bubblegum green", slightly metallic/aqua
      - Top half: EQ graph with Hertz grid, 3 draggable filter circles,
        3 coloured individual curves + one white sum curve
      - Bottom half: 3 CurveDrawers side by side (Freq / Gain / Q),
        baby blue, rounded, with shadow; each with a tidy two-row control
        header (time control + sync in row one, Smooth/WT/Draw buttons in
        row two) tinted in the filter colour
      - Reset button at the bottom
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
namespace FlowEQColours
{
    const juce::Colour outerGummi{ 0xff8fd9a8 }; // bubblegum green, outer
    const juce::Colour outerGummiDark{ 0xff5fae7c };
    const juce::Colour panelBabyBlue{ 0xffcfe9f7 };
    const juce::Colour panelBabyBlueLo{ 0xffb7dcef };
    const juce::Colour metalHighlight{ 0xffeaf7f4 };
    const juce::Colour textDark{ 0xff2c4a4a };

    const juce::Colour cyan{ 0xff34c6c6 };
    const juce::Colour teal{ 0xff2f9e6f };
    const juce::Colour brown{ 0xff8a5a3c };

    inline juce::Colour forFilter(int i)
    {
        switch (i) { case 0: return cyan; case 1: return teal; default: return brown; }
    }
}

//==============================================================================
class FlowLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FlowLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
        float sliderPosProportional, float rotaryStartAngle,
        float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Top graphic: Hertz/dB grid, 3 draggable circles, curves, sum curve
class TopEqGraph : public juce::Component, private juce::Timer
{
public:
    explicit TopEqGraph(FlowEQAudioProcessor& p);
    ~TopEqGraph() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    std::function<void(int)> onFilterSelected;

    // Record mode: while active, dragging a filter circle also writes the
    // dragged position into that filter's Freq and Gain curves (at the
    // index matching each curve's current live cycle phase), turning the
    // drag gesture into recorded curve data.
    void setRecordMode(bool shouldRecord) { recordMode = shouldRecord; }
    bool isRecordMode() const { return recordMode; }

private:
    void timerCallback() override;
    float freqToX(float freq) const;
    float xToFreq(float x) const;
    float gainToY(float gainDb) const;
    float yToGain(float y) const;
    int   hitTestCircle(juce::Point<float> pos) const;
    void  buildResponsePath(juce::Path& path, const std::vector<double>& magsDb);
    void  recordDragIntoCurves(int filterIndex, float freqHz, float gainDb);

    FlowEQAudioProcessor& proc;
    int draggingFilter = -1;
    bool recordMode = false;
    juce::Rectangle<float> graphArea;

    std::vector<double> freqTable;
};

//==============================================================================
// A drawable 128-point curve field (for Freq, Gain or Q), including a
// two-row control header: time control + sync toggle in row one, and
// Smooth / load-wavetable / draw-mode buttons in row two.
class CurveDrawer : public juce::Component, private juce::Timer
{
public:
    CurveDrawer(FlowEQAudioProcessor& p, CurveTarget target, juce::String captionText);
    ~CurveDrawer() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setActiveFilter(int filterIndex);

private:
    void timerCallback() override;
    void paintCurveArea(juce::Graphics& g, juce::Rectangle<float> area);
    void paintValueLine(const juce::MouseEvent& e);
    void updateSliderAttachment();
    void updateSyncVisibility();
    void updateModeControls();
    void chooseWavetableFile();
    void scrollFrame(int delta);
    void smoothDrawnCurve();

    FlowEQAudioProcessor& proc;
    CurveTarget target;
    juce::String caption;
    int activeFilter = 0;

    juce::Rectangle<float> curveArea;

    juce::Slider timeSlider{ juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::ComboBox syncDivBox;
    juce::TextButton syncToggle{ "S" };

    juce::TextButton loadWavetableButton{ "WT" };
    juce::TextButton drawModeButton{ "Draw" };
    juce::TextButton smoothButton{ "Smooth" }; // smooths the freehand-drawn curve
    juce::Slider frameKnob{ juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; // scrubs frame by frame through the wavetable

    juce::Rectangle<int> captionBounds; // set in resized(), never overlaps the buttons

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncDivAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int dragFrameOffset = 0; // for scrubbing through wavetable frames via drag
};

//==============================================================================
class FlowEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit FlowEQAudioProcessorEditor(FlowEQAudioProcessor&);
    ~FlowEQAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FlowEQAudioProcessor& audioProcessor;
    FlowLookAndFeel lookAndFeel;

    TopEqGraph topGraph;
    CurveDrawer freqDrawer;
    CurveDrawer gainDrawer;
    CurveDrawer qDrawer;

    juce::TextButton resetButton{ "Reset" };
    juce::TextButton recordButton{ "Rec" };
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlowEQAudioProcessorEditor)
};