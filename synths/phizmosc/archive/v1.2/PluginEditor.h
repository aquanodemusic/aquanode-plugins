#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class PhizmOscLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PhizmOscLookAndFeel();
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
class PhizmOscKnob : public juce::Component
{
public:
    PhizmOscKnob(const juce::String& label,
        juce::AudioProcessorValueTreeState& apvts,
        const juce::String& paramID,
        PhizmOscLookAndFeel& laf);
    ~PhizmOscKnob() override = default;
    void resized() override;

private:
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhizmOscKnob)
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
class WavetableDisplay : public juce::Component,
                         private juce::Timer,
                         public juce::FileDragAndDropTarget
{
public:
    WavetableDisplay(TranswaveAudioProcessor& p, int slot, const juce::String& label);
    ~WavetableDisplay() override;
    void paint(juce::Graphics& g) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Cycle-size source so the display can read the editor's text field
    std::function<int()> getCycleSize;

private:
    void timerCallback() override { repaint(); }
    TranswaveAudioProcessor& proc;
    int          slotIndex;
    juce::String slotLabel;
    bool         dragOver = false;   // highlight while a valid file hovers
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableDisplay)
};

//==============================================================================
class TranswaveAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer,
    public juce::FileDragAndDropTarget
{
public:
    TranswaveAudioProcessorEditor(TranswaveAudioProcessor&);
    ~TranswaveAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override {}
    void loadWavetableClicked(int slot);
    void savePresetClicked();
    void loadPresetClicked();
    void setSampleFolderClicked();
    void loadPresetFromFile(const juce::File& src);
    void refreshUIAfterPresetLoad();

    TranswaveAudioProcessor& audioProcessor;
    PhizmOscLookAndFeel laf;

    // ROW 1: WT | EVOLUTION | PITCH
    PhizmOscKnob knobGain;

    PhizmOscKnob knobEvoTime, knobEvoLFORate, knobEvoLFODepth;
    PhizmOscKnob knobPosLFORate, knobPosLFODepth;

    PhizmOscKnob knobDetune, knobPitchLFO, knobPitchLFORate;
    PhizmOscKnob knobPitchEnvAmt, knobPitchEnvAtt, knobPitchEnvDec;

    // ROW 2 (new): TRANSWAVE ENVELOPE — right 6 slots, left 6 empty
    PhizmOscKnob knobTwAtt, knobTwDec, knobTwSus, knobTwRel, knobTwAmt, knobTwVelAmt;
    juce::Label  sectionTWEnv;

    // ROW 2: MISC — left 6 slots (cols 1-6)
    PhizmOscKnob knobFrameSnap, knobTwToFilter, knobEvoBPhaseOff,
                 knobKeytrack, knobEvoTimeB, knobEvoRestart;
    juce::Label  sectionMisc;

    // ROW 3: ENVELOPE | CHARACTER | SCAN | FILTER
    PhizmOscKnob knobAttack, knobDecay, knobSustain, knobRelease;
    PhizmOscKnob knobBitCrush, knobGrit;
    PhizmOscKnob knobScanStyle, knobScanJump;
    // Filter: base + env
    PhizmOscKnob knobFilterFreq, knobFilterRes;
    PhizmOscKnob knobFilterAtt, knobFilterDec, knobFilterSus, knobFilterRel;
    PhizmOscKnob knobFilterEnvAmt, knobFilterLFODep;

    // ROW 3: OSC2 ADSR | OCTAVES | GLIDE/MONO | FILTER ENV
    PhizmOscKnob knobAttack2, knobDecay2, knobSustain2, knobRelease2;
    PhizmOscKnob knobOctaveA, knobOctaveB;
    PhizmOscKnob knobGlide, knobMono;

    // ROW 4: STEREO | FX
    PhizmOscKnob knobSpread, knobStereoWidth, knobUniDetune, knobStereoPhase;
    PhizmOscKnob knobOscMix;
    PhizmOscKnob knobChorusRate, knobChorusDepth;
    PhizmOscKnob knobRingMod;
    PhizmOscKnob knobReverbSize, knobReverbDamp, knobReverbWet;
    PhizmOscKnob knobNoise;

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
    juce::TextButton toggleSampleFolder  { "SET DIR" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Guard: true only after the constructor has finished. Prevents the
    // initial setSize() call from overwriting the APVTS-restored width.
    bool editorReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranswaveAudioProcessorEditor)
};