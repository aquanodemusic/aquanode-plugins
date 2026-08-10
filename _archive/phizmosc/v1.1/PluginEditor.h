#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class PhismOscLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PhismOscLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont(juce::Label&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;

    static constexpr juce::uint32 col_accent1 = 0xffff6ec7;
    static constexpr juce::uint32 col_accent2 = 0xff6ec6ff;

private:
    const juce::Colour bg1{ 0xff1a0a2e };
    const juce::Colour bg2{ 0xff0d1f4f };
    const juce::Colour accent1{ col_accent1 };
    const juce::Colour accent2{ col_accent2 };
    const juce::Colour knobFace{ 0xff2a1a4a };
    const juce::Colour knobRim{ 0xff9940c8 };
};

//==============================================================================
class PhismOscKnob : public juce::Component
{
public:
    PhismOscKnob(const juce::String& label,
        juce::AudioProcessorValueTreeState& apvts,
        const juce::String& paramID,
        PhismOscLookAndFeel& laf);
    ~PhismOscKnob() override = default;
    void resized() override;

private:
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhismOscKnob)
};

//==============================================================================
// Evolution curve editor for a single oscillator (osc = 0 for A, 1 for B).
// Owns its own FLAT / RAMP / STEP buttons and lays them out (and its title)
// in their own rows so nothing overlaps.
class EvoCurveEditor : public juce::Component, private juce::Timer
{
public:
    EvoCurveEditor(TranswaveAudioProcessor& p, int oscIndex, const juce::String& titleText);
    ~EvoCurveEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override { repaint(); }
    void handleDrag(const juce::MouseEvent& e);
    int   xToPointIndex(float px) const;
    float yToVal(float py) const;
    float pointToPixelX(int idx) const;
    float pointToPixelY(float val) const;
    juce::Rectangle<float> drawArea() const;
    juce::String steppedParamID() const;

    TranswaveAudioProcessor& proc;
    int          osc;     // 0 = Osc A, 1 = Osc B
    juce::String title;
    int dragIndex = -1;

    juce::TextButton btnReset{ "FLAT" };
    juce::TextButton btnRamp{ "RAMP" };
    juce::TextButton btnStepped{ "STEP" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvoCurveEditor)
};

//==============================================================================
class WavetableDisplay : public juce::Component, private juce::Timer
{
public:
    WavetableDisplay(TranswaveAudioProcessor& p, int slot, const juce::String& label);
    ~WavetableDisplay() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }
    TranswaveAudioProcessor& proc;
    int          slotIndex;
    juce::String slotLabel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableDisplay)
};

//==============================================================================
class TranswaveAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    TranswaveAudioProcessorEditor(TranswaveAudioProcessor&);
    ~TranswaveAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override {}
    void loadWavetableClicked(int slot);
    void savePresetClicked();
    void loadPresetClicked();

    TranswaveAudioProcessor& audioProcessor;
    PhismOscLookAndFeel laf;

    // ROW 1: WT | EVOLUTION | PITCH
    PhismOscKnob knobGain;

    PhismOscKnob knobEvoTime, knobEvoLFORate, knobEvoLFODepth;
    PhismOscKnob knobPosLFORate, knobPosLFODepth;

    PhismOscKnob knobDetune, knobPitchLFO, knobPitchLFORate;
    PhismOscKnob knobPitchEnvAmt, knobPitchEnvAtt, knobPitchEnvDec;

    // ROW 2: ENVELOPE | CHARACTER | SCAN | FILTER
    PhismOscKnob knobAttack, knobDecay, knobSustain, knobRelease;
    PhismOscKnob knobBitCrush, knobGrit;
    PhismOscKnob knobScanStyle, knobScanJump;
    // Filter: base + env
    PhismOscKnob knobFilterFreq, knobFilterRes;
    PhismOscKnob knobFilterAtt, knobFilterDec, knobFilterSus, knobFilterRel;
    PhismOscKnob knobFilterEnvAmt, knobFilterLFODep;

    // ROW 3: OSC2 ADSR | OCTAVES | GLIDE/MONO | FILTER ENV
    PhismOscKnob knobAttack2, knobDecay2, knobSustain2, knobRelease2;
    PhismOscKnob knobOctaveA, knobOctaveB;
    PhismOscKnob knobGlide, knobMono;

    // ROW 4: STEREO | FX
    PhismOscKnob knobSpread, knobStereoWidth, knobUniDetune, knobStereoPhase;
    PhismOscKnob knobOscMix;
    PhismOscKnob knobChorusRate, knobChorusDepth;
    PhismOscKnob knobRingMod;
    PhismOscKnob knobReverbSize, knobReverbDamp, knobReverbWet;
    PhismOscKnob knobNoise;

    // CURVE EDITORS — one per oscillator, stacked in the right-hand column
    EvoCurveEditor evoCurveEditorA, evoCurveEditorB;

    // Section labels
    juce::Label sectionWT, sectionEvo, sectionPitch;
    juce::Label sectionADSR, sectionGrit, sectionScan, sectionFilter;
    juce::Label sectionOsc2, sectionOctave, sectionGlide, sectionFilterEnv;
    juce::Label sectionStereo, sectionFX;

    // Wavetable displays
    WavetableDisplay wtDisplayA, wtDisplayB;

    // Slot A controls
    juce::TextButton loadButtonA;
    juce::Label      filenameLabelA, cycleSizeLabelA;
    juce::TextEditor cycleSizeEditorA;

    // Slot B controls
    juce::TextButton loadButtonB;
    juce::Label      filenameLabelB, cycleSizeLabelB;
    juce::TextEditor cycleSizeEditorB;

    // Preset controls
    juce::TextButton savePresetButton, loadPresetButton;
    juce::Label      presetNameLabel, infoLabel;

    // Engine mode toggles (title bar)
    juce::TextButton toggleFrameInterp   { "INTERP" };
    juce::TextButton toggleFilterPerVoice{ "PV FILT" };
    juce::TextButton toggleVelToFrame    { "VEL>FRM" };
    juce::TextButton toggleEvoPhaseCarry { "EVO CARRY" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranswaveAudioProcessorEditor)
};
