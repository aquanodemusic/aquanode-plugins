#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class CombWeaveAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    CombWeaveAudioProcessorEditor(CombWeaveAudioProcessor&);
    ~CombWeaveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    CombWeaveAudioProcessor& proc;

    // ── Attachment typedefs ──────────────────────────────────────────
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // ── Row 1 ────────────────────────────────────────────────────────
    juce::Slider     amountSlider, hpFreqSlider;
    juce::Label      amountLabel, hpFreqLabel;
    juce::TextButton spreadModeBtn{ "Linear" };   // toggles label & attachment

    std::unique_ptr<SliderAtt> amountAtt, hpFreqAtt;
    std::unique_ptr<ButtonAtt> spreadModeAtt;

    // ── Row 2 ────────────────────────────────────────────────────────
    juce::Slider     spreadSlider, volumeSlider;
    juce::Label      spreadLabel, volumeLabel;
    juce::TextButton bidirBtn{ "Bidirectional" };

    std::unique_ptr<SliderAtt> spreadHzAtt, spreadRatioAtt;   // swapped at runtime
    std::unique_ptr<SliderAtt> volumeAtt;
    std::unique_ptr<ButtonAtt> bidirAtt;

    // ── Row 3 ────────────────────────────────────────────────────────
    juce::ComboBox   freqModeBox;
    juce::Label      freqModeLabel;
    juce::Slider     attackSlider, releaseSlider, rolloffSlider, tuneSlider;
    juce::Label      attackLabel, releaseLabel, rolloffLabel, tuneLabel;

    std::unique_ptr<ComboAtt>  freqModeAtt;
    std::unique_ptr<SliderAtt> attackAtt, releaseAtt, rolloffAtt, tuneAtt;

    // ── Title ────────────────────────────────────────────────────────
    juce::Label titleLabel;
    juce::Label subtitleLabel;

    // ── Spread mode state tracking ───────────────────────────────────
    bool lastSpreadState = false;   // false = linear, true = exp

    // ── Helpers ──────────────────────────────────────────────────────
    void setupKnob(juce::Slider&, juce::Label&, const juce::String& name);
    void setupBtn(juce::TextButton&, bool toggleable = true);
    void drawDisplay(juce::Graphics& g, juce::Rectangle<int> area);
    void syncSpreadAttachment(bool isExp);

    static constexpr int kPad = 15;
    static constexpr int kDispH = 185;
    static constexpr int kTitleH = 42;
    static constexpr int kRowH = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombWeaveAudioProcessorEditor)
};