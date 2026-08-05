#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralGateAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    SpectralGateAudioProcessorEditor(SpectralGateAudioProcessor&);
    ~SpectralGateAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    // ── Timer ─────────────────────────────────────────────────────────────────
    void timerCallback() override;

    // ── Drawing helpers ───────────────────────────────────────────────────────
    void drawFFT(juce::Graphics& g);

    // ── Mouse helper ──────────────────────────────────────────────────────────
    void updateControlFromMouse(const juce::MouseEvent&);

    // ── Coordinate conversion helpers ─────────────────────────────────────────
    // Pixel x (absolute) → FFT bin index [0, numBins)
    int   xToBinIndex(float x, int numBins) const;
    // FFT bin index → pixel x (absolute)
    float binIndexToX(int bin, int numBins) const;
    // Pixel y (absolute) → gate-curve value [0, 1]
    //   y == fftArea.getY()      → value 1.0  (fully gated)
    //   y == fftArea.getBottom() → value 0.0  (fully open)
    float yToGateValue(float y) const;
    // Gate-curve value → pixel y (absolute)
    float gateValueToY(float val) const;

    // ── Processor reference ───────────────────────────────────────────────────
    SpectralGateAudioProcessor& audioProcessor;

    // ── Live display buffers (filled each timer tick via getFFTData / getGateCurve) ─
    std::vector<float> fftDisplayData;
    std::vector<float> gateCurveData;

    // ── Layout ────────────────────────────────────────────────────────────────
    juce::Rectangle<int> fftArea;       // top section — spectrum + gate curve drawn here
    juce::Rectangle<int> controlArea;   // bottom strip — all controls live here

    // ── Controls ──────────────────────────────────────────────────────────────
    // FFT size selector
    juce::ComboBox fftSizeCombo;
    juce::Label    fftSizeLabel;

    // Invert Interval: normal  = mute everything OUTSIDE [lower, upper]
    //                  inverted = mute everything INSIDE  [lower, upper]
    juce::TextButton invertIntervalButton{ "Inv. Interval" };

    // Invert Gate:     normal  = mute bins whose magnitude is BELOW the curve
    //                  inverted = mute bins whose magnitude is ABOVE the curve
    juce::TextButton invertGateButton{ "Inv. Gate" };

    // Right-click anywhere in fftArea resets the gate curve to 0.
    // (No extra reset button needed — described in paint tooltip.)

    // ── Mouse-drag state ──────────────────────────────────────────────────────
    enum DraggedControl
    {
        None = -1,
        LowerFreq = 0,
        UpperFreq = 1,
        GateCurve = 2   // freehand gate-curve drawing
    };

    DraggedControl draggedControl{ None };

    // Previous drag position — used to interpolate gate-curve segments smoothly
    int   prevDragBin{ -1 };
    float prevDragVal{ 0.0f };

    // ── Shared log-scale constants ─────────────────────────────────────────────
    static constexpr float kNyquist = 22050.0f;
    static constexpr float kMinFreq = 20.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralGateAudioProcessorEditor)
};