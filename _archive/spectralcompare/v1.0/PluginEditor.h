#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralCompareAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    SpectralCompareAudioProcessorEditor(SpectralCompareAudioProcessor&);
    ~SpectralCompareAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Drawing helpers
    void drawBackground(juce::Graphics&);
    void drawGrid(juce::Graphics&);
    void drawSpectrum(juce::Graphics&, const std::array<float, SpectralCompareAudioProcessor::maxBins>&,
        juce::Colour colour, float alpha);
    void drawDelta(juce::Graphics&);
    void drawDifference(juce::Graphics&);
    void drawLabels(juce::Graphics&);
    void drawHoverInfo(juce::Graphics&);

    // Coordinate helpers — all respect viewFreqMin / viewFreqMax
    float binToX(int   bin) const;
    int   xToBin(float x)   const;
    float magToY(float mag) const;
    float deltaDBtoY(float dB)  const;
    float nyquist() const { return static_cast<float>(audioProcessor.currentSampleRate * 0.5f); }

    // Canvas area = full area minus right panel
    juce::Rectangle<int> canvasArea() const;

    // Current visible frequency range (Hz) — drives binToX / xToBin
    float viewFreqMin = 20.0f;
    float viewFreqMax = 20000.0f;   // updated to nyquist at first paint if needed

    static constexpr float kDeltaRange = 30.0f;
    static constexpr float kSilenceFloor = 1e-4f;

    // Mouse
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    float hoverX = -1.0f;

    SpectralCompareAudioProcessor& audioProcessor;

    static constexpr int maxDisplayBins = SpectralCompareAudioProcessor::maxBins;

    std::array<float, maxDisplayBins> mainDisplayData;
    std::array<float, maxDisplayBins> sidechainDisplayData;
    std::array<float, maxDisplayBins> frozenMainDisplay;
    std::array<float, maxDisplayBins> frozenSidechainDisplay;
    bool mainIsFrozen = false;
    bool sidechainIsFrozen = false;

    bool deltaMode = false;
    bool diffMode = false;
    bool interpolate = true;   // true = smooth line, false = discrete vertical bars

    // -----------------------------------------------------------------------
    // Right-panel controls
    // -----------------------------------------------------------------------
    juce::ComboBox  fftSizeCombo;
    juce::Label     fftSizeLabel;

    juce::TextButton freezeMainButton;
    juce::TextButton freezeSidechainButton;
    juce::TextButton deltaButton;
    juce::TextButton diffButton;
    juce::TextButton interpolateButton;

    juce::Slider mainSmoothSlider;
    juce::Label  mainSmoothLabel;
    juce::Slider sidechainSmoothSlider;
    juce::Label  sidechainSmoothLabel;

    juce::Label      bgColorLabel, gridColorLabel;
    juce::TextEditor bgColorInput, gridColorInput;
    juce::Label      mainColorLabel, sidechainColorLabel;
    juce::TextEditor mainColorInput, sidechainColorInput;
    juce::Label      deltaColorLabel;
    juce::TextEditor deltaColorInput;
    juce::Label      diffColorLabel;
    juce::TextEditor diffColorInput;
    juce::Label      sidebarColorLabel;
    juce::TextEditor sidebarColorInput;
    juce::TextButton resetColorsButton;

    void applyBgColor();
    void applyGridColor();
    void applyMainColor();
    void applySidechainColor();
    void applyDeltaColor();
    void applyDiffColor();
    void applySidebarColor();

    // -----------------------------------------------------------------------
    // Freq range knobs (sidebar rotary sliders)
    // -----------------------------------------------------------------------
    juce::Slider freqFromKnob;
    juce::Label  freqFromLabel;
    juce::Slider freqToKnob;
    juce::Label  freqToLabel;

    std::unique_ptr<juce::LookAndFeel_V4> freqKnobLF;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompareAudioProcessorEditor)
};