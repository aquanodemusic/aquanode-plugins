/*
  ==============================================================================

    Ableton-Style Resonator UI Implementation
    
    Color scheme: Light blue/gray background with orange accents
    Matches Ableton Live's device aesthetic
    
    Layout:
    Column 1: Filter (On, Frequency, Type selector)
    Column 2: Mode (A/B/C), Decay, Const, Color, Smooth
    Columns 3-7: Resonators I-V (I has Note selector, II-V have Pitch offset)
    Column 8: Width, Gain, Dry/Wet, Wet Only

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Resonator Channel Implementation

ResonatorChannel::ResonatorChannel(ResonatorAudioProcessor& proc, int index)
    : resonatorIndex(index), processor(proc)
{
    // Set up number label (Roman numerals I-V)
    numberLabel.setText(juce::String(getRomanNumeral(index + 1)), juce::dontSendNotification);
    numberLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    addAndMakeVisible(numberLabel);
    
    // Set up enable button
    enableButton.setButtonText("On");
    addAndMakeVisible(enableButton);
    
    // Set up knobs and displays based on resonator index
    if (index == 0)
    {
        // Resonator I has Note selector
        addAndMakeVisible(noteKnob);
        addAndMakeVisible(noteDisplay); // Keep note display visible
        addAndMakeVisible(noteLabel);
        
        noteKnob.setRange(0.0, 127.0, 1.0);
        noteKnob.addListener(this);
        noteKnob.textFromValueFunction = [](double value) {
            return midiNoteToNoteName(static_cast<int>(value));
        };
    }
    else
    {
        // Resonators II-V have Pitch offset
        addAndMakeVisible(pitchKnob);
        // pitchDisplay removed - don't add it
        addAndMakeVisible(pitchLabel);
        
        pitchKnob.setRange(-24, 24, 1);
        pitchKnob.setTextValueSuffix(" st");
        pitchKnob.addListener(this);
    }
    
    addAndMakeVisible(fineKnob);
    addAndMakeVisible(gainKnob);
    
    // fineDisplay and gainDisplay removed - don't add them
    
    addAndMakeVisible(fineLabel);
    addAndMakeVisible(gainLabel);
    
    // Fine knob (cents)
    fineKnob.setRange(-50.0, 50.0, 0.1);
    fineKnob.setTextValueSuffix(" c");
    fineKnob.addListener(this);
    
    // Gain knob
    gainKnob.setRange(-48.0, 12.0, 0.1);
    gainKnob.setTextValueSuffix(" dB");
    gainKnob.addListener(this);
    
    // Create parameter attachments
    juce::String id = "res" + juce::String(index + 1);
    
    if (index == 0)
    {
        // Resonator 1 has note parameter
        noteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc.getParameters(), id + "_note", noteKnob);
    }
    else
    {
        // Resonators 2-5 have pitch parameter
        pitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc.getParameters(), id + "_pitch", pitchKnob);
    }
    
    fineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.getParameters(), id + "_fine", fineKnob);
    
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.getParameters(), id + "_gain", gainKnob);
    
    enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.getParameters(), id + "_enabled", enableButton);
    
    updateDisplays();
}

void ResonatorChannel::sliderValueChanged(juce::Slider* slider)
{
    updateDisplays();
}

void ResonatorChannel::updateDisplays()
{
    if (resonatorIndex == 0)
    {
        // Update note display for resonator 1
        int note = static_cast<int>(noteKnob.getValue());
        noteDisplay.setValue(midiNoteToNoteName(note));
    }
    else
    {
        // Update pitch display for resonators 2-5
        int pitch = static_cast<int>(pitchKnob.getValue());
        pitchDisplay.setValue(juce::String(pitch) + " st");
    }
    
    // Update fine display
    float fine = static_cast<float>(fineKnob.getValue());
    fineDisplay.setValue(juce::String(fine, 1) + " c");
    
    // Update gain display
    float gain = static_cast<float>(gainKnob.getValue());
    gainDisplay.setValue(juce::String(gain, 1) + " dB");
}

juce::String ResonatorChannel::getRomanNumeral(int num)
{
    switch(num)
    {
        case 1: return "I";
        case 2: return "II";
        case 3: return "III";
        case 4: return "IV";
        case 5: return "V";
        default: return juce::String(num);
    }
}

void ResonatorChannel::paint(juce::Graphics& g)
{
    // Light background
    g.setColour(juce::Colour(0xffc8d0d8));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    
    // Subtle border
    g.setColour(juce::Colour(0xffa0a8b0));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
}

void ResonatorChannel::resized()
{
    auto bounds = getLocalBounds().reduced(6);
    
    int knobSize = 70;
    int spacing = 4;
    
    // Number label and enable button at top
    auto topSection = bounds.removeFromTop(24);
    numberLabel.setBounds(topSection.removeFromLeft(22));
    topSection.removeFromLeft(4);
    enableButton.setBounds(topSection);
    
    bounds.removeFromTop(spacing + 4);
    
    if (resonatorIndex == 0)
    {
        // Resonator I: Note section
        noteLabel.setBounds(bounds.removeFromTop(14));
        noteDisplay.setBounds(bounds.removeFromTop(18)); // Keep display for Res I
        noteKnob.setBounds(bounds.removeFromTop(knobSize));
        bounds.removeFromTop(spacing);
    }
    else
    {
        // Resonators II-V: Pitch section
        pitchLabel.setBounds(bounds.removeFromTop(14));
        bounds.removeFromTop(18); // Skip display space
        pitchKnob.setBounds(bounds.removeFromTop(knobSize));
        bounds.removeFromTop(spacing);
    }
    
    // Fine section
    fineLabel.setBounds(bounds.removeFromTop(14));
    bounds.removeFromTop(18); // Skip display space
    fineKnob.setBounds(bounds.removeFromTop(knobSize));
    bounds.removeFromTop(spacing);
    
    // Gain section
    gainLabel.setBounds(bounds.removeFromTop(14));
    bounds.removeFromTop(18); // Skip display space
    gainKnob.setBounds(bounds.removeFromTop(knobSize));
}

//==============================================================================
// Main Editor Implementation

ResonatorAudioProcessorEditor::ResonatorAudioProcessorEditor (ResonatorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set Ableton-style colors
    juce::Colour abletonBlue = juce::Colour(0xffd0d8e0);
    juce::Colour cyan = juce::Colour(0xff00d4ff); // Changed from orange to cyan
    juce::Colour darkGray = juce::Colour(0xff505860);
    juce::Colour yellow = juce::Colour(0xffffff00);
    
    getLookAndFeel().setColour(juce::Slider::thumbColourId, cyan);
    getLookAndFeel().setColour(juce::Slider::rotarySliderFillColourId, cyan);
    getLookAndFeel().setColour(juce::Slider::rotarySliderOutlineColourId, darkGray);
    getLookAndFeel().setColour(juce::Slider::textBoxTextColourId, juce::Colours::black);
    getLookAndFeel().setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::grey);
    getLookAndFeel().setColour(juce::ToggleButton::tickColourId, cyan);
    getLookAndFeel().setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff808080));
    getLookAndFeel().setColour(juce::Label::textColourId, juce::Colours::black);
    getLookAndFeel().setColour(juce::ComboBox::backgroundColourId, yellow);
    getLookAndFeel().setColour(juce::ComboBox::outlineColourId, darkGray);
    getLookAndFeel().setColour(juce::ComboBox::textColourId, juce::Colours::black);
    
    // Column 1: Filter section
    filterOnButton.setButtonText("On");
    addAndMakeVisible(filterOnButton);
    addAndMakeVisible(filterLabel);
    
    addAndMakeVisible(filterFreqKnob);
    addAndMakeVisible(freqLabel);
    // freqDisplay removed
    
    filterFreqKnob.setRange(20.0, 20000.0, 1.0);
    filterFreqKnob.addListener(this);
    
    addAndMakeVisible(filterTypeSelector);
    addAndMakeVisible(filterTypeLabel);
    
    filterTypeSelector.addItem("Lowpass", 1);
    filterTypeSelector.addItem("Highpass", 2);
    filterTypeSelector.addItem("Bandpass", 3);
    filterTypeSelector.addItem("Notch", 4);
    
    // Column 2: Mode, Decay, Const, Color, Smooth
    addAndMakeVisible(modeSelector);
    addAndMakeVisible(modeLabel);
    
    modeSelector.addItem("A", 1);
    modeSelector.addItem("B", 2);
    
    addAndMakeVisible(decayKnob);
    addAndMakeVisible(decayLabel);
    // decayDisplay removed
    
    decayKnob.setRange(0.0, 100.0, 0.1);
    decayKnob.addListener(this);
    
    constButton.setButtonText("Const");
    addAndMakeVisible(constButton);
    addAndMakeVisible(constLabel);
    
    addAndMakeVisible(colorKnob);
    addAndMakeVisible(colorLabel);
    // colorDisplay removed
    
    colorKnob.setRange(0.0, 100.0, 0.1);
    colorKnob.addListener(this);
    
    addAndMakeVisible(smoothKnob);
    addAndMakeVisible(smoothLabel);
    // smoothDisplay removed
    
    smoothKnob.setRange(0.0, 100.0, 0.1);
    smoothKnob.addListener(this);
    
    // Create 5 resonator channels
    for (int i = 0; i < 5; ++i)
    {
        channels[i] = std::make_unique<ResonatorChannel>(audioProcessor, i);
        addAndMakeVisible(channels[i].get());
    }
    
    // Column 8: Width, Gain, Dry/Wet, Wet Only
    addAndMakeVisible(widthKnob);
    addAndMakeVisible(gainKnob);
    addAndMakeVisible(dryWetKnob);
    
    addAndMakeVisible(widthLabel);
    addAndMakeVisible(gainLabel);
    addAndMakeVisible(dryWetLabel);
    
    // widthDisplay, gainDisplay, dryWetDisplay removed
    
    widthKnob.setRange(0.0, 100.0, 1.0);
    widthKnob.setTextValueSuffix(" %");
    widthKnob.addListener(this);
    
    gainKnob.setRange(-48.0, 12.0, 0.1);
    gainKnob.setTextValueSuffix(" dB");
    gainKnob.addListener(this);
    
    dryWetKnob.setRange(0.0, 100.0, 0.1);
    dryWetKnob.setTextValueSuffix(" %");
    dryWetKnob.addListener(this);
    
    wetOnlyButton.setButtonText("Wet Only");
    addAndMakeVisible(wetOnlyButton);
    addAndMakeVisible(wetOnlyLabel);
    
    // Create parameter attachments
    auto& params = audioProcessor.getParameters();
    
    filterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "filter_enabled", filterOnButton);
    
    filterFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "filter_freq", filterFreqKnob);
    
    filterTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        params, "filter_type", filterTypeSelector);
    
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        params, "mode", modeSelector);
    
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "decay", decayKnob);
    
    constAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "const_mode", constButton);
    
    colorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "color", colorKnob);
    
    smoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "smooth", smoothKnob);
    
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "width", widthKnob);
    
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "gain", gainKnob);
    
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "drywet", dryWetKnob);
    
    wetOnlyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "wet_only", wetOnlyButton);
    
    // Start timer for updating displays
    startTimer(100);
    
    setSize (950, 450); // Increased height for smooth knob
}

ResonatorAudioProcessorEditor::~ResonatorAudioProcessorEditor()
{
    stopTimer();
}

void ResonatorAudioProcessorEditor::sliderValueChanged(juce::Slider* slider)
{
    // Displays removed, nothing to update
}

void ResonatorAudioProcessorEditor::timerCallback()
{
    // Update mode selector to match parameter (A or B)
    int modeIndex = static_cast<int>(audioProcessor.getParameters().getRawParameterValue("mode")->load());
    modeSelector.setSelectedId(modeIndex + 1, juce::dontSendNotification);
}

void ResonatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Ableton-style gradient background
    juce::ColourGradient bgGradient(
        juce::Colour(0xffe0e8f0), 0.0f, 0.0f,
        juce::Colour(0xffc8d0d8), 0.0f, static_cast<float>(getHeight()),
        false);
    g.setGradientFill(bgGradient);
    g.fillAll();
    
    // Branding text in bottom left corner
    g.setColour(juce::Colours::white);
    g.setFont(16.0f); // Larger font
    g.drawText("aqua", 20, getHeight() - 90, 60, 20, juce::Justification::left);
    g.drawText("node", 20, getHeight() - 68, 60, 20, juce::Justification::left);
    g.drawText("reso", 20, getHeight() - 46, 60, 20, juce::Justification::left);
    g.drawText("nate", 20, getHeight() - 24, 60, 20, juce::Justification::left);
}

void ResonatorAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    
    int knobSize = 70;
    int spacing = 8;
    
    // Layout: Column 1 (Filter) | Column 2 (Mode/Decay/Const/Color/Smooth) | Columns 3-7 (Resonators I-V) | Column 8 (Width/Gain/Dry-Wet)
    
    // Column 1: Filter section
    auto col1 = bounds.removeFromLeft(100);
    
    filterLabel.setBounds(col1.removeFromTop(14));
    filterOnButton.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    
    freqLabel.setBounds(col1.removeFromTop(14));
    col1.removeFromTop(18); // Skip display space
    filterFreqKnob.setBounds(col1.removeFromTop(knobSize));
    col1.removeFromTop(spacing);
    
    filterTypeLabel.setBounds(col1.removeFromTop(14));
    filterTypeSelector.setBounds(col1.removeFromTop(24));
    
    bounds.removeFromLeft(spacing);
    
    // Column 2: Mode, Decay, Const, Color, Smooth
    auto col2 = bounds.removeFromLeft(100);
    
    modeLabel.setBounds(col2.removeFromTop(14));
    modeSelector.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    
    decayLabel.setBounds(col2.removeFromTop(14));
    col2.removeFromTop(18); // Skip display space
    decayKnob.setBounds(col2.removeFromTop(knobSize));
    col2.removeFromTop(spacing);
    
    constLabel.setBounds(col2.removeFromTop(14));
    constButton.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    
    colorLabel.setBounds(col2.removeFromTop(14));
    col2.removeFromTop(18); // Skip display space
    colorKnob.setBounds(col2.removeFromTop(knobSize));
    col2.removeFromTop(spacing);
    
    smoothLabel.setBounds(col2.removeFromTop(14));
    col2.removeFromTop(18); // Skip display space
    smoothKnob.setBounds(col2.removeFromTop(knobSize));
    
    bounds.removeFromLeft(spacing);
    
    // Column 8: Width, Gain, Dry/Wet, Wet Only (rightmost)
    auto col8 = bounds.removeFromRight(100);
    
    widthLabel.setBounds(col8.removeFromTop(14));
    col8.removeFromTop(18); // Skip display space
    widthKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    
    gainLabel.setBounds(col8.removeFromTop(14));
    col8.removeFromTop(18); // Skip display space
    gainKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    
    dryWetLabel.setBounds(col8.removeFromTop(14));
    col8.removeFromTop(18); // Skip display space
    dryWetKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    
    wetOnlyLabel.setBounds(col8.removeFromTop(14));
    wetOnlyButton.setBounds(col8.removeFromTop(24));
    
    bounds.removeFromRight(spacing);
    
    // Columns 3-7: 5 resonator channels (I-V)
    int channelWidth = bounds.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
    {
        auto channelBounds = bounds.removeFromLeft(channelWidth).reduced(3);
        channels[i]->setBounds(channelBounds);
    }
}
