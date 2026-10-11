#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CrtMatrix.h"

// ==============================================================
//  Colour palette  – watery shiny blue + light grey
// ==============================================================
namespace Pal {
    static const juce::Colour bg{ 0xFFCDD2DA };
    static const juce::Colour bgDark{ 0xFFB8BDC8 };
    static const juce::Colour cell{ 0xFFB0B5C0 };
    static const juce::Colour knobTop{ 0xFFD8DCE6 };
    static const juce::Colour knobMid{ 0xFF9EA5B2 };
    static const juce::Colour knobBot{ 0xFF5E6570 };
    static const juce::Colour knobRim{ 0xFF3A3E4A };
    static const juce::Colour track{ 0xFF454A56 };
    // Blues – watery, luminous
    static const juce::Colour blue{ 0xFF3AB0FF };
    static const juce::Colour blueDark{ 0xFF1A70CC };
    static const juce::Colour blueMid{ 0xFF55C4FF };
    static const juce::Colour blueBright{ 0xFFAAEAFF };
    static const juce::Colour blueGlow{ 0x443AB0FF };
    // Text
    static const juce::Colour text{ 0xFF1E2230 };
    static const juce::Colour textDim{ 0xFF606A7A };
    static const juce::Colour textBright{ 0xFF8DAABB };
    // Title bar
    static const juce::Colour titleBg{ 0xFF141820 };
    static const juce::Colour titleLine{ 0xFF1A78CC };
    // Separator / section dividers
    static const juce::Colour sepBg{ 0xFFC5CAD5 };
}

// ==============================================================
//  Custom LookAndFeel
// ==============================================================
class AlphaLAF : public juce::LookAndFeel_V4 {
public:
    AlphaLAF();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
        float pos, float startA, float endA, juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int w, int h, bool down,
        int bx, int by, int bw, int bh, juce::ComboBox&) override;

    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont(juce::ComboBox&)   override;
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
};

// ==============================================================
//  LKnob – rotary slider + small label below
// ==============================================================
class LKnob : public juce::Component {
public:
    juce::Slider slider;
    const juce::String labelText;

    explicit LKnob(const juce::String& lab);
    void resized() override;
    void paint(juce::Graphics& g) override;
};

// ==============================================================
//  WtSlot – one per wave slot (A / B of each oscillator):
//  a clickable name strip (left-click loads a wavetable, right-click
//  offers frame sizes / clear) and a horizontal position slider.
// ==============================================================
class WtSlot : public juce::Component, private juce::Timer {
public:
    juce::Slider slider;
    std::function<juce::String()> getName;
    std::function<void(int)> onLoad;   // frame size, 0 = auto
    std::function<void()> onClear;

    explicit WtSlot(const juce::String& title);
    ~WtSlot() override { stopTimer(); }
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void refresh() { timerCallback(); }

private:
    juce::String title, shownName;
    juce::Rectangle<int> strip, titleArea;
    void timerCallback() override;
};

