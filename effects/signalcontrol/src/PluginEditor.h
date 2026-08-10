#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
enum class DrawMode { Draw, Erase };

class DrawCanvas : public juce::Component
{
public:
    explicit DrawCanvas(SignalControlAudioProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void updateScanPosition(float pos, float cvVal)
    {
        currentScan = pos;
        currentCVVal = cvVal;
        repaint();
    }

    void clearCurve();

    void setDrawMode(DrawMode m) { activeMode = m; }
    DrawMode getDrawMode() const { return activeMode; }

private:
    SignalControlAudioProcessor& processor;
    float    currentScan = 0.0f;
    float    currentCVVal = -1.0f;
    DrawMode activeMode = DrawMode::Draw;

    bool  rightClickErasing = false;

    float lastPhase = -1.0f;
    float lastCV = -1.0f;

    void applyInterpolatedStroke(float phase1, float cv1, float phase2, float cv2);
    void eraseStroke(float phase1, float phase2);

    float xToPhase(int x) const;
    float yToCV(int y) const;
    int   phaseToX(float phase) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawCanvas)
};

//==============================================================================
class SignalControlAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit SignalControlAudioProcessorEditor(SignalControlAudioProcessor&);
    ~SignalControlAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildSyncDivisionMenu();
    void updateSyncVisibility();
    void setDrawModeUI(DrawMode m);
    void applyRateRange(bool is100x);

    //==============================================================================
    SignalControlAudioProcessor& audioProcessor;

    DrawCanvas drawCanvas;

    juce::Slider      rateKnob;
    juce::Label       rateLabel;

    juce::ToggleButton tempoSyncButton{ "Tempo Sync" };
    juce::ComboBox     divisionCombo;
    juce::Label        divisionLabel;

    juce::TextButton   clearButton{ "Clear" };
    juce::Label        cvReadout;

    // Draw mode buttons (ToggleButton so LookAndFeel glow applies automatically)
    juce::ToggleButton modeDrawButton{ "Draw" };
    juce::ToggleButton modeEraseButton{ "Erase" };

    // 100x rate multiplier toggle
    juce::ToggleButton rate100xButton{ "100x" };

    // Transform row — mirror and shift controls
    juce::TextButton   mirrorXButton{ "Mirror X" };
    juce::TextButton   mirrorYButton{ "Mirror Y" };

    // Shift sliders — values accumulated and applied on change
    juce::Slider       shiftXSlider;   // horizontal shift: wraps at edges
    juce::Slider       shiftYSlider;   // vertical shift: wraps at edges
    juce::Label        shiftXLabel;
    juce::Label        shiftYLabel;

    // Track previous slider values to compute delta on change
    float prevShiftX = 0.0f;
    float prevShiftY = 0.0f;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   rateAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   syncAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalControlAudioProcessorEditor)
};