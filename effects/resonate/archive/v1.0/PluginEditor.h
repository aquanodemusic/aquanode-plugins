/*
  ==============================================================================

    Ableton-Style Resonator UI
    Matching the original Ableton layout and color scheme

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Helper function to convert MIDI note to note name
static juce::String midiNoteToNoteName(int midiNote)
{
    const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (midiNote / 12) - 1;
    int note = midiNote % 12;
    return juce::String(noteNames[note]) + juce::String(octave);
}

//==============================================================================
// Custom knob with Ableton-style appearance
class AbletonKnob : public juce::Slider
{
public:
    AbletonKnob()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                           juce::MathConstants<float>::pi * 2.8f,
                           true);
    }
};

//==============================================================================
// Custom label
class AbletonLabel : public juce::Label
{
public:
    AbletonLabel(const juce::String& text = {})
    {
        setText(text, juce::dontSendNotification);
        setJustificationType(juce::Justification::centred);
    }
};

//==============================================================================
// Value display for showing current value above knob
class ValueDisplay : public juce::Component
{
public:
    ValueDisplay() {}
    
    void setValue(const juce::String& val)
    {
        value = val;
        repaint();
    }
    
    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff00d4ff)); // Cyan to match sliders
        g.setFont(12.0f);
        g.drawText(value, getLocalBounds(), juce::Justification::centred);
    }
    
private:
    juce::String value;
};

//==============================================================================
// Resonator channel strip
class ResonatorChannel : public juce::Component, private juce::Slider::Listener
{
public:
    ResonatorChannel(ResonatorAudioProcessor& proc, int index);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void sliderValueChanged(juce::Slider* slider) override;
    
    static juce::String getRomanNumeral(int num);

private:
    int resonatorIndex;
    ResonatorAudioProcessor& processor;
    
    // Resonator 1 has note selector, others have pitch offset
    AbletonKnob noteKnob;     // Only for resonator 1
    AbletonKnob pitchKnob;    // Only for resonators 2-5
    AbletonKnob fineKnob;
    AbletonKnob gainKnob;
    
    ValueDisplay noteDisplay;   // Only for resonator 1
    ValueDisplay pitchDisplay;  // Only for resonators 2-5
    ValueDisplay fineDisplay;
    ValueDisplay gainDisplay;
    
    AbletonLabel noteLabel{"Note"};     // Only for resonator 1
    AbletonLabel pitchLabel{"Pitch"};   // Only for resonators 2-5
    AbletonLabel fineLabel{"Fine"};
    AbletonLabel gainLabel{"Gain"};
    AbletonLabel numberLabel;
    
    juce::ToggleButton enableButton;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
    
    void updateDisplays();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonatorChannel)
};

//==============================================================================
class ResonatorAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       private juce::Slider::Listener
{
public:
    ResonatorAudioProcessorEditor (ResonatorAudioProcessor&);
    ~ResonatorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void sliderValueChanged(juce::Slider* slider) override;

private:
    ResonatorAudioProcessor& audioProcessor;
    
    // Column 1: Filter controls
    juce::ToggleButton filterOnButton;
    AbletonKnob filterFreqKnob;
    juce::ComboBox filterTypeSelector;
    
    AbletonLabel filterLabel{"Filter"};
    AbletonLabel freqLabel{"Frequency"};
    AbletonLabel filterTypeLabel{"Type"};
    
    ValueDisplay freqDisplay;
    
    // Column 2: Mode, Decay, Const, Color
    juce::ComboBox modeSelector;
    AbletonKnob decayKnob;
    juce::ToggleButton constButton;
    AbletonKnob colorKnob;
    AbletonKnob smoothKnob;
    
    AbletonLabel modeLabel{"Mode"};
    AbletonLabel decayLabel{"Decay"};
    AbletonLabel constLabel{"Const"};
    AbletonLabel colorLabel{"Color"};
    AbletonLabel smoothLabel{"Smooth"};
    
    ValueDisplay decayDisplay;
    ValueDisplay colorDisplay;
    ValueDisplay smoothDisplay;
    
    // Columns 3-7: 5 resonator channels (I-V)
    std::unique_ptr<ResonatorChannel> channels[5];
    
    // Column 8: Width, Gain, Dry/Wet, Wet Only
    AbletonKnob widthKnob;
    AbletonKnob gainKnob;
    AbletonKnob dryWetKnob;
    juce::ToggleButton wetOnlyButton;
    
    AbletonLabel widthLabel{"Width"};
    AbletonLabel gainLabel{"Gain"};
    AbletonLabel dryWetLabel{"Dry/Wet"};
    AbletonLabel wetOnlyLabel{"Wet Only"};
    
    ValueDisplay widthDisplay;
    ValueDisplay gainDisplay;
    ValueDisplay dryWetDisplay;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> filterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> constAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> colorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetOnlyAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonatorAudioProcessorEditor)
};
