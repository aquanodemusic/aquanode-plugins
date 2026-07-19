#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class VocodeAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit VocodeAudioProcessorEditor(VocodeAudioProcessor&);
    ~VocodeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Drawing helpers
    void drawGrid(juce::Graphics&);
    void drawSpectrum(juce::Graphics&,
        const std::array<float, VocodeAudioProcessor::maxBins>&,
        juce::Colour, float alpha);

    // Coordinate helpers
    float binToX(int   bin) const;
    float magToY(float mag) const;
    juce::Rectangle<float> spectrumBounds() const;

    VocodeAudioProcessor& proc;

    static constexpr int kMaxBins = VocodeAudioProcessor::maxBins;
    std::array<float, kMaxBins> mainData{};
    std::array<float, kMaxBins> sideData{};
    std::array<float, kMaxBins> morphData{};

    // ---- Controls ----

    // Combos (row 1)
    juce::ComboBox fftSizeCombo;
    juce::ComboBox numBandsCombo;
    juce::Label    fftLbl;
    juce::Label    bandsLbl;

    // Analog mode toggle (row 1)
    juce::TextButton analogModeBtn{ "ANALOG" };

    // Knobs (row 2): MORPH  CLARITY  SMOOTH  GATE  FORMANT
    juce::Slider morphKnob, clarityKnob, smoothingKnob, gateKnob, formantKnob;
    juce::Label  morphLbl, clarityLbl, smoothingLbl, gateLbl, formantLbl;

    // ---- APVTS attachments ----
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BtnAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAtt>  fftSizeAtt;
    std::unique_ptr<ComboAtt>  numBandsAtt;
    std::unique_ptr<BtnAtt>    analogModeAtt;
    std::unique_ptr<SliderAtt> morphAtt;
    std::unique_ptr<SliderAtt> clarityAtt;
    std::unique_ptr<SliderAtt> smoothingAtt;
    std::unique_ptr<SliderAtt> gateAtt;
    std::unique_ptr<SliderAtt> formantAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocodeAudioProcessorEditor)
};