#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ==============================================================
//  Colour palette
// ==============================================================
namespace FXPal {
    static const juce::Colour bg        { 0xFFCDD2DA };
    static const juce::Colour bgDark    { 0xFFB8BDC8 };
    static const juce::Colour knobTop   { 0xFFD8DCE6 };
    static const juce::Colour knobMid   { 0xFF9EA5B2 };
    static const juce::Colour knobBot   { 0xFF5E6570 };
    static const juce::Colour knobRim   { 0xFF3A3E4A };
    static const juce::Colour track     { 0xFF454A56 };
    static const juce::Colour blue      { 0xFF3AB0FF };
    static const juce::Colour blueDark  { 0xFF1A70CC };
    static const juce::Colour blueMid   { 0xFF55C4FF };
    static const juce::Colour blueBright{ 0xFFAAEAFF };
    static const juce::Colour blueGlow  { 0x443AB0FF };
    static const juce::Colour text      { 0xFF1E2230 };
    static const juce::Colour textDim   { 0xFF606A7A };
    static const juce::Colour textBright{ 0xFF8DAABB };
    static const juce::Colour titleBg   { 0xFF141820 };
    static const juce::Colour sepBg     { 0xFFC5CAD5 };

    // Envelope section accent (warm amber/green tint to distinguish from chorus blue)
    static const juce::Colour envOn     { 0xFF4DDBA2 };   // teal-green when active
    static const juce::Colour envOnGlow { 0x334DDBA2 };
    static const juce::Colour envOff    { 0xFF4A5060 };
}

// ==============================================================
//  Custom LookAndFeel
// ==============================================================
class FXAlphaLAF : public juce::LookAndFeel_V4 {
public:
    FXAlphaLAF();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
        float pos, float startA, float endA, juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int w, int h, bool down,
        int bx, int by, int bw, int bh, juce::ComboBox&) override;

    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&)       override;
    void       drawLabel(juce::Graphics&, juce::Label&) override;

    void fillTextEditorBackground(juce::Graphics&, int, int, juce::TextEditor&) override;
    void drawPopupMenuBackground(juce::Graphics&, int, int) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&,
        bool, bool, bool, bool, bool, const juce::String&,
        const juce::String&, const juce::Drawable*, const juce::Colour*) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
        const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;

    // Toggle button — drawn as a pill-shaped LED toggle
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
        bool shouldDrawHighlighted, bool shouldDrawDown) override;
};

// ==============================================================
//  LKnob  (rotary + label below)
// ==============================================================
class FXLKnob : public juce::Component {
public:
    juce::Slider slider;
    const juce::String labelText;

    explicit FXLKnob(const juce::String& lab);
    void resized() override;
    void paint(juce::Graphics& g) override;
};

// ==============================================================
//  Editor
// ==============================================================
class AlphaBetaFXAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit AlphaBetaFXAudioProcessorEditor(AlphaBetaFXAudioProcessor&);
    ~AlphaBetaFXAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AlphaBetaFXAudioProcessor& proc;
    FXAlphaLAF laf;

    // ---- Drive ----
    FXLKnob knobDrive{ "Drive" };

    // ---- Filter ----
    FXLKnob fCutoff{ "Cutoff" };
    FXLKnob fRes   { "Resonance" };
    juce::ComboBox fType;
    juce::ComboBox resType;

    // ---- Chorus ----
    FXLKnob hWet { "Wet" };
    FXLKnob hTime{ "Time" };
    FXLKnob hRate{ "Rate" };

    // ---- Filter Envelope ----
    FXLKnob envAtk{ "Attack" };
    FXLKnob envDcy{ "Decay" };
    FXLKnob envSus{ "Sustain" };
    FXLKnob envRel{ "Release" };
    FXLKnob envAmt{ "Amount" };
    FXLKnob envRtg{ "Retrigger" };  // AUTO mode: level-follower reset speed
    juce::ToggleButton envToggle;   // ENV on/off
    juce::ToggleButton midiToggle;  // AUTO / MIDI source

    // ---- Attachments ----
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SA> att_drv, att_fcut, att_fres;
    std::unique_ptr<SA> att_hwet, att_htim, att_hrat;
    std::unique_ptr<SA> att_eatk, att_edcy, att_esus, att_erel, att_eamt, att_ertg;
    std::unique_ptr<CA> att_ftyp, att_rtyp;
    std::unique_ptr<BA> att_eon, att_emid;

    // ---- Layout ----
    // Cols: Drive(95) | Sep(22) | Cutoff(95) | Res(95) | Sep(22) |
    //       Wet(95)  | Time(95) | Rate(95)   | Sep(22) |
    //       Atk(80)  | Dcy(80)  | Sus(80)    | Rel(80) | Amt(80) | Rtg(80)
    static constexpr int TITLE_H = 34;
    static constexpr int NCOLS   = 15;
    static constexpr int CW[NCOLS] = { 95, 22, 95, 95, 22, 95, 95, 95, 22, 80, 80, 80, 80, 80, 80 };
    static constexpr int NROWS   = 2;

    juce::Rectangle<int> cell(int col, int row) const;
    void placeCB(juce::ComboBox&, int col, int row);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaBetaFXAudioProcessorEditor)
};
