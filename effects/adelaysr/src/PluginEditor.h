#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
 *  AeroLookAndFeel
 *  ───────────────
 *  Windows-Vista-era "Aero glass" aesthetic applied to JUCE controls:
 *  • Rotary knobs: layered radial gradients, gloss bubble, value-arc glow,
 *    metallic rim bevel, centre-indicator dot with light trail.
 *  • Toggle buttons: gradient face, embossed border, top-edge shine.
 *  • ComboBoxes: dark glass body, gradient arrow.
 *  All accent colours are read from each component's colour map so that
 *  individual controls can be tinted differently via setColour().
 */
class AeroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AeroLookAndFeel();

    //── Rotary knob ──────────────────────────────────────────────────────────
    void drawRotarySlider (juce::Graphics&,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    //── Label (value textbox under knob) ─────────────────────────────────────
    juce::Font getLabelFont (juce::Label&) override;

    //── Toggle / push button ─────────────────────────────────────────────────
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                                const juce::Colour& bgColour,
                                bool isHighlighted, bool isDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool isHighlighted, bool isDown) override;

    //── ComboBox ──────────────────────────────────────────────────────────────
    void drawComboBox (juce::Graphics&, int width, int height,
                       bool isDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    //── Popup menu (ComboBox dropdown) ────────────────────────────────────────
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    void drawPopupMenuItem (juce::Graphics&,
                            const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked,
                            bool hasSubMenu,
                            const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

    //── Theme ─────────────────────────────────────────────────────────────────
    void setLightMode (bool light);   // called by ADelaySREditor::applyTheme()
    bool getLightMode() const noexcept { return lightMode; }

private:
    bool lightMode { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeroLookAndFeel)
};


//==============================================================================
/**
 *  ADelaySREditor  –  v3
 *
 *  750 × 210 px  –  all controls in a single horizontal row + bottom strip.
 *
 *  Layout  (5 equal slots × 150 px):
 *  ┌─────────────────┬──────────┬──────────┬──────────┬─────────────────┐
 *  │  [SYNC] [NOTE]  │          │          │          │ [TRIG]          │
 *  │  Timing widget  │ ATTACK   │ RELEASE  │ TAP VOL  │ [MODE combo]    │
 *  │                 │          │          │          │ [WET ONLY]      │
 *  ├─────────────────┴──────────┴──────────┴──────────┤         [● REC]│
 *  └────────────────────────────────────────────────────────────────────┘
 *     slot 1            slot 2    slot 3    slot 4       slot 5
 *
 *  The "timing widget" in slot 1 shows ONE of:
 *    delayTimeSlider    (!noteDelay && !synced)
 *    noteDelaySlider    ( noteDelay)
 *    syncDivCombo       (!noteDelay &&  synced)
 */
class ADelaySREditor final
    : public  juce::AudioProcessorEditor,
      private juce::AudioProcessorValueTreeState::Listener,
      private juce::Timer
{
public:
    explicit ADelaySREditor (ADelaySRProcessor&);
    ~ADelaySREditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    void parameterChanged (const juce::String& paramID, float) override;
    void updateTimingVisibility();
    void timerCallback() override;
    void saveRecordingToFile (const juce::File&);

    ADelaySRProcessor& proc;
    AeroLookAndFeel    aeroLAF;

    //── Timing section ────────────────────────────────────────────────────────
    juce::TextButton syncButton      { "SYNC" };
    juce::TextButton noteDelayButton { "NOTE" };

    juce::Slider     delayTimeSlider;
    juce::Label      delayTimeLabel;

    juce::Slider     smoothingSlider;    // vertical, 0–1000 ms
    juce::Label      smoothingLabel;

    juce::Slider     noteDelaySlider;
    juce::Label      noteDelayLabel;

    juce::ComboBox   syncDivCombo;
    juce::Label      syncDivLabel;

    //── Envelope section ──────────────────────────────────────────────────────
    juce::Slider   attackSlider;
    juce::Label    attackLabel;

    juce::Slider   releaseSlider;
    juce::Label    releaseLabel;

    juce::Slider   tapVolSlider;
    juce::Label    tapVolLabel;

    //── Routing section ───────────────────────────────────────────────────────
    juce::TextButton trigButton    { "TRIG"     };
    juce::TextButton wetOnlyButton { "WET ONLY" };
    juce::ComboBox   modeCombo;
    juce::Label      modeLabel;

    //── Recording section ────────────────────────────────────────────────────
    juce::TextButton recButton   { "REC"   };
    juce::TextButton clearButton { "CLEAR" };
    bool             blinkOn   { false };
    std::unique_ptr<juce::FileChooser> fileChooser;

    //── Theme / boost UI state ────────────────────────────────────────────────
    juce::TextButton themeButton     { "DARK" };
    juce::TextButton tapVolBoostButton { "1.2x" };
    bool             isLightMode     { false };

    void applyTheme       (bool light);
    void updateTapVolRange();

    //── APVTS attachments ─────────────────────────────────────────────────────
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BtnAtt    = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAtt> delayTimeAtt, noteDelayAtt;
    std::unique_ptr<SliderAtt> attackAtt, releaseAtt, tapVolAtt, smoothingAtt;
    std::unique_ptr<ComboAtt>  syncDivAtt, modeAtt;
    std::unique_ptr<BtnAtt>    syncBtnAtt, noteDelayBtnAtt, trigBtnAtt, wetOnlyBtnAtt;
    std::unique_ptr<BtnAtt>    tapVolBoostBtnAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ADelaySREditor)
};
