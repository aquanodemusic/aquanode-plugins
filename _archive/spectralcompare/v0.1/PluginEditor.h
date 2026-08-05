#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralCompareAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    SpectralCompareAudioProcessorEditor(SpectralCompareAudioProcessor&);
    ~SpectralCompareAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Drawing helpers
    void drawBackground (juce::Graphics&);
    void drawGrid       (juce::Graphics&);
    void drawSpectrum   (juce::Graphics&, const std::array<float, SpectralCompareAudioProcessor::maxBins>&,
                         juce::Colour colour, float alpha);
    void drawLabels     (juce::Graphics&);
    void drawHoverInfo  (juce::Graphics&);

    // Coordinate helpers
    float binToX  (int   bin) const;
    int   xToBin  (float x)   const;
    float magToY  (float mag) const;   // magnitude → pixel (log scale)
    float nyquist () const { return static_cast<float>(audioProcessor.currentSampleRate * 0.5f); }

    // Canvas area (everything except the right-panel strip)
    juce::Rectangle<int> canvasArea() const;

    // Mouse
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    float hoverX = -1.0f;

    SpectralCompareAudioProcessor& audioProcessor;

    static constexpr int maxDisplayBins = SpectralCompareAudioProcessor::maxBins;

    // 60-fps display copies (no lock held during paint)
    std::array<float, maxDisplayBins> mainDisplayData;
    std::array<float, maxDisplayBins> sidechainDisplayData;
    std::array<float, maxDisplayBins> frozenMainDisplay;  // snapshot held when freeze is on
    bool mainIsFrozen = false;

    // -----------------------------------------------------------------------
    // Right-panel controls
    // -----------------------------------------------------------------------
    juce::ComboBox  fftSizeCombo;
    juce::Label     fftSizeLabel;

    juce::TextButton freezeButton;

    // Color inputs
    juce::Label      bgColorLabel,   gridColorLabel;
    juce::TextEditor bgColorInput,   gridColorInput;
    juce::Label      mainColorLabel, sidechainColorLabel;
    juce::TextEditor mainColorInput, sidechainColorInput;
    juce::Label      sidebarColorLabel;
    juce::TextEditor sidebarColorInput;
    juce::TextButton resetColorsButton;

    void applyBgColor();
    void applyGridColor();
    void applyMainColor();
    void applySidechainColor();
    void applySidebarColor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompareAudioProcessorEditor)
};
