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
    void drawSpectrum(juce::Graphics&,
        const std::array<float, SpectralCompareAudioProcessor::maxBins>&,
        juce::Colour colour, float alpha);
    void drawMorphedSpectrum(juce::Graphics&);
    void drawDelta(juce::Graphics&);
    void drawLabels(juce::Graphics&);
    void drawHoverInfo(juce::Graphics&);

    // Coordinate helpers
    float binToX(int   bin) const;
    int   xToBin(float x)   const;
    float magToY(float mag) const;
    float deltaDBtoY(float dB) const;
    float nyquist() const { return static_cast<float>(audioProcessor.currentSampleRate * 0.5f); }

    juce::Rectangle<int> canvasArea() const;

    float viewFreqMin = 20.0f;
    float viewFreqMax = 20000.0f;

    static constexpr float kDeltaRange   = 30.0f;
    static constexpr float kSilenceFloor = 1e-4f;

    // Mouse
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    float hoverX = -1.0f;

    SpectralCompareAudioProcessor& audioProcessor;

    static constexpr int maxDisplayBins = SpectralCompareAudioProcessor::maxBins;

    // Live display arrays
    std::array<float, maxDisplayBins> mainDisplayData      {};
    std::array<float, maxDisplayBins> sidechainDisplayData {};
    std::array<float, maxDisplayBins> morphDisplayData     {};
    // Editor-side IIR-smoothed delta in dB (avoids holding processor lock)
    std::array<float, maxDisplayBins> smoothedDeltaDisplay {};

    // Frozen snapshots
    std::array<float, maxDisplayBins> frozenMainDisplay      {};
    std::array<float, maxDisplayBins> frozenSidechainDisplay {};
    std::array<float, maxDisplayBins> frozenDeltaDisplay     {};
    std::array<float, maxDisplayBins> frozenMorphDisplay     {};

    // Visibility / freeze state
    bool showMain      = true;
    bool showSidechain = true;
    bool showDelta     = false;
    bool showMorph     = true;

    bool mainIsFrozen      = false;
    bool sidechainIsFrozen = false;
    bool deltaIsFrozen     = false;
    bool morphIsFrozen     = false;

    bool interpolate = true;

    // -----------------------------------------------------------------------
    // Right-panel controls
    // -----------------------------------------------------------------------
    juce::ComboBox fftSizeCombo;
    juce::Label    fftSizeLabel;

    juce::Slider morphSlider;
    juce::Label  morphLabel;

    juce::Slider claritySlider;
    juce::Label  clarityLabel;

    // Show + Freeze button pairs (laid out two-per-row)
    juce::TextButton showMainButton,      freezeMainButton;
    juce::TextButton showSidechainButton, freezeSidechainButton;
    juce::TextButton showDeltaButton,     freezeDeltaButton;
    juce::TextButton showMorphButton,     freezeMorphButton;

    juce::TextButton interpolateButton;

    juce::Slider mainSmoothSlider;    juce::Label mainSmoothLabel;
    juce::Slider sidechainSmoothSlider; juce::Label sidechainSmoothLabel;
    juce::Slider deltaSmoothSlider;   juce::Label deltaSmoothLabel;
    juce::Slider morphSmoothSlider;   juce::Label morphSmoothLabel;

    // Color pickers
    juce::Label      bgColorLabel,        gridColorLabel;
    juce::TextEditor bgColorInput,        gridColorInput;
    juce::Label      mainColorLabel,      sidechainColorLabel;
    juce::TextEditor mainColorInput,      sidechainColorInput;
    juce::Label      deltaColorLabel,     morphColorLabel;
    juce::TextEditor deltaColorInput,     morphColorInput;
    juce::Label      sidebarColorLabel;
    juce::TextEditor sidebarColorInput;

    juce::TextButton resetColorsButton;

    void applyBgColor();
    void applyGridColor();
    void applyMainColor();
    void applySidechainColor();
    void applyDeltaColor();
    void applyMorphColor();
    void applySidebarColor();

    // Propagates all colours to sliders, buttons, etc., then repaints
    void refreshUIColors();

    // Freq range knobs
    juce::Slider freqFromKnob; juce::Label freqFromLabel;
    juce::Slider freqToKnob;   juce::Label freqToLabel;

    std::unique_ptr<juce::LookAndFeel_V4> freqKnobLF;

    // -----------------------------------------------------------------------
    // APVTS Attachments
    // -----------------------------------------------------------------------
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ComboBoxAttachment> fftSizeAttachment;

    std::unique_ptr<SliderAttachment> morphAttachment;
    std::unique_ptr<SliderAttachment> clarityAttachment;

    std::unique_ptr<SliderAttachment> mainSmoothAttachment;
    std::unique_ptr<SliderAttachment> sidechainSmoothAttachment;
    std::unique_ptr<SliderAttachment> deltaSmoothAttachment;
    std::unique_ptr<SliderAttachment> morphSmoothAttachment;

    std::unique_ptr<SliderAttachment> freqFromAttachment;
    std::unique_ptr<SliderAttachment> freqToAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompareAudioProcessorEditor)
};
