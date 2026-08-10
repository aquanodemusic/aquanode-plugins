#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

/*  SpectrogramEditor
    =================
    Renders a rolling, log-frequency spectrogram with:

      • Blue/cyan gradient colour map
      • Horizontal dashed markers at each C note (C1–C6 in 20–2000 Hz)
      • Time scrolls right-to-left; newest column on the right
      • 60 fps repaint via juce::Timer

    CONTROLS (top-right corner)
    ---------------------------
      GATE    – per-frame relative gate threshold (−70 … −20 dB, def −40 dB)
                Tighter = fewer phantom sidelobe bands; too tight silences quiet notes.
      BRIGHT  – display gamma (0.30 … 1.20, def 0.65)
                Lower = brighter midtones; higher = more contrast.
      DECAY   – running-peak decay rate (slow … fast, mapped to 0.9990 … 1.0)
                Slow decay keeps bright long-term average; fast decay adapts quickly.
*/
class SpectrogramEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SpectrogramEditor (SpectrogramProcessor&);
    ~SpectrogramEditor() override;

    void paint   (juce::Graphics&) override;
    void resized ()                override;

private:
    SpectrogramProcessor& proc;

    juce::Image spectrogramImage;
    int         lastReadHead = 0;
    float       runningPeak  = 1e-6f;

    // ---- Controls ----
    juce::Slider gateKnob;
    juce::Slider brightKnob;
    juce::Slider decayKnob;

    juce::Label  gateLabel;
    juce::Label  brightLabel;
    juce::Label  decayLabel;

    void setupKnob (juce::Slider& s, juce::Label& l,
                    const juce::String& labelText,
                    double minV, double maxV, double defV,
                    std::function<void(double)> onChange);

    // ---- Timer ----
    void timerCallback() override;

    // ---- Rendering helpers ----
    void  drawNewColumns    (int from, int to);
    void  drawCNoteOverlay  (juce::Graphics&) const;
    float freqToY           (float freqHz)    const noexcept;

    static juce::Colour spectralColour (float t) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramEditor)
};
