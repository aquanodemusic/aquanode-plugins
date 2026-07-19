#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralFilterAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    SpectralFilterAudioProcessorEditor(SpectralFilterAudioProcessor&);
    ~SpectralFilterAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown       (const juce::MouseEvent&) override;
    void mouseDrag       (const juce::MouseEvent&) override;
    void mouseUp         (const juce::MouseEvent&) override;
    void mouseMove       (const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void updateExportIREnabled();

    // Drawing
    void drawBackground (juce::Graphics&);
    void drawFreqRangeMask(juce::Graphics&);
    void drawFFTSpectrum(juce::Graphics&);
    void drawFilterCurve(juce::Graphics&);
    void drawPhaseCurve (juce::Graphics&);
    void drawFreqShiftCurve(juce::Graphics&);
    void drawPanCurve   (juce::Graphics&);
    void drawLabels     (juce::Graphics&);

    // Coordinate helpers
    int   xToBin(float x) const;
    float binToX(int bin) const;
    float yToDB (float y) const;
    float dBToY (float dB) const;
    float yToRad(float y) const;
    float radToY(float rad) const;
    float yToFreqOffset(float y) const;   // maps y to per-bin offset in bins
    float freqOffsetToY(float off) const;
    float yToPan(float y) const;           // maps y to pan [-1,+1]
    float panToY(float pan) const;
    float nyquist() const { return static_cast<float>(audioProcessor.currentSampleRate * 0.5); }
    // Returns the active [lo, hi] bin range, always lo<=hi, clamped to [0, numBins-1]
    std::pair<int,int> getActiveBinRange() const;

    // Curve drawing helpers
    void paintSegment(float x, float y);   // dispatches to whichever curve is active

    SpectralFilterAudioProcessor& audioProcessor;
    static constexpr int maxDisplayBins = SpectralFilterAudioProcessor::maxBins;

    // 60-fps display copies (no lock held during paint)
    std::array<float, maxDisplayBins> displayCurveDB;
    std::array<float, maxDisplayBins> displayPhaseRad;
    std::array<float, maxDisplayBins> displayFreqShift;
    std::array<float, maxDisplayBins> displayPan;
    std::array<float, maxDisplayBins> fftDisplayData;

    // Drawing state
    bool  isDrawing  = false;
    float lastDragX  = 0.f, lastDragY = 0.f;
    float hoverX     = -1.f;

    // Which curve the Edit combo is pointing to
    enum class EditMode { Filter, Phase, FreqShift, Pan };
    EditMode editMode = EditMode::Filter;

    // dB range
    static constexpr float maxDB =  24.0f;
    static constexpr float minDB = -96.0f;

    // -----------------------------------------------------------------------
    // Right-panel controls
    // -----------------------------------------------------------------------
    juce::ComboBox  fftSizeCombo;
    juce::Label     fftSizeLabel;

    juce::TextButton randomFilterButton, resetFilterButton;
    juce::TextButton randomPhaseButton,  resetPhaseButton;
    juce::TextButton randomFreqButton,   resetFreqButton;
    juce::TextButton randomPanButton,    resetPanButton;

    // Per-curve strength knobs (0–100%)
    juce::Slider filterStrengthKnob, phaseStrengthKnob, freqStrengthKnob, panStrengthKnob;

    // LookAndFeel objects for strength knobs (must outlive the sliders)
    std::unique_ptr<juce::LookAndFeel_V4> filterKnobLF, phaseKnobLF, freqKnobLF, panKnobLF;

    // APVTS attachments — keep knob values in sync with DAW automation
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> filterStrengthAttachment;
    std::unique_ptr<SliderAttachment> phaseStrengthAttachment;
    std::unique_ptr<SliderAttachment> freqStrengthAttachment;
    std::unique_ptr<SliderAttachment> panStrengthAttachment;

    juce::ComboBox  editModeCombo;
    juce::Label     editModeLabel;

    juce::TextButton wetOnlyButton;
    juce::TextButton exportIRButton;

    // Color inputs
    juce::Label      bgColorLabel,      gridColorLabel;
    juce::TextEditor bgColorInput,      gridColorInput;
    juce::Label      filterColorLabel,  phaseColorLabel;
    juce::TextEditor filterColorInput,  phaseColorInput;
    juce::Label      freqColorLabel,    panColorLabel;
    juce::TextEditor freqColorInput,    panColorInput;
    juce::Label      maskColorLabel;
    juce::TextEditor maskColorInput;
    juce::TextButton resetColorsButton;

    void updateBgColor();
    void updateGridColor();
    void updateFilterColor();
    void updatePhaseColor();
    void updateFreqColor();
    void updatePanColor();
    void updateMaskColor();

    // -----------------------------------------------------------------------
    // Bottom-strip sliders: Filter Shift, Phase Shift, Freq Shift, Pan Shift, Global Freq Shift
    // Each has: label, slider, auto button, speed input
    // Global Freq Shift also has a Wrap button
    // -----------------------------------------------------------------------
    struct ShiftStrip
    {
        juce::Label      label;
        juce::Slider     slider;
        juce::TextButton autoBtn;
        juce::Slider     speedKnob;
        std::array<float, maxDisplayBins> baseline;
        std::unique_ptr<juce::LookAndFeel_V2> lf;
        std::unique_ptr<juce::LookAndFeel_V4> speedLF;
    };

    ShiftStrip filterShift, phaseShift, freqShift, panShift, globalFreqShift;
    juce::TextButton globalFreqWrapButton;

    // APVTS attachments for speed knobs
    std::unique_ptr<SliderAttachment> filterSpeedAttachment;
    std::unique_ptr<SliderAttachment> phaseSpeedAttachment;
    std::unique_ptr<SliderAttachment> freqSpeedAttachment;
    std::unique_ptr<SliderAttachment> panSpeedAttachment;
    std::unique_ptr<SliderAttachment> binSpeedAttachment;

    // ---- Active frequency range knobs (bottom-left, above shift strips) ----
    juce::Slider  freqStartKnob, freqEndKnob;
    juce::Label   freqStartLabel, freqEndLabel;
    std::unique_ptr<juce::LookAndFeel_V4> freqStartKnobLF, freqEndKnobLF;
    std::unique_ptr<SliderAttachment> freqStartAttachment;
    std::unique_ptr<SliderAttachment> freqEndAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFilterAudioProcessorEditor)
};