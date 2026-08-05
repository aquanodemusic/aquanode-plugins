#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// ============================================================================
//  Look and feel — dark panel, teal accent, matches the other aquanode plugins
// ============================================================================
class AquanodeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AquanodeLookAndFeel(SpectralCompressAudioProcessor& p);

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
        float sliderPos, float rotaryStartAngle,
        float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
        int buttonX, int buttonY, int buttonW, int buttonH,
        juce::ComboBox&) override;

    juce::Font getLabelFont(juce::Label&) override;

    // Re-reads the processor's palette (used when light mode is toggled)
    void refreshColours();

private:
    SpectralCompressAudioProcessor& proc;
};

// ============================================================================
//  Editor
// ============================================================================
class SpectralCompressAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit SpectralCompressAudioProcessorEditor(SpectralCompressAudioProcessor&);
    ~SpectralCompressAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    // Declared first so they outlive every component that references them
    SpectralCompressAudioProcessor& audioProcessor;
    AquanodeLookAndFeel lookAndFeel;

    void timerCallback() override;

    // ---- drawing -------------------------------------------------------
    void drawGrid(juce::Graphics&);
    void drawSpectra(juce::Graphics&);
    void drawGainReduction(juce::Graphics&);
    void drawCurves(juce::Graphics&);
    void drawHoverReadout(juce::Graphics&);
    void drawLegend(juce::Graphics&);
    void drawSidebarBackdrop(juce::Graphics&);

    // ---- geometry ------------------------------------------------------
    juce::Rectangle<int> canvasArea() const;
    int   sidebarWidth() const;
    float binToX(int bin) const;
    int   xToBin(float x) const;
    float hzToX(float hz) const;
    float dbToY(float dB) const;
    float yToDB(float y) const;
    float magToDB(float mag) const;

    static constexpr int   kBaseWidth = 1240;
    static constexpr int   kBaseHeight = 780;
    static constexpr int   kSidebarW = 340;
    static constexpr float kDisplayMaxDB = 6.0f;
    static constexpr float kDisplayMinDB = -100.0f;

    float scaleFactor = 1.0f;

    // ---- curve drawing state -------------------------------------------
    bool  isDrawing = false;
    bool  isErasing = false;
    bool  flatDraw = false;
    float lastDragX = -1.0f;
    float lastDragY = -1.0f;
    float hoverX = -1.0f;
    float hoverY = -1.0f;

    void applyDrawSegment(float x0, float y0, float x1, float y1);

    // ---- data snapshots -------------------------------------------------
    static constexpr int maxBins = SpectralCompressAudioProcessor::maxBins;
    std::array<float, maxBins> inputData{};
    std::array<float, maxBins> sidechainData{};
    std::array<float, maxBins> grData{};
    std::array<float, maxBins> curveData{};      // as stored, unshifted
    std::array<float, maxBins> shiftedCurve{};   // as displayed / as heard
    std::array<float, maxBins> scratchA{}, scratchB{};

    // Turns an array of per-bin dB values into a screen path, collapsing bins
    // that land on the same pixel column (peak-holding for spectra)
    void buildPathFromBins(juce::Path&, const float* dbValues, bool peak) const;

    // ---- controls -------------------------------------------------------
    struct KnobPack
    {
        juce::Slider slider;
        juce::Label  label;
    };

    KnobPack gainKnob, mixKnob, attackKnob, releaseKnob;
    KnobPack downOffsetKnob, downRatioKnob, downKneeKnob, downAmountKnob;
    KnobPack upOffsetKnob, upRatioKnob, upKneeKnob, upAmountKnob;
    KnobPack matchKnob, morphKnob, clarityKnob, scLinkKnob, stereoLinkKnob;
    KnobPack shiftKnob;

    juce::Slider visSmoothSlider;
    juce::Label  visSmoothLabel;

    juce::ComboBox fftCombo, overlapCombo, modeCombo;
    juce::Label    fftLabel, overlapLabel, modeLabel;

    juce::TextButton resetCurveButton, learnCurveButton, tiltDownButton, tiltUpButton;
    juce::TextButton showScButton, showGrButton, showOutButton, lightModeButton;
    juce::TextButton deltaButton;

    juce::Label titleLabel, subtitleLabel, scStatusLabel;
    juce::Label globalHeader, downHeader, upHeader, scHeader, curveHeader;

    bool showSidechain = true;
    bool showGR = true;
    bool showOutput = true;

    void setupKnob(KnobPack&, const juce::String& name, const juce::String& paramID);
    int  wrapBin(int bin) const;
    void setupHeader(juce::Label&, const juce::String& text);
    void refreshUIColors();
    std::vector<KnobPack*> allKnobs();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ButtonAttachment> deltaAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::unique_ptr<ComboAttachment> fftAttachment, overlapAttachment, modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompressAudioProcessorEditor)
};
