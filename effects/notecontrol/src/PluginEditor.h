#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class NoteControlAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit NoteControlAudioProcessorEditor(NoteControlAudioProcessor&);
    ~NoteControlAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // ---- Drawing ------------------------------------------------------------
    void drawBackground    (juce::Graphics&);
    void drawGrid          (juce::Graphics&);
    void drawNoteLabels    (juce::Graphics&);
    void drawCells         (juce::Graphics&);
    void drawHoverHighlight(juce::Graphics&);
    void drawSpectrumStrip (juce::Graphics&);
    void drawLegend        (juce::Graphics&);

    // ---- Layout helpers ------------------------------------------------------
    juce::Rectangle<int> gridArea()     const; // the 12x12 cell area
    juce::Rectangle<int> spectrumArea() const; // spectrum strip below grid
    juce::Rectangle<int> cellRect(int row, int col) const;
    std::pair<int,int>   cellAtPoint(juce::Point<int> p) const; // {row,col} or {-1,-1}

    // ---- Colours / styling --------------------------------------------------
    juce::Colour noteColour(int noteClass, float alpha = 1.0f) const;

    NoteControlAudioProcessor& proc;

    // ---- Cached display copies ----------------------------------------------
    std::array<float, NoteControlAudioProcessor::numBins> spectrumData{};

    // ---- Hover state --------------------------------------------------------
    int hoverRow = -1, hoverCol = -1;

    // ---- Frequency gate drag state ------------------------------------------
    // Gate positions are normalised log-scale 0–1, matching spectrum display.
    // Dragging either handle updates proc.gateStart / proc.gateEnd directly.
    enum class GateDrag { None, Start, End };
    GateDrag activeDrag = GateDrag::None;

    // Convert normalised gate pos → pixel x within spectrum strip
    int   gatePosToX (float t) const;
    float xToGatePos (int   x) const;
    // Returns which handle (if any) is close enough to grab at pixel x
    GateDrag hitTestGate(int x) const;

    // ---- Controls -----------------------------------------------------------
    juce::Slider  dryWetSlider;
    juce::Label   dryWetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttach;

    juce::TextButton resetButton;
    juce::TextButton enhanceButton;      // toggles soft Gaussian mode
    juce::TextButton pentaButton;        // apply C-major pentatonic snap
    juce::TextButton shiftUpButton;      // inputs scroll up   (rows -1)
    juce::TextButton shiftDownButton;    // inputs scroll down (rows +1)
    juce::TextButton shiftLeftButton;    // outputs scroll left  (cols -1)
    juce::TextButton shiftRightButton;   // outputs scroll right (cols +1)

    // ---- Note names  (static) -----------------------------------------------
    static constexpr const char* kNoteNames[12] =
        { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    // ---- Geometry constants -------------------------------------------------
    static constexpr int kTopBar     = 44;   // title / control strip height
    static constexpr int kCellSize   = 46;   // each grid cell
    static constexpr int kLabelW     = 38;   // left/top note label width
    static constexpr int kSpectrumH  = 70;   // spectrum strip height
    static constexpr int kBottomPad  = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteControlAudioProcessorEditor)
};
