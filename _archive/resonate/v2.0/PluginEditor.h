/*
  ==============================================================================

    Ableton-Style Resonator UI
    Matching the original Ableton layout and color scheme.

    New UI in this version:
    - Global Decay / Const / Color grey out while Per Res is active
      (these are the only three globals the engine actually overrides)
    - Per-resonator Pan knob in the Per Res strip
    - Per-resonator MIDI note readout
    - Resizable window: everything lives in a scaled content component so
      the fixed-pixel layout is preserved exactly at any size
    - Save / Load preset buttons writing standalone .xml files
    - MIDI In toggle

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
static juce::String midiNoteToNoteName(int midiNote)
{
    const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int octave = (midiNote / 12) - 1;
    int note   = midiNote % 12;
    return juce::String(noteNames[note]) + juce::String(octave);
}

//==============================================================================
class AKnob : public juce::Slider
{
public:
    AKnob()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                            juce::MathConstants<float>::pi * 2.8f, true);
    }
};

//==============================================================================
class ALabel : public juce::Label
{
public:
    ALabel(const juce::String& text = {})
    {
        setText(text, juce::dontSendNotification);
        setJustificationType(juce::Justification::centred);
    }
};

//==============================================================================
class ValueDisplay : public juce::Component
{
public:
    void setValue(const juce::String& val) { value = val; repaint(); }
    void setIsNoteDisplay(bool b)          { isNoteDisplay = b; repaint(); }

    void paint(juce::Graphics& g) override
    {
        if (isNoteDisplay)
        {
            g.setColour(juce::Colours::black);
            g.setFont(12.0f);
            auto bounds = getLocalBounds();
            bounds.removeFromLeft(30);
            int lw = g.getCurrentFont().getStringWidth("Note: ");
            g.drawText("Note: ", bounds.removeFromLeft(lw),
                       juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff00aaaa));
            g.setFont(16.0f);
            g.drawText(value, bounds, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour(juce::Colour(0xff00d4ff));
            g.setFont(12.0f);
            g.drawText(value, getLocalBounds(), juce::Justification::centred);
        }
    }
private:
    juce::String value;
    bool isNoteDisplay = false;
};

//==============================================================================
// Small readout showing which MIDI note (if any) currently drives a resonator.
class MidiNoteDisplay : public juce::Component
{
public:
    void setNote(int midiNote)
    {
        if (midiNote == note) return;
        note = midiNote;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (note < 0)
            return;

        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xff00aaaa).withAlpha(0.18f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(juce::Colour(0xff007e8a));
        g.setFont(11.0f);
        g.drawText("MIDI " + midiNoteToNoteName(note), getLocalBounds(),
                   juce::Justification::centred);
    }

private:
    int note = -1;
};

//==============================================================================
// Branding, drawn inside the scaled content so it resizes with everything else.
class BrandingPanel : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colours::white);
        g.setFont(16.0f);
        g.drawText("aqua",  0, 0,  60, 20, juce::Justification::left);
        g.drawText("node",  0, 20, 60, 20, juce::Justification::left);
        g.drawText("reso", 42, 0,  60, 20, juce::Justification::left);
        g.drawText("nate", 42, 20, 60, 20, juce::Justification::left);
    }
};

//==============================================================================
class ResonateChannel : public juce::Component, private juce::Slider::Listener
{
public:
    ResonateChannel(ResonateAudioProcessor& proc, int index);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void sliderValueChanged(juce::Slider* slider) override;

    // Call from editor when per-res toggle changes; triggers resized()
    void setPerResMode(bool active);
    bool getPerResMode() const { return perResActive; }

    // Called from the editor's timer to reflect incoming MIDI
    void refreshMidiDisplay();

    static juce::String getRomanNumeral(int num);

    // Fixed heights so editor can calculate total size
    static constexpr int HEIGHT_NORMAL  = 380;
    static constexpr int HEIGHT_PER_RES = 750;

private:
    int  resonatorIndex;
    bool perResActive = false;
    ResonateAudioProcessor& processor;

    AKnob noteKnob;   // Resonator I only
    AKnob pitchKnob;  // Resonators II-V
    AKnob fineKnob;
    AKnob gainKnob;

    // Per-res decay/color/const/pan (shown only when perResActive)
    AKnob              perResDecayKnob;
    AKnob              perResColorKnob;
    AKnob              perResPanKnob;
    juce::ToggleButton perResConstButton;

    ValueDisplay    noteDisplay;
    MidiNoteDisplay midiDisplay;

    ALabel noteLabel{"Note"};
    ALabel pitchLabel{"Pitch"};
    ALabel fineLabel{"Fine"};
    ALabel gainLabel{"Gain"};
    ALabel perResDecayLabel{"Decay"};
    ALabel perResColorLabel{"Color"};
    ALabel perResConstLabel{"Const"};
    ALabel perResPanLabel{"Pan"};
    ALabel numberLabel;

    juce::ToggleButton enableButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  perResDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  perResColorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  perResPanAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>  perResConstAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>  enableAttachment;

    void updateDisplays();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResonateChannel)
};

//==============================================================================
class ResonateAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::Slider::Listener
{
public:
    ResonateAudioProcessorEditor(ResonateAudioProcessor&);
    ~ResonateAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void sliderValueChanged(juce::Slider* slider) override;

private:
    ResonateAudioProcessor& audioProcessor;

    // Everything below lives inside this, which gets an AffineTransform scale.
    // That means the whole fixed-pixel layout below stays exactly as designed
    // and simply gets drawn larger or smaller.
    juce::Component content;
    BrandingPanel   branding;

    juce::ComponentBoundsConstrainer constrainer;
    double uiScale = 1.0;

    // Column 1: Filter + Smooth + Exp Decay + MIDI
    juce::ToggleButton filterOnButton;
    AKnob              filterFreqKnob;
    juce::ComboBox     filterTypeSelector;
    AKnob              smoothKnob;
    juce::ToggleButton midiEnableButton;
    ALabel filterLabel{"Filter"};
    ALabel freqLabel{"Frequency"};
    ALabel filterTypeLabel{"Type"};
    ALabel smoothLabel{"Smooth"};
    ALabel midiLabel{"MIDI In"};
    ValueDisplay freqDisplay;

    // Column 2: Mode / Decay / Const / Color / Per Res / Presets
    juce::ComboBox     modeSelector;
    AKnob              decayKnob;
    juce::ToggleButton constButton;
    AKnob              colorKnob;
    juce::ToggleButton centerButton;
    juce::ToggleButton perResButton;
    juce::ToggleButton expDecayButton;
    juce::TextButton   savePresetButton;
    juce::TextButton   loadPresetButton;
    ALabel modeLabel{"Mode"};
    ALabel decayLabel{"Decay"};
    ALabel constLabel{"Const"};
    ALabel colorLabel{"Color"};
    ALabel centerLabel{"DC Center"};
    ALabel expDecayLabel{"Exp Decay"};
    ALabel perResLabel{"Per Res"};
    ALabel presetLabel{"Preset"};
    ValueDisplay decayDisplay;
    ValueDisplay colorDisplay;

    // Columns 3-7: Resonator channels
    std::unique_ptr<ResonateChannel> channels[5];

    // Column 8: Width / Gain / Dry-Wet / Wet Only
    AKnob              widthKnob;
    AKnob              gainKnob;
    AKnob              dryWetKnob;
    juce::ToggleButton wetOnlyButton;
    ALabel widthLabel{"Width"};
    ALabel gainLabel{"Gain"};
    ALabel dryWetLabel{"Dry/Wet"};
    ALabel wetOnlyLabel{"Wet Only"};
    ValueDisplay widthDisplay;
    ValueDisplay gainDisplay;
    ValueDisplay dryWetDisplay;

    // Column 9: Chorus / LFO
    AKnob  chorusKnob;
    AKnob  lfoRateKnob;
    AKnob  lfoDepthKnob;
    ALabel chorusLabel{"Chorus"};
    ALabel lfoRateLabel{"LFO Rate"};
    ALabel lfoDepthLabel{"LFO Depth"};

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   filterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   filterFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   constAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   colorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   centerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   perResAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   expDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   midiEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   smoothAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   chorusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   dryWetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   wetOnlyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   lfoRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   lfoDepthAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Track per-res state for resize
    bool currentPerResMode = false;
    void applyPerResMode(bool active);
    void updateGlobalEnablement();
    void layoutContent();
    void updateEditorSize();
    int  logicalHeight() const
    {
        return currentPerResMode ? (BASE_HEIGHT + PER_RES_EXTRA) : BASE_HEIGHT;
    }

    void showSavePresetDialog();
    void showLoadPresetDialog();
    void flashPresetMessage(const juce::String& msg);
    int  presetMessageTicks = 0;

    static constexpr int LOGICAL_WIDTH  = 1050;
    static constexpr int BASE_HEIGHT    = 460;
    static constexpr int PER_RES_EXTRA  = 380;  // decay + const + color + pan rows

    static constexpr double MIN_SCALE = 0.55;
    static constexpr double MAX_SCALE = 2.00;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResonateAudioProcessorEditor)
};
