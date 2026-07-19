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
    void drawFxRegionBars(juce::Graphics&);
    void drawSpectrum(juce::Graphics&,
        const std::array<float, SpectralCompareAudioProcessor::maxBins>&,
        juce::Colour colour, float alpha);
    void drawMorphedSpectrum(juce::Graphics&);
    void drawOutputSpectrum(juce::Graphics&);
    void drawDelta(juce::Graphics&);
    void drawLabels(juce::Graphics&);
    void drawHoverInfo(juce::Graphics&);

    // Coordinate helpers
    float binToX(int   bin) const;
    int   xToBin(float x)   const;
    float magToY(float mag) const;
    float deltaDBtoY(float dB) const;
    float nyquist() const { return static_cast<float>(audioProcessor.currentSampleRate * 0.5f); }
    juce::Rectangle<int> canvasArea()   const;
    juce::Rectangle<int> spectrumArea() const;  // top portion when heatmap on, else full canvas
    juce::Rectangle<int> heatmapArea()  const;  // bottom portion for waterfall

    float viewFreqMin = 20.0f;
    float viewFreqMax = 20000.0f;

    static constexpr float kDeltaRange = 30.0f;
    static constexpr float kSilenceFloor = 1e-4f;

    // GUI scaling
    static constexpr int kBaseWidth = 1220;
    static constexpr int kBaseHeight = 660;
    float scaleFactor = 1.0f;

    // Sidebar visibility
    bool sidebarVisible = true;
    juce::TextButton toggleSidebarButton;

    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    float hoverX = -1.0f;

    // -----------------------------------------------------------------------
    // Spectral filter curve drawing state
    // -----------------------------------------------------------------------
    static constexpr float kFilterRange = 60.0f;   // ±60 dB filter range centered on canvas
    // 0 = Draw Main Filter, 1 = Draw Side Filter, 2 = Draw Gate, 3 = Draw Enhance
    int   drawTarget = 0;
    bool  isDrawingFilter = false;
    float filterLastDragX = -1.0f;
    float filterLastDragY = -1.0f;

    SpectralCompareAudioProcessor& audioProcessor;
    static constexpr int maxDisplayBins = SpectralCompareAudioProcessor::maxBins;

    std::array<float, maxDisplayBins> mainFilterDisplay{};
    std::array<float, maxDisplayBins> sidechainFilterDisplay{};
    std::array<float, maxDisplayBins> gateFilterDisplay{};
    std::array<float, maxDisplayBins> enhanceFilterDisplay{};

    // Coordinate helpers for filter drawing (canvas-relative)
    int   filterXToBin(float x) const;
    float filterYToDB(float y) const;
    float filterDBtoY(float dB) const;
    float spectrumYToDB(float y) const;   // Y → dB on spectrum scale (0 top, -90 bottom)
    float spectrumDBtoY(float dB) const;  // dB on spectrum scale → canvas Y
    void  drawFilterCurves(juce::Graphics&);
    void  drawHeatmap(juce::Graphics&);
    void  updateHeatmap();

    // -----------------------------------------------------------------------
    // Waterfall heatmap
    // -----------------------------------------------------------------------
    bool             showHeatmap = false;
    float            heatmapSplitRatio = 0.60f;   // fraction of canvas height for spectrum
    bool             isDraggingSplit = false;
    juce::TextButton heatmapToggleButton;
    juce::Image      heatmapImage;   // scrolling ARGB render buffer

    // Live display arrays
    std::array<float, maxDisplayBins> mainDisplayData{};
    std::array<float, maxDisplayBins> sidechainDisplayData{};
    std::array<float, maxDisplayBins> morphDisplayData{};
    std::array<float, maxDisplayBins> outputDisplayData{};
    std::array<float, maxDisplayBins> smoothedDeltaDisplay{};

    // Frozen snapshots
    std::array<float, maxDisplayBins> frozenMainDisplay{};
    std::array<float, maxDisplayBins> frozenSidechainDisplay{};
    std::array<float, maxDisplayBins> frozenDeltaDisplay{};
    std::array<float, maxDisplayBins> frozenMorphDisplay{};
    std::array<float, maxDisplayBins> frozenOutputDisplay{};

    // Visibility / freeze flags
    bool showMain = true;
    bool showSidechain = true;
    bool showDelta = false;
    bool showMorph = true;
    bool showOutput = true;

    bool mainIsFrozen = false;
    bool sidechainIsFrozen = false;
    bool deltaIsFrozen = false;
    bool morphIsFrozen = false;
    bool outputIsFrozen = false;

    bool interpolate = true;

    // -----------------------------------------------------------------------
    // Section header labels
    // -----------------------------------------------------------------------
    juce::Label generalLabel, visualsLabel, audioLabel, morphingLabel;

    // -----------------------------------------------------------------------
    // General Controls
    // -----------------------------------------------------------------------
    juce::ComboBox fftSizeCombo;
    juce::Slider   freqFromKnob;
    juce::Slider   freqToKnob;
    juce::Label    fftSizeLabel;
    juce::Label    freqFromLabel, freqToLabel;   // "From Hz" / "To Hz" column headers

    // -----------------------------------------------------------------------
    // Visuals — Show / Speed / Freeze triplets
    // -----------------------------------------------------------------------
    juce::TextButton showMainButton, freezeMainButton;
    juce::TextButton showSidechainButton, freezeSidechainButton;
    juce::TextButton showDeltaButton, freezeDeltaButton;
    juce::TextButton showMorphButton, freezeMorphButton;
    juce::TextButton showOutputButton, freezeOutputButton;

    juce::Label SmoothingLabel;

    juce::Slider mainSmoothSlider;
    juce::Slider sidechainSmoothSlider;
    juce::Slider deltaSmoothSlider;
    juce::Slider morphSmoothSlider;
    juce::Slider outputSmoothSlider;

    // Color editors — background = the current color value
    juce::TextEditor bgColorInput, gridColorInput, sidebarColorInput;
    juce::TextEditor mainColorInput, sidechainColorInput, deltaColorInput;
    juce::TextEditor morphColorInput, outputColorInput, textColorInput;

    // Tiny column labels above each color input (3 rows × 3 cols = 9)
    // Order: BG, Grid, Sidebar | Main, Sidechain, Delta | Morph, Output, Text
    juce::Label colorCatLabels[9];

    juce::TextButton resetVisualsButton;
    juce::TextButton resetColorsButton;
    juce::TextButton interpolateButton;

    // -----------------------------------------------------------------------
    // Audio section
    // -----------------------------------------------------------------------
    juce::TextButton hearDeltaButton;
    juce::TextButton monitorSideButton;

    juce::TextButton gateEnableButton;

    juce::Slider     gateBinStartKnob;
    juce::Slider     gateBinEndKnob;
    juce::Label      gateLowHzLabel, gateHighHzLabel;

    juce::TextButton enhanceEnableButton;
    juce::TextButton enhanceModeButton;
    juce::Label      enhLowHzLabel, enhHighHzLabel;

    juce::Slider     enhanceBinStartKnob;
    juce::Slider     enhanceBinEndKnob;

    // -----------------------------------------------------------------------
    // Morphing section
    // -----------------------------------------------------------------------
    juce::Slider     morphSlider;
    juce::Label      morphAmtLabel;
    juce::Slider     claritySlider;
    juce::Label      clarityAmtLabel;
    juce::TextButton morphSmoothToggle;
    juce::ComboBox   drawTargetCombo;   // Draw Main / Draw Side / Draw Gate / Draw Enhance
    juce::TextButton resetDrawsButton;

    // -----------------------------------------------------------------------
    // Color helpers
    // -----------------------------------------------------------------------
    void applyBgColor();
    void applyGridColor();
    void applyMainColor();
    void applySidechainColor();
    void applyDeltaColor();
    void applyMorphColor();
    void applyOutputColor();
    void applySidebarColor();
    void applyTextColor();
    void refreshUIColors();

    // -----------------------------------------------------------------------
    // APVTS attachments
    // -----------------------------------------------------------------------
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboBoxAttachment> fftSizeAttachment;
    std::unique_ptr<SliderAttachment>   freqFromAttachment;
    std::unique_ptr<SliderAttachment>   freqToAttachment;
    std::unique_ptr<SliderAttachment>   mainSmoothAttachment;
    std::unique_ptr<SliderAttachment>   sidechainSmoothAttachment;
    std::unique_ptr<SliderAttachment>   deltaSmoothAttachment;
    std::unique_ptr<SliderAttachment>   morphSmoothAttachment;
    std::unique_ptr<SliderAttachment>   outputSmoothAttachment;
    std::unique_ptr<SliderAttachment>   morphAttachment;
    std::unique_ptr<SliderAttachment>   clarityAttachment;
    std::unique_ptr<ButtonAttachment>   morphSmoothAudioAttachment;
    std::unique_ptr<ButtonAttachment>   hearDeltaAttachment;
    std::unique_ptr<ButtonAttachment>   monitorSideAttachment;
    std::unique_ptr<ButtonAttachment>   gateEnableAttachment;
    std::unique_ptr<SliderAttachment>   gateBinStartAttachment;
    std::unique_ptr<SliderAttachment>   gateBinEndAttachment;
    std::unique_ptr<ButtonAttachment>   enhanceEnableAttachment;
    std::unique_ptr<ButtonAttachment>   enhanceModeAttachment;
    std::unique_ptr<SliderAttachment>   enhanceBinStartAttachment;
    std::unique_ptr<SliderAttachment>   enhanceBinEndAttachment;
    std::unique_ptr<ButtonAttachment>   interpolateAttachment;
    std::unique_ptr<ButtonAttachment>   showMainAttachment;
    std::unique_ptr<ButtonAttachment>   showSidechainAttachment;
    std::unique_ptr<ButtonAttachment>   showDeltaAttachment;
    std::unique_ptr<ButtonAttachment>   showMorphAttachment;
    std::unique_ptr<ButtonAttachment>   showOutputAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompareAudioProcessorEditor)
};