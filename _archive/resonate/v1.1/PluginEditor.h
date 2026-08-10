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
class AKnob : public juce::Slider
{
public:
    AKnob()
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
    
    void setIsNoteDisplay(bool isNote)
    {
        isNoteDisplay = isNote;
        repaint();
    }
    
    void paint(juce::Graphics& g) override
    {
        if (isNoteDisplay)
        {
            // Special formatting for note display: "Note: C4"
            g.setColour(juce::Colours::black);
            g.setFont(12.0f);
            auto bounds = getLocalBounds();
            bounds.removeFromLeft(30);
            int labelWidth = g.getCurrentFont().getStringWidth("Note: ");
            g.drawText("Note: ", bounds.removeFromLeft(labelWidth), juce::Justification::centredLeft);
            
            // Draw note value in cyan and larger
            g.setColour(juce::Colour(0xff00aaaa));
            g.setFont(16.0f);
            g.drawText(value, bounds, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour(juce::Colour(0xff00d4ff)); // Cyan to match sliders
            g.setFont(12.0f);
            g.drawText(value, getLocalBounds(), juce::Justification::centred);
        }
    }
    
private:
    juce::String value;
    bool isNoteDisplay = false;
};

//==============================================================================
// Resonator channel strip
class ResonateChannel : public juce::Component, private juce::Slider::Listener
{
public:
    ResonateChannel(ResonateAudioProcessor& proc, int index);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void sliderValueChanged(juce::Slider* slider) override;
    
    static juce::String getRomanNumeral(int num);

private:
    int resonatorIndex;
    ResonateAudioProcessor& processor;
    
    // Resonator 1 has note selector, others have pitch offset
    AKnob noteKnob;     // Only for resonator 1
    AKnob pitchKnob;    // Only for resonators 2-5
    AKnob fineKnob;
    AKnob gainKnob;
    
    ValueDisplay noteDisplay;   // Only for resonator 1
    ValueDisplay pitchDisplay;  // Only for resonators 2-5
    ValueDisplay fineDisplay;
    ValueDisplay gainDisplay;
    
    ALabel noteLabel{"Note"};     // Only for resonator 1
    ALabel pitchLabel{"Pitch"};   // Only for resonators 2-5
    ALabel fineLabel{"Fine"};
    ALabel gainLabel{"Gain"};
    ALabel numberLabel;
    
    juce::ToggleButton enableButton;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
    
    void updateDisplays();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonateChannel)
};

//==============================================================================
class ResonateAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::Slider::Listener
{
public:
    ResonateAudioProcessorEditor (ResonateAudioProcessor&);
    ~ResonateAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void sliderValueChanged(juce::Slider* slider) override;

private:
    ResonateAudioProcessor& audioProcessor;
    
    // Column 1: Filter controls
    juce::ToggleButton filterOnButton;
    AKnob filterFreqKnob;
    juce::ComboBox filterTypeSelector;
    
    ALabel filterLabel{"Filter"};
    ALabel freqLabel{"Frequency"};
    ALabel filterTypeLabel{"Type"};
    
    ValueDisplay freqDisplay;
    
    // Column 2: Mode, Decay, Const, Color
    juce::ComboBox modeSelector;
    AKnob decayKnob;
    juce::ToggleButton constButton;
    AKnob colorKnob;
    AKnob smoothKnob;
    
    ALabel modeLabel{"Mode"};
    ALabel decayLabel{"Decay"};
    ALabel constLabel{"Const"};
    ALabel colorLabel{"Color"};
    ALabel smoothLabel{"Smooth"};
    
    ValueDisplay decayDisplay;
    ValueDisplay colorDisplay;
    ValueDisplay smoothDisplay;
    
    // Columns 3-7: 5 resonator channels (I-V)
    std::unique_ptr<ResonateChannel> channels[5];
    
    // Column 8: Width, Gain, Dry/Wet, Wet Only
    AKnob widthKnob;
    AKnob gainKnob;
    AKnob dryWetKnob;
    juce::ToggleButton wetOnlyButton;
    
    ALabel widthLabel{"Width"};
    ALabel gainLabel{"Gain"};
    ALabel dryWetLabel{"Dry/Wet"};
    ALabel wetOnlyLabel{"Wet Only"};
    
    ValueDisplay widthDisplay;
    ValueDisplay gainDisplay;
    ValueDisplay dryWetDisplay;
    
    // Column 9: LFO/Chorus controls (NEW)
    AKnob chorusKnob;
    AKnob lfoRateKnob;
    AKnob lfoDepthKnob;
    
    ALabel chorusLabel{"Chorus"};
    ALabel lfoRateLabel{"LFO Rate"};
    ALabel lfoDepthLabel{"LFO Depth"};
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> filterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> constAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> colorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetOnlyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lfoRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lfoDepthAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonateAudioProcessorEditor)
};
