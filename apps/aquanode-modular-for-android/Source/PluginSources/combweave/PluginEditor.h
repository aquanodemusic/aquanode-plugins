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

    // ── Attachment typedefs ──────────────────────────────────────────────
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // ════════════════════════════════════════════════════════════════════
    //  OSC 1
    // ════════════════════════════════════════════════════════════════════
    juce::Label osc1Label;

    juce::Slider     amountSlider, hpFreqSlider, spreadSlider, volumeSlider;
    juce::Label      amountLabel, hpFreqLabel, spreadLabel, volumeLabel;
    juce::Slider     attackSlider, releaseSlider, rolloffSlider, tuneSlider;
    juce::Label      attackLabel, releaseLabel, rolloffLabel, tuneLabel;

    juce::TextButton bidirBtn{ "Onedirectional" };
    juce::TextButton noteLockBtn{ "Note Lock" };

    juce::ComboBox spreadModeBox;      juce::Label spreadModeLabel;
    juce::ComboBox freqModeBox;        juce::Label freqModeLabel;
    juce::ComboBox harmonicFilterBox;  juce::Label harmonicFilterLabel;

    std::unique_ptr<SliderAtt> amountAtt, hpFreqAtt;
    std::unique_ptr<SliderAtt> spreadHzAtt, spreadRatioAtt;
    std::unique_ptr<SliderAtt> volumeAtt, attackAtt, releaseAtt, rolloffAtt, tuneAtt;
    std::unique_ptr<ButtonAtt> bidirAtt, noteLockAtt;
    std::unique_ptr<ComboAtt>  spreadModeAtt, freqModeAtt, harmonicFilterAtt;

    int  lastSpreadMode = -1;
    bool lastBidirState = false;

    // ════════════════════════════════════════════════════════════════════
    //  OSC 2
    // ════════════════════════════════════════════════════════════════════
    juce::Label osc2Label;

    juce::Slider     amountSlider2, hpFreqSlider2, spreadSlider2, volumeSlider2;
    juce::Label      amountLabel2, hpFreqLabel2, spreadLabel2, volumeLabel2;
    juce::Slider     attackSlider2, releaseSlider2, rolloffSlider2, tuneSlider2;
    juce::Label      attackLabel2, releaseLabel2, rolloffLabel2, tuneLabel2;

    juce::TextButton bidirBtn2{ "Onedirectional" };
    juce::TextButton noteLockBtn2{ "Note Lock" };

    juce::ComboBox spreadModeBox2;      juce::Label spreadModeLabel2;
    juce::ComboBox freqModeBox2;        juce::Label freqModeLabel2;
    juce::ComboBox harmonicFilterBox2;  juce::Label harmonicFilterLabel2;

    std::unique_ptr<SliderAtt> amountAtt2, hpFreqAtt2;
    std::unique_ptr<SliderAtt> spreadHzAtt2, spreadRatioAtt2;
    std::unique_ptr<SliderAtt> volumeAtt2, attackAtt2, releaseAtt2, rolloffAtt2, tuneAtt2;
    std::unique_ptr<ButtonAtt> bidirAtt2, noteLockAtt2;
    std::unique_ptr<ComboAtt>  spreadModeAtt2, freqModeAtt2, harmonicFilterAtt2;

    int  lastSpreadMode2 = -1;
    bool lastBidirState2 = false;

    // ════════════════════════════════════════════════════════════════════
    //  Title
    // ════════════════════════════════════════════════════════════════════
    juce::Label titleLabel;
    juce::Label subtitleLabel;

    // ── Layout constants ─────────────────────────────────────────────────
    static constexpr int kPad = 15;
    static constexpr int kDispH = 185;
    static constexpr int kTitleH = 42;
    static constexpr int kRowH = 100;
    static constexpr int kOscLabelH = 20;

    // ── Helpers ──────────────────────────────────────────────────────────
    void setupKnob(juce::Slider&, juce::Label&, const juce::String& name);
    void setupBtn(juce::TextButton&, bool toggleable = true);
    void setupCombo(juce::ComboBox&, juce::Label&, const juce::String& name);

    void drawDisplay(juce::Graphics&, juce::Rectangle<int> area);

    void syncSpreadAttachment(int mode);   // osc1
    void syncSpreadAttachment2(int mode);   // osc2

    // ── Layout helper ────────────────────────────────────────────────────
    // Positions one oscillator's controls within the given area.
    // sfx: "" for osc1, "2" for osc2 (used to select correct widgets).
    void layoutOscSection(juce::Rectangle<int> area, int oscIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombWeaveAudioProcessorEditor)
};