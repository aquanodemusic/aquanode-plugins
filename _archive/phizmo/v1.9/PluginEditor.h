#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Look & feel — magenta/purple Phizmo panel aesthetic: black domed knobs with a
// white pointer, oval buttons with an off-centre LED, tiny legend text.
//==============================================================================
class PhizmoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PhizmoLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont(juce::Label&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;

    // ---- GUI scaling -------------------------------------------------------
    // The layout is authored in a virtual 1300 x 720 space and scaled by S.
    // Every font in the plugin goes through font() so text scales with the
    // window instead of staying at a fixed pixel size and shrinking away.
    static float guiScale;                       // set once per resized()
    static constexpr float kBaseTextPx = 12.0f;  // one size for all panel text

    static juce::Font font(float designPx, bool bold = true)
    {
        auto o = juce::FontOptions().withHeight(juce::jmax(7.0f, designPx * guiScale));
        return juce::Font(bold ? o.withStyle("Bold") : o);
    }
    static juce::Font panelFont(bool bold = true) { return font(kBaseTextPx, bold); }

    static constexpr juce::uint32 col_accent1 = 0xffff2e93;   // hot magenta
    static constexpr juce::uint32 col_accent2 = 0xff8a5bff;   // violet
    static constexpr juce::uint32 col_led = 0xffff3b3b;   // red LED

private:
    const juce::Colour bg1{ 0xff2a0a3a };
    const juce::Colour bg2{ 0xff120a3a };
    const juce::Colour accent1{ col_accent1 };
    const juce::Colour accent2{ col_accent2 };
    const juce::Colour knobFace{ 0xff141018 };
    const juce::Colour knobRim{ 0xff45304f };
};

//==============================================================================
// A rotary knob + caption that can be *re-bound* to a different parameter at
// runtime. This reproduces the hardware's "select OSC 1/2 then edit" and
// "select Pitch/Filter/Amp EG then edit" workflow: the shared physical knobs
// simply re-point at the selected slot's / EG's parameters.
//==============================================================================
class PhKnob : public juce::Component
{
public:
    PhKnob(juce::AudioProcessorValueTreeState& s, juce::LookAndFeel* laf,
        const juce::String& caption, const juce::String& paramID = {},
        bool stepped = false);
    void resized() override;
    void setCaption(const juce::String& c) { label.setText(c, juce::dontSendNotification); }
    void bind(const juce::String& paramID);          // (re)create the attachment
    juce::Slider& getSlider() { return slider; }

private:
    juce::AudioProcessorValueTreeState& state;
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhKnob)
};

//==============================================================================
// Oval "domed" Phizmo button with an off-centre LED. Works as momentary or
// toggle; the LED lights when the button is on/lit.
//==============================================================================
class PhButton : public juce::TextButton
{
public:
    explicit PhButton(const juce::String& text) : juce::TextButton(text) {}
    bool ledLit = false;                 // external lit state (for select groups)
    bool hasLed = true;                  // momentary action buttons draw no LED
    void paintButton(juce::Graphics&, bool, bool) override;
};

//==============================================================================
// Four-plus character red-LED display, like the the hardware's tiny window.
//==============================================================================
class LCDDisplay : public juce::Component
{
public:
    void setText(const juce::String& t) { text = t; repaint(); }
    void paint(juce::Graphics&) override;
private:
    juce::String text{ "PHZM" };
};

//==============================================================================
// Evolution curve editor (one per oscillator) — your engine's transwave scan
// shape. Retained from the original engine GUI.
//==============================================================================
class EvoCurveEditor : public juce::Component, private juce::Timer
{
public:
    EvoCurveEditor(PhizmoAudioProcessor& p, int oscIndex, const juce::String& titleText);
    ~EvoCurveEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
private:
    void timerCallback() override { refreshEvoModeLabel(); repaint(); }
    void handleDrag(const juce::MouseEvent& e);
    int   xToPointIndex(float px) const;
    float yToVal(float py) const;
    float pointToPixelX(int idx) const;
    float pointToPixelY(float val) const;
    juce::Rectangle<float> drawArea() const;
    juce::String steppedParamID() const;
    juce::String modeParamID() const;      // per-osc EVO playhead mode param
    void refreshEvoModeLabel();            // sync the EVO button text to the param

    PhizmoAudioProcessor& proc;
    int          osc;
    juce::String title;
    int dragIndex = -1;
    // PhButton, not TextButton: this LookAndFeel deliberately draws nothing in
    // drawButtonBackground/drawButtonText, so a plain TextButton is invisible.
    PhButton btnReset{ "FLAT" };
    PhButton btnRamp{ "RAMP" };
    PhButton btnStepped{ "STEP" };
    // 4-state cycle: EVO: NORM / RAND / LOCK / FIX (see PhizmoVoice EVO modes).
    PhButton btnEvoMode{ "EVO: NORM" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvoCurveEditor)
};

