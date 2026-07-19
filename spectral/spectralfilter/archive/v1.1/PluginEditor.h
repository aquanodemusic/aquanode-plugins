#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralFilterAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    SpectralFilterAudioProcessorEditor (SpectralFilterAudioProcessor&);
    ~SpectralFilterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown       (const juce::MouseEvent& event) override;
    void mouseDrag       (const juce::MouseEvent& event) override;
    void mouseUp         (const juce::MouseEvent& event) override;
    void mouseMove       (const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;

    void drawBackground  (juce::Graphics& g);
    void drawFFTSpectrum (juce::Graphics& g);
    void drawFilterCurve (juce::Graphics& g);
    void drawLabels      (juce::Graphics& g);

    // Coordinate conversion — all use the actual sample rate from the processor
    int   xToBin (float x) const;
    float binToX (int bin) const;
    float yToDB  (float y) const;
    float dBToY  (float dB) const;
    float nyquist() const { return static_cast<float>(audioProcessor.currentSampleRate * 0.5); }

    // Paint curve from last drag position to (x, y)
    void paintCurveSegment(float x, float y);

    SpectralFilterAudioProcessor& audioProcessor;

    static constexpr int maxDisplayBins = SpectralFilterAudioProcessor::maxBins;

    // Local copies updated from the processor at 60 fps — no locks held during paint
    std::array<float, maxDisplayBins> displayCurveDB;
    std::array<float, maxDisplayBins> fftDisplayData;

    // Drawing state
    bool  isDrawing = false;
    float lastDragX = 0.f;
    float lastDragY = 0.f;
    float hoverX    = -1.f;  // -1 means not hovering

    // dB range shown in the editor
    static constexpr float maxDB =  24.0f;
    static constexpr float minDB = -96.0f;  // visual floor (-144 dB = silence in processor)

    juce::ComboBox fftSizeCombo;
    juce::Label fftSizeLabel;
    
    // Color customization controls
    juce::Label bgColorLabel;
    juce::TextEditor bgColorInput;
    juce::Label curveColorLabel;
    juce::TextEditor curveColorInput;
    juce::Label gridColorLabel;
    juce::TextEditor gridColorInput;
    juce::Label spectrumColorLabel;
    juce::TextEditor spectrumColorInput;
    
    juce::TextButton randomButton;
    juce::TextButton resetFilterButton;
    juce::TextButton wetOnlyButton;
    juce::TextButton exportIRButton;
    juce::TextButton resetColorsButton;

    // Filter curve shift slider (shifts the dB curve values)
    juce::Slider shiftSlider;
    juce::Label  shiftLabel;
    std::array<float, maxDisplayBins> shiftBaseline;
    std::unique_ptr<juce::LookAndFeel_V2> shiftSliderLF;

    // Filter Shift auto controls
    juce::TextButton autoShiftFilterButton;
    juce::TextEditor autoShiftFilterSpeedInput;

    // Bin Shift auto controls
    juce::TextButton autoShiftBinButton;
    juce::TextEditor autoShiftBinSpeedInput;

    // Bin shift slider (frequency shift effect on audio)
    juce::Slider     binShiftSlider;
    juce::Label      binShiftLabel;
    juce::TextButton binShiftWrapButton;
    std::unique_ptr<juce::LookAndFeel_V2> binShiftSliderLF;
    
    void updateBackgroundColor();
    void updateCurveColor();
    void updateGridColor();
    void updateSpectrumColor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFilterAudioProcessorEditor)
};
