/*
  ==============================================================================
    LiquidChor - Roland Juno BBD Chorus
    PluginEditor.h
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Colours
namespace LCColours
{
    const juce::Colour paper      { 0xFFF7EED8 };
    const juce::Colour paperDark  { 0xFFECDFBE };
    const juce::Colour paperDeep  { 0xFFDFCDA0 };
    const juce::Colour ink        { 0xFF2B1F12 };
    const juce::Colour inkFaint   { 0xFF6B5540 };
    const juce::Colour cyanBright { 0xFF38B9CC };
    const juce::Colour cyanDeep   { 0xFF1F8A9C };
    const juce::Colour cyanPale   { 0xFFBEEDF3 };
    const juce::Colour cyanGlow   { 0xFF82DDE8 };
    const juce::Colour coral      { 0xFFCB6040 };
    const juce::Colour headerBg   { 0xFF1A4A52 };
    const juce::Colour headerText { 0xFFDDF7FA };
}

//==============================================================================
class LiquidChorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LiquidChorLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderOutlineColourId, LCColours::paperDeep);
        setColour (juce::Slider::rotarySliderFillColourId,    LCColours::cyanBright);
        setColour (juce::Slider::thumbColourId,               LCColours::ink);
        setColour (juce::Label::textColourId,                 LCColours::ink);
        setColour (juce::ComboBox::backgroundColourId,        LCColours::paperDark);
        setColour (juce::ComboBox::textColourId,              LCColours::ink);
        setColour (juce::ComboBox::outlineColourId,           LCColours::inkFaint);
        setColour (juce::ComboBox::arrowColourId,             LCColours::cyanDeep);
        setColour (juce::PopupMenu::backgroundColourId,       LCColours::paper);
        setColour (juce::PopupMenu::textColourId,             LCColours::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, LCColours::cyanPale);
        setColour (juce::PopupMenu::highlightedTextColourId,  LCColours::ink);
    }

    //==========================================================================
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& /*slider*/) override
    {
        const float cx    = x + width  * 0.5f;
        const float cy    = y + height * 0.5f;
        const float r     = juce::jmin (width, height) * 0.5f - 3.0f;
        const float angle = rotaryStartAngle
                            + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        g.setColour (LCColours::paper);
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

        {
            juce::Path track;
            track.addArc (cx - r + 5, cy - r + 5,
                          (r - 5) * 2.0f, (r - 5) * 2.0f,
                          rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (LCColours::paperDeep);
            g.strokePath (track, juce::PathStrokeType (2.5f,
                          juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
        }

        if (sliderPos > 0.0f)
        {
            juce::Path arc;
            arc.addArc (cx - r + 5, cy - r + 5,
                        (r - 5) * 2.0f, (r - 5) * 2.0f,
                        rotaryStartAngle, angle, true);
            g.setColour (LCColours::cyanGlow);
            g.strokePath (arc, juce::PathStrokeType (2.5f,
                          juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
        }

        g.setColour (LCColours::inkFaint);
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.2f);

        {
            const float pl = r * 0.55f;
            const float px = cx + std::sin (angle) * pl;
            const float py = cy - std::cos (angle) * pl;
            g.setColour (LCColours::ink);
            juce::Path ptr;
            ptr.startNewSubPath (cx, cy);
            ptr.lineTo (px, py);
            g.strokePath (ptr, juce::PathStrokeType (1.8f,
                          juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
        }

        g.setColour (LCColours::cyanDeep);
        g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
    }

    //==========================================================================
    void drawToggleButton (juce::Graphics& g,
                           juce::ToggleButton& button,
                           bool /*highlighted*/, bool /*down*/) override
    {
        const bool on     = button.getToggleState();
        auto       bounds = button.getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (on ? LCColours::cyanDeep : LCColours::paperDark);
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (on ? LCColours::cyanGlow : LCColours::inkFaint);
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        g.setColour (on ? LCColours::paper : LCColours::inkFaint);
        g.setFont (juce::Font (juce::FontOptions().withHeight (10.5f)
                                                  .withStyle ("Bold")));
        g.drawText (button.getButtonText(), button.getLocalBounds(),
                    juce::Justification::centred, false);
    }

    //==========================================================================
    juce::Font getLabelFont (juce::Label& label) override
    {
        return juce::Font (juce::FontOptions().withHeight (
            label.getProperties().getWithDefault ("smallFont", false)
                ? 9.5f : 10.5f));
    }

    //==========================================================================
    void drawComboBox (juce::Graphics& g,
                       int w, int h,
                       bool /*down*/,
                       int bx, int by, int bw, int bh,
                       juce::ComboBox& /*box*/) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, (float)w, (float)h);
        g.setColour (LCColours::paperDark);
        g.fillRoundedRectangle (bounds.reduced (0.5f), 4.0f);
        g.setColour (LCColours::inkFaint);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        juce::Path arrow;
        const float ax = bx + bw * 0.5f;
        const float ay = by + bh * 0.5f;
        arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (LCColours::cyanDeep);
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (juce::Font (juce::FontOptions().withHeight (10.5f)));
    }
};

//==============================================================================
class LiquidChorAudioProcessorEditor
    : public juce::AudioProcessorEditor,
      public juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit LiquidChorAudioProcessorEditor (LiquidChorAudioProcessor&);
    ~LiquidChorAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

    void parameterChanged (const juce::String& paramID, float newValue) override;

private:
    LiquidChorAudioProcessor& audioProcessor;
    LiquidChorLookAndFeel      lnf;

    // ── Helpers ───────────────────────────────────────────────────────────
    juce::Slider& makeKnob  (juce::Slider& s);
    juce::Label&  makeLabel (juce::Label& l, const juce::String& text);

    // Positions a knob + label pair centred at (cx, cy)
    void placeKnob (juce::Slider& s, juce::Label& l, int cx, int cy);
    // Narrow-label variant for 3-column rows
    void placeKnobN (juce::Slider& s, juce::Label& l, int cx, int cy);

    // ── DELAY ────────────────────────────────────────────────────────────
    juce::Slider time1Slider,    time2Slider,
                 feedbackSlider, hpassSlider,
                 lpassSlider;
    juce::Label  time1Label,     time2Label,
                 feedbackLabel,  hpassLabel,
                 lpassLabel;

    // ── MODULATION ───────────────────────────────────────────────────────
    juce::Slider       startPhaseSlider, lrPhaseSlider, speedSlider;
    juce::Label        startPhaseLabel,  lrPhaseLabel,  speedLabel;
    juce::ComboBox     lfoTypeBox, syncDivBox;
    juce::Label        lfoTypeLabel, syncDivLabel;
    juce::ToggleButton tempoSyncButton;

    // ── LEVELS ───────────────────────────────────────────────────────────
    juce::Slider       noiseSlider, gainSlider, mixSlider;
    juce::Label        noiseLabel,  gainLabel,  mixLabel;
    juce::ComboBox     noiseModeBox;
    juce::Label        noiseModeLabel;
    juce::ToggleButton invertWetButton, noiseGateButton;

    // ── APVTS Attachments ────────────────────────────────────────────────
    using SliderAtt  = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt  = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt   = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAtt> time1Att,    time2Att,
                               feedbackAtt, hpassAtt,
                               lpassAtt;
    std::unique_ptr<SliderAtt> startPhaseAtt, lrPhaseAtt, speedAtt;
    std::unique_ptr<SliderAtt> noiseAtt, gainAtt, mixAtt;
    std::unique_ptr<ButtonAtt> invertWetAtt, noiseGateAtt, tempoSyncAtt;
    std::unique_ptr<ComboAtt>  lfoTypeAtt, syncDivAtt, noiseModeAtt;

    // ── Layout constants ─────────────────────────────────────────────────
    static constexpr int KNOB_SZ  = 62;
    static constexpr int LABEL_H  = 15;
    static constexpr int LABEL_W  = 76;
    static constexpr int LABEL_WN = 68;   // narrow variant for 3-col rows

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiquidChorAudioProcessorEditor)
};