//==============================================================================
// Wavetable frame display with drag & drop. Retained from the original engine.
//==============================================================================
class WavetableDisplay : public juce::Component,
    private juce::Timer,
    public juce::FileDragAndDropTarget
{
public:
    WavetableDisplay(PhizmoAudioProcessor& p, int slot, const juce::String& label);
    ~WavetableDisplay() override;
    void paint(juce::Graphics& g) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    std::function<int()> getCycleSize;
    void setSlot(int s) { slotIndex = s; repaint(); }
    int  getSlot() const { return slotIndex; }
private:
    void timerCallback() override { repaint(); }
    PhizmoAudioProcessor& proc;
    int          slotIndex;
    juce::String slotLabel;
    bool         dragOver = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableDisplay)
};

//==============================================================================
// A titled section panel that draws the screened divider + heading, like the
// hardware's screened vertical section lines.
//==============================================================================
struct SectionPanel : public juce::Component
{
    juce::String title;
    explicit SectionPanel(const juce::String& t) : title(t) {}
    void paint(juce::Graphics&) override;
};

//==============================================================================
class PhizmoAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer,
    public juce::FileDragAndDropTarget
{
public:
    PhizmoAudioProcessorEditor(PhizmoAudioProcessor&);
    ~PhizmoAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;

    // Hardware workflow state
    void selectSound(int snd);               // 0..3 focus a Sound (Phizmo Preset layer)
    void selectOsc(int osc);                 // 0 = OSC1, 1 = OSC2
    void selectEG(int eg);                   // 0 = Pitch, 1 = Filter, 2 = Amp
    void refreshLeds();
    void updateOctaveButton();               // refresh OCTAVE button label per osc

    void refreshSoundButtons();

    // Preset / wavetable actions
    void loadFactoryWavetable(int slot);
    void loadFactoryWavetableFromChooser(int slot);

    // --- 2.0: wave generation ---------------------------------------------
    // Appended to the existing slot popup rather than given new panel real
    // estate, so the 1.x layout is untouched.
    void addGenerateMenuItems(juce::PopupMenu& menu, int slot);
    bool handleGenerateMenuResult(int result, int slot);
    void showGenerateStatus(const juce::String& text);
    juce::String lastGenerateStatus;
    void exportSlotAsWavClicked(int slot);
    void loadPresetFromFile(const juce::File& src);
    void savePresetClicked();
    void loadPresetClicked();
    void tapTempo();

    std::unique_ptr<PhKnob> makeKnob(const juce::String& cap, const juce::String& id, bool stepped = false);
    std::unique_ptr<PhButton> makeButton(const juce::String& txt, bool toggle = false);
    std::unique_ptr<PhButton> makeActionButton(const juce::String& txt);

    PhizmoAudioProcessor& audioProcessor;
    PhizmoLookAndFeel laf;

    int currentOsc = 0;      // 0=A,1=B
    int currentEG = 2;      // default Amp

    LCDDisplay lcd;

    // Product wordmark, drawn where the old text title used to sit. Sourced from
    // the Phizmo_Logo.png binary resource added to the .jucer.
    juce::Image logoImg;

    // ---- Section panels (drawn dividers + titles) ----
    // Section titles carry a scope prefix so it is always clear what a control
    // affects: OSC = the focused oscillator, VOICE = the focused Voice (both
    // oscillators), GLOBAL = the whole instrument.
    SectionPanel secRev{ "GLOBAL REVERB" };
    SectionPanel secVolume{ "GLOBAL" }, secPreset{ "PRESET" },
        secSound{ "EDIT / ON  VOICE" },
        secNoise{ "VOICE NOISE" }, secKbd{ "GLOBAL KEYBOARD" },
        secOsc{ "EDIT / ON  OSC" }, secPitch{ "OSC PITCH" }, secGlide{ "GLIDE" },
        secEnv{ "OSC ENVELOPE" }, secAmp{ "OSC AMPLITUDE" },
        secTempo{ "TEMPO" }, secArp{ "GLOBAL ARPEGGIATOR" }, secFx{ "GLOBAL EFFECTS" },
        secWave{ "OSC WAVE" }, secFilter{ "VOICE FILTER" }, secMod{ "OSC MODULATION (LFO)" },
        secTwEnv{ "TRANSWAVE ENV" },
        secRT{ "REALTIME  CONTROL" }, secEvo{ "TRANSWAVES" };

    // ---- Volume ----
    std::unique_ptr<PhKnob> kVolume;

    // ---- Preset (just LOAD / SAVE now) ----
    std::unique_ptr<PhButton> bLoad, bSave;

    // ---- Sound ----
    // Four exclusive EDIT buttons choose which Sound the panel edits; four
    // independent ON buttons switch each Sound in and out of the Preset.
    // Four Voices; each Voice is a pair of oscillators.
    std::unique_ptr<PhButton> bSoundEdit[4], bSoundOn[4];
    // Oscillator focus and oscillator enable are separate controls, the same
    // way the Voice rows are — no press-twice-to-mean-something-else.
    std::unique_ptr<PhButton> bOscOn1, bOscOn2;
    std::unique_ptr<PhKnob> kSoundLow, kSoundHigh, kSoundLevel;

    // ---- Keyboard (velocity response + pitch bend range) ----
    std::unique_ptr<PhKnob> kVelCurve, kBendRange;

    // ---- OSC select ----
    std::unique_ptr<PhButton> bOsc1, bOsc2;

    // ---- Pitch (retargeted per OSC) ----
    std::unique_ptr<PhKnob> kTune, kFine, kPitchMod, kPitchAmt;
    std::unique_ptr<PhButton> bPitchModSel, bWaveModSel, bFiltModSel;

    // ---- Glide ----
    std::unique_ptr<PhButton> bMono;
    std::unique_ptr<PhKnob> kGlide;

    // ---- Envelope (retargeted per EG) ----
    std::unique_ptr<PhButton> bEgSelect, bEgMode;
    std::unique_ptr<PhKnob> kAttack, kDecay, kSustain, kRelease, kEnvVel;

    // ---- Amplitude (retargeted per OSC) ----
    std::unique_ptr<PhKnob> kLevel, kPan;

    // ---- Wave (retargeted per OSC) ----
    std::unique_ptr<PhKnob> kWaveMod, kWaveAmt;

    // ---- Filter ----
    std::unique_ptr<PhButton> bFilterType;
    std::unique_ptr<PhKnob> kCutoff, kReso, kFiltMod, kFiltAmt, kFiltKbd;

    // ---- Modulation (LFO / Noise) ----
    std::unique_ptr<PhButton> bModSelect;
    std::unique_ptr<PhKnob> kLfoShape, kLfoSpeed, kLfoSync;
    std::unique_ptr<PhKnob> kNoiseRate, kNoiseSync;
    std::unique_ptr<PhKnob> kNoise;          // audio-rate noise level
    std::unique_ptr<PhKnob> kNoiseFreq, kNoiseQ, kNoiseType;   // its own filter
    std::unique_ptr<PhKnob> kEvoLFORate, kEvoLFODepth;         // evolution LFO

    // ---- Effects ----
    std::unique_ptr<PhButton> bFxSelect, bFxBusSelect;
    std::unique_ptr<PhKnob> kFxAlgo, kFxVariation, kFxMix;
    std::unique_ptr<PhKnob> kRevVariation, kRevAmount;

    // ---- Arpeggiator ----
    std::unique_ptr<PhButton> bArpOn, bArpLatch;
    std::unique_ptr<PhKnob> kArpRange, kArpMode, kArpRate;

    // ---- Tempo ----
    std::unique_ptr<PhButton> bTap;
    std::unique_ptr<PhKnob> kTempo;

    // ---- Realtime F I Z M O ----
    std::unique_ptr<PhKnob> kF, kI, kZ, kM, kO;
    std::unique_ptr<PhKnob> kODest;

    // ---- Transwave / Evolution power strip (engine extras) ----
    std::unique_ptr<EvoCurveEditor> evoA, evoB;
    std::unique_ptr<WavetableDisplay> wtA, wtB;
    std::unique_ptr<PhButton> bLoadA, bLoadB;
    std::unique_ptr<PhKnob> kEvoTime, kScanStyle, kOscMix, kSpread;
    // Per-pad scan phase-offset (OFS) and fade-time (SLW) knobs.
    std::unique_ptr<PhKnob> kEvoOffA, kEvoOffB, kEvoSlewA, kEvoSlewB;
    // Advanced / relocated engine knobs
    std::unique_ptr<PhKnob> kFiltEnvAmt, kDetune, kKeytrack;
    std::unique_ptr<PhKnob> kTwAmt, kTwAtt, kTwDec, kTwToFilt, kTwVel;
    std::unique_ptr<PhKnob> kFrameSnap, kGrit, kJumpProb, kRingMod, kStWidth;
    std::unique_ptr<PhKnob> kUniDet, kPosLFORate, kPosLFODep;
    // OCTAVE (cyclic, per-osc) and VEL>FR (on/off) are buttons, not knobs.
    std::unique_ptr<PhButton> bOctave, bVelFrame;

    // Attachments for buttons that map directly to a toggle/choice param
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;

    std::unique_ptr<juce::FileChooser> fileChooser;
    double lastTapTime = 0.0;

    bool editorReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhizmoAudioProcessorEditor)
};