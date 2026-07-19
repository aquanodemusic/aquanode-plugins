#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class PhismOscLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PhismOscLookAndFeel();
    void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
    void drawLabel(juce::Graphics&,juce::Label&) override;
    juce::Font getLabelFont(juce::Label&) override;
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
    void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;

    // Exposed colours for use by EvoCurveEditor
    static constexpr juce::uint32 col_accent1 = 0xffff6ec7;
    static constexpr juce::uint32 col_accent2 = 0xff6ec6ff;

private:
    const juce::Colour bg1     {0xff1a0a2e};
    const juce::Colour bg2     {0xff0d1f4f};
    const juce::Colour accent1 {col_accent1};
    const juce::Colour accent2 {col_accent2};
    const juce::Colour knobFace{0xff2a1a4a};
    const juce::Colour knobRim {0xff9940c8};
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
// Drawable evolution curve editor — 32 draggable points, real-time playhead
class EvoCurveEditor : public juce::Component, private juce::Timer
{
public:
    EvoCurveEditor(TranswaveAudioProcessor& p);
    ~EvoCurveEditor() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Buttons wired up by the editor
    juce::TextButton btnReset   { "FLAT" };
    juce::TextButton btnRamp    { "RAMP" };
    juce::TextButton btnStepped { "STEP" };

private:
    void timerCallback() override { repaint(); }
    void handleDrag(const juce::MouseEvent& e);

    // Map pixel x -> curve point index (nearest)
    int xToPointIndex(float px) const;
    // Map pixel y -> normalised curve value
    float yToVal(float py) const;
    // Map point index/val -> pixel coords
    float pointToPixelX(int idx) const;
    float pointToPixelY(float val) const;

    juce::Rectangle<float> drawArea() const;

    TranswaveAudioProcessor& proc;
    int  dragIndex = -1;

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
    void updateABToggleLabel();
    void savePresetClicked();
    void loadPresetClicked();

    TranswaveAudioProcessor& audioProcessor;
    PhismOscLookAndFeel laf;

    // ROW 1: WT | EVOLUTION | PITCH
    PhismOscKnob knobGain;          // WT panel (1 knob)

    PhismOscKnob knobEvoTime;       // EVO panel (5 knobs)
    PhismOscKnob knobEvoLFORate;
    PhismOscKnob knobEvoLFODepth;
    PhismOscKnob knobPosLFORate;
    PhismOscKnob knobPosLFODepth;

    PhismOscKnob knobDetune;        // PITCH panel (3 knobs)
    PhismOscKnob knobPitchLFO;
    PhismOscKnob knobPitchLFORate;

    // ROW 2: ENVELOPE | CHARACTER | SCAN | FILTER
    PhismOscKnob knobAttack, knobDecay, knobSustain, knobRelease;
    PhismOscKnob knobBitCrush, knobGrit;
    PhismOscKnob knobScanStyle, knobScanJump;
    juce::TextButton btnStepped;    // STEPPED toggle inside SCAN panel
    PhismOscKnob knobFilterFreq, knobFilterRes;

    // ROW 3: STEREO | FX
    PhismOscKnob knobSpread, knobStereoWidth, knobUniDetune, knobStereoPhase;
    PhismOscKnob knobChorusRate, knobChorusDepth, knobReverbSize, knobReverbDamp, knobReverbWet;

    // CURVE EDITOR (spans rows 1+2+3)
    EvoCurveEditor evoCurveEditor;

    // Section labels
    juce::Label sectionWT, sectionEvo, sectionPitch;
    juce::Label sectionADSR, sectionGrit, sectionScan, sectionFilter;
    juce::Label sectionStereo, sectionFX;
    juce::Label sectionCurve;

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

    // A/B toggle + preset controls
    juce::TextButton abToggleButton;
    juce::TextButton savePresetButton, loadPresetButton;
    juce::Label      presetNameLabel, infoLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranswaveAudioProcessorEditor)
};
