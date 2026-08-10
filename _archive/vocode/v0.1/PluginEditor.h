/*
  ==============================================================================
    PluginEditor.h  —  Vocode GUI
    Layout:   top strip  = controls (knobs, combo, toggle)
              bottom     = SpectrumBandEditor (spectrum + editable BW curve)
  ==============================================================================
*/
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** Custom LookAndFeel: dark industrial palette, teal accent. */
class VocodeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VocodeLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
        float sliderPos, float startAngle, float endAngle,
        juce::Slider&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
        bool highlighted, bool down) override;

    juce::Font getLabelFont(juce::Label&) override;
};

//==============================================================================
/** Draws three spectrum overlays (carrier / modulator / output) and an
 *  editable per-band bandwidth curve in a bottom strip.
 *
 *  Interaction:
 *    Left-drag  inside the bandwidth strip  →  paint bandwidth values
 *    Right-click anywhere                   →  reset bandwidth curve to 0.5
 */
class SpectrumBandEditor : public juce::Component,
    private juce::Timer
{
public:
    explicit SpectrumBandEditor(VocodeAudioProcessor& p);
    ~SpectrumBandEditor() override;

    void paint(juce::Graphics&)        override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    //----------------------------------------------------------------------
    void timerCallback() override;

    // Geometry helpers  (all coordinates are component-local)
    float bwStripTop()    const noexcept { return getHeight() * 0.60f; }
    float bwStripBottom() const noexcept { return (float)getHeight() - 1.f; }
    bool  isInBwStrip(float y) const noexcept
    {
        return y >= bwStripTop() && y <= bwStripBottom();
    }

    // Band / value ↔ pixel conversions within the bandwidth strip
    int   xToBand(float x)   const noexcept;
    float bandToX(int band)  const noexcept;
    float yToVal(float y)   const noexcept;   ///< y within bw strip → 0..1
    float valToY(float val) const noexcept;   ///< 0..1 → y within bw strip

    // Build a filled spectrum path in log-frequency / dB space
    juce::Path buildSpectrumPath(const float* mags, int numBins,
        double sampleRate,
        float w, float h,
        float dBmin = -78.f,
        float dBmax = 3.f) const;

    //----------------------------------------------------------------------
    VocodeAudioProcessor& proc;

    // Display buffers — sized to maxBins so they survive FFT-size changes
    std::array<float, VocodeAudioProcessor::maxBins> carrierMag{};
    std::array<float, VocodeAudioProcessor::maxBins> modMag{};
    std::array<float, VocodeAudioProcessor::maxBins> vocodeMag{};
    std::array<float, VocodeAudioProcessor::maxBands> bwCurve{};

    // Drag state
    bool  dragging = false;
    int   dragBandA = 0;
    float dragValA = 0.5f;
    int   dragBandB = 0;
    float dragValB = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumBandEditor)
};

//==============================================================================
/** Main plugin editor.
 *  Top strip  (kControlH px): five rotary sliders + FFT combo + self-vocode toggle
 *  Remainder              : SpectrumBandEditor
 */
class VocodeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    VocodeAudioProcessorEditor(VocodeAudioProcessor&);
    ~VocodeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized()                override;

private:
    VocodeAudioProcessor& audioProcessor;
    VocodeLookAndFeel     laf;

    // ---- Rotary knobs ----
    juce::Slider slAttack, slRelease, slMorph, slDryWet, slBands;
    juce::Label  lbAttack, lbRelease, lbMorph, lbDryWet, lbBands;

    // ---- FFT size selector ----
    juce::ComboBox cbFFT;
    juce::Label    lbFFT;

    // ---- Self-vocode toggle ----
    juce::ToggleButton btnSelf;

    // ---- APVTS attachments ----
    using SlAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BtnAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CbAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SlAtt>  attAttack, attRelease, attMorph, attDryWet, attBands;
    std::unique_ptr<BtnAtt> attSelf;
    std::unique_ptr<CbAtt>  attFFT;

    // ---- Visualiser ----
    SpectrumBandEditor spectrumEditor;

    // Height of the top control strip in pixels
    static constexpr int kControlH = 152;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocodeAudioProcessorEditor)
};