// ==============================================================
//  Editor
// ==============================================================
class AlphaBetaAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit AlphaBetaAudioProcessorEditor(AlphaBetaAudioProcessor&);
    ~AlphaBetaAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AlphaBetaAudioProcessor& proc;
    AlphaLAF laf;

    // ---- OSC 1 ----
    juce::ComboBox o1WaveA, o1OctA, o1WaveB, o1OctB;
    LKnob o1Morph{ "O1 Morph" };
    LKnob o1Detune{ "O1 Detune" };

    // ---- OSC 2 ----
    juce::ComboBox o2WaveA, o2OctA, o2WaveB, o2OctB;
    LKnob o2Morph{ "O2 Morph" };
    LKnob o2Detune{ "O2 Detune" };

    // ---- Mix column ----
    LKnob knobMix{ "OSC Mix" };
    LKnob knobDrive{ "Drive" };
    LKnob knobFM{ "FM" };
    LKnob knobSpread{ "Width" };      // stereo width (param id "spread")

    // ---- Filter ----
    LKnob fCutoff{ "Cutoff" };
    LKnob fRes{ "Resonance" };
    juce::ComboBox fType;
    juce::ComboBox resType;    // resonance curve selector
    LKnob fAtt{ "Flt Attack" };
    LKnob fDec{ "Flt Decay" };
    LKnob fSus{ "Flt Sustain" };
    LKnob fRel{ "Flt Release" };
    LKnob fFade{ "Flt Fade" };
    LKnob fDepth{ "Flt Depth" };

    // ---- Amp ----
    LKnob aVol{ "Volume" };
    LKnob aVel{ "Velocity" };
    LKnob aAtt{ "Amp Attack" };
    LKnob aDec{ "Amp Decay" };
    LKnob aSus{ "Amp Sustain" };
    LKnob aRel{ "Amp Release" };
    LKnob aFade{ "Amp Fade" };

    // ---- Chorus + Glide ----
    LKnob hWet{ "Chorus Wet" };
    LKnob hTime{ "Chorus Time" };
    LKnob hRate{ "Chorus Rate" };
    LKnob knobGlide{ "Glide" };

    // ---- OSC pitch ----
    LKnob o1Pitch{ "O1 Pitch" };
    LKnob o2Pitch{ "O2 Pitch" };

    // ---- Alpha 3 extras ----
    LKnob knobNoise{ "Noise" };
    LKnob knobRing{ "Ringmod" };
    LKnob knobFFM{ "Filter FM" };
    juce::ComboBox ffmSrc;
    LKnob knobBend{ "Bend Range" };
    LKnob knobUnison{ "Unison" };     // Alpha 3 "spread": 3 detuned voices
    LKnob knobAnalog{ "Analog" };     // Alpha 3 "analogness"
    juce::ComboBox voiceMode;         // Poly / Mono / Legato (title bar)
    juce::TooltipWindow tooltips{ this, 700 };
    // Wavetable slots: [0] OSC1 A, [1] OSC1 B, [2] OSC2 A, [3] OSC2 B
    std::array<WtSlot, 4> wtSlots{ WtSlot("A"), WtSlot("B"), WtSlot("A"), WtSlot("B") };
    LKnob knobTune{ "Tune" };

    // ---- Modulation matrix (CRT) + LFOs ----
    CrtMatrix crt;
    struct LfoUi {
        juce::ComboBox wave, sync, mode;
        LKnob rate{ "Rate" };
        LKnob att{ "Attack" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aWave, aSync, aMode;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aRate, aAtt;
    };
    std::array<LfoUi, Mod::NUM_LFOS> lfoUi;
    std::array<juce::Rectangle<int>, Mod::NUM_LFOS> lfoBoxes;   // for paint()

    juce::TextButton importBtn{ "IMPORT" };

    // ---- Randomizer button ----
    juce::TextButton randomBtn;
    juce::ComboBox randomType;        // character for RANDOM (Bass, Pad, Pluck, ...)

    // ---- Save / Load state buttons ----
    juce::TextButton saveBtn{ "SAVE" };
    juce::TextButton loadBtn{ "LOAD" };

    // ---- Attachments ----
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SA>
        att_o1m, att_o1d, att_o2m, att_o2d,
        att_mix, att_drv, att_fm, att_spr,
        att_fcut, att_fres,
        att_fatt, att_fdec, att_fsus, att_frel, att_ffad, att_fdep,
        att_avol, att_avel, att_aatt, att_adec, att_asus, att_arel, att_afad,
        att_hwet, att_htim, att_hrat, att_glide,
        att_o1pit, att_o2pit,
        att_noise, att_ring, att_ffm, att_bend,
        att_uni, att_analog, att_tune;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> att_wtp;
    juce::Rectangle<int> wtRowBounds(int osc, int ab) const;

    std::unique_ptr<CA>
        att_o1wa, att_o1oa, att_o1wb, att_o1ob,
        att_o2wa, att_o2oa, att_o2wb, att_o2ob,
        att_ftyp, att_rtyp, att_ffms, att_vmode;

    // ---- Layout constants ----
    static constexpr int TITLE_H = 34;
    static constexpr int ROWS = 6;
    static constexpr int COLS = 10;
    // 8 content cols (95px) + 2 separator cols (22px)
    static constexpr int CW[COLS] = { 95,95,95,95, 22, 95,95, 22, 95,95 };
    static constexpr int GRID_W = 804;   // sum of CW
    static constexpr int PANEL_W = 336;  // matrix + LFO panel on the right

    juce::Rectangle<int> cell(int col, int row) const;
    juce::Rectangle<int> knobArea(int col, int row) const;

    juce::Rectangle<int> oscComboBounds(int col, int row) const;
    void placeOscCB(juce::ComboBox& cb, int col, int row);
    void placeCB(juce::ComboBox&, int col, int row);
    void syncAllUIFromAPVTS();

    // NOTE: initWaveCombo / initOctCombo removed – population done inline in ctor
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaBetaAudioProcessorEditor)
};