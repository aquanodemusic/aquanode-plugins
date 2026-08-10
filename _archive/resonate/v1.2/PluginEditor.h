/*
  ==============================================================================

    Ableton-Style Resonator UI
    Matching the original Ableton layout and color scheme.

    New UI in this version:
    - Center toggle (below Color knob) – OFF = Ableton DC bias
    - Per Res toggle (below Center) – expands channel strips with
      per-resonator Decay + Color knobs
    - Plugin height grows when Per Res mode is active

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

    static juce::String getRomanNumeral(int num);

    // Fixed heights so editor can calculate total size
    static constexpr int HEIGHT_NORMAL  = 360;
    static constexpr int HEIGHT_PER_RES = 560;  // + 2 extra knob blocks

private:
    int  resonatorIndex;
    bool perResActive = false;
    ResonateAudioProcessor& processor;

    AKnob noteKnob;   // Resonator I only
    AKnob pitchKnob;  // Resonators II-V
    AKnob fineKnob;
    AKnob gainKnob;

    // Per-res decay/color/const (shown only when perResActive)
    AKnob              perResDecayKnob;
    AKnob              perResColorKnob;
    juce::ToggleButton perResConstButton;

    ValueDisplay noteDisplay;
    ALabel noteLabel{"Note"};
    ALabel pitchLabel{"Pitch"};
    ALabel fineLabel{"Fine"};
    ALabel gainLabel{"Gain"};
    ALabel perResDecayLabel{"Decay"};
    ALabel perResColorLabel{"Color"};
    ALabel perResConstLabel{"Const"};
    ALabel numberLabel;

    juce::ToggleButton enableButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  perResDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  perResColorAttachment;
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

    // Column 1: Filter + Smooth
    juce::ToggleButton filterOnButton;
    AKnob              filterFreqKnob;
    juce::ComboBox     filterTypeSelector;
    AKnob              smoothKnob;
    ALabel filterLabel{"Filter"};
    ALabel freqLabel{"Frequency"};
    ALabel filterTypeLabel{"Type"};
    ALabel smoothLabel{"Smooth"};
    ValueDisplay freqDisplay;

    // Column 2: Mode / Decay / Const / Color / Center / Per Res
    juce::ComboBox     modeSelector;
    AKnob              decayKnob;
    juce::ToggleButton constButton;
    AKnob              colorKnob;
    juce::ToggleButton centerButton;
    juce::ToggleButton perResButton;
    juce::ToggleButton expDecayButton;
    ALabel modeLabel{"Mode"};
    ALabel decayLabel{"Decay"};
    ALabel constLabel{"Const"};
    ALabel colorLabel{"Color"};
    ALabel centerLabel{"DC Center"};
    ALabel expDecayLabel{"Exp Decay"};
    ALabel perResLabel{"Per Res"};     // NEW
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
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   smoothAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   chorusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   dryWetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   wetOnlyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   lfoRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   lfoDepthAttachment;

    // Track per-res state for resize
    bool currentPerResMode = false;
    void applyPerResMode(bool active);

    static constexpr int BASE_HEIGHT    = 420;
    static constexpr int PER_RES_EXTRA  = 250;  // 2 knob rows + 1 button row + spacing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResonateAudioProcessorEditor)
};
