/*
  ==============================================================================

    Ableton-Style Resonator UI Implementation
    
    Color scheme: Light blue/gray background with orange accents
    Matches Ableton Live's device aesthetic
    
    Layout:
    Column 1: Filter (On, Frequency, Type selector), Smooth
    Column 2: Mode (A/B), Decay, Const, Color
    Columns 3-7: Resonators I-V (I has Note selector, II-V have Pitch offset)
    Column 8: Width, Gain, Dry/Wet, Wet Only
    Column 9: Chorus, LFO Rate, LFO Depth

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Resonator Channel Implementation

ResonateChannel::ResonateChannel(ResonateAudioProcessor& proc, int index)
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
        // Resonator I has Note selector with special display
        addAndMakeVisible(noteKnob);
        addAndMakeVisible(noteDisplay);
        noteDisplay.setIsNoteDisplay(true); // Enable special formatting
        addAndMakeVisible(noteLabel);
        
        noteKnob.setRange(0.0, 127.0, 1.0);
        noteKnob.addListener(this);
        noteKnob.textFromValueFunction = [](double value) {
            return midiNoteToNoteName(static_cast<int>(value));
        };
        
        // Hide the standard "Note" label since it's now part of the display
        noteLabel.setVisible(false);
    }
    else
    {
        // Resonators II-V have Pitch offset
        addAndMakeVisible(pitchKnob);
        addAndMakeVisible(pitchLabel);
        
        pitchKnob.setRange(-24, 24, 1);
        pitchKnob.setTextValueSuffix(" st");
        pitchKnob.addListener(this);
    }
    
    addAndMakeVisible(fineKnob);
    addAndMakeVisible(gainKnob);
    
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

void ResonateChannel::sliderValueChanged(juce::Slider* slider)
{
    updateDisplays();
}

void ResonateChannel::updateDisplays()
{
    if (resonatorIndex == 0)
    {
        // Update note display for resonator 1
        int note = static_cast<int>(noteKnob.getValue());
        noteDisplay.setValue(midiNoteToNoteName(note));
    }
}

juce::String ResonateChannel::getRomanNumeral(int num)
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

void ResonateChannel::paint(juce::Graphics& g)
{
    // Light background
    g.setColour(juce::Colour(0xffc8d0d8));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    
    // Subtle border
    g.setColour(juce::Colour(0xffa0a8b0));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
}


void ResonateChannel::resized()
{
    auto bounds = getLocalBounds().reduced(4);

    int knobSize = 90;   // Standard 90px for all knobs
    int labelHeight = 14;
    int spacing = 4;

    // 1. Top Section (Roman numeral I-V and On-Button)
    auto topSection = bounds.removeFromTop(22);
    numberLabel.setBounds(topSection.removeFromLeft(35)); // Width for "III"
    topSection.removeFromLeft(2); // Small gap
    enableButton.setBounds(topSection.removeFromLeft(50));

    bounds.removeFromTop(spacing);

    // 2. First Block: NOTE (Res I) or PITCH (Res II-V)
    if (resonatorIndex == 0)
    {
        // Note display shows "Note: C4" format
        noteDisplay.setBounds(bounds.removeFromTop(14));
        noteKnob.setBounds(bounds.removeFromTop(knobSize));
    }
    else
    {
        pitchLabel.setBounds(bounds.removeFromTop(labelHeight));
        pitchKnob.setBounds(bounds.removeFromTop(knobSize));
    }

    bounds.removeFromTop(spacing);

    // 3. Second Block: FINE
    fineLabel.setBounds(bounds.removeFromTop(labelHeight));
    fineKnob.setBounds(bounds.removeFromTop(knobSize));

    bounds.removeFromTop(spacing);

    // 4. Third Block: GAIN
    gainLabel.setBounds(bounds.removeFromTop(labelHeight));
    gainKnob.setBounds(bounds.removeFromTop(knobSize));
}

//==============================================================================
// Main Editor Implementation

ResonateAudioProcessorEditor::ResonateAudioProcessorEditor (ResonateAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set style colors
    juce::Colour aBlue = juce::Colour(0xffd0d8e0);
    juce::Colour cyan = juce::Colour(0xff00d4ff);
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
    
    filterFreqKnob.setRange(20.0, 20000.0, 1.0);
    filterFreqKnob.addListener(this);
    
    addAndMakeVisible(filterTypeSelector);
    addAndMakeVisible(filterTypeLabel);
    
    filterTypeSelector.addItem("Lowpass", 1);
    filterTypeSelector.addItem("Highpass", 2);
    filterTypeSelector.addItem("Bandpass", 3);
    filterTypeSelector.addItem("Notch", 4);
    
    // Smooth knob (moved to column 1)
    addAndMakeVisible(smoothKnob);
    addAndMakeVisible(smoothLabel);
    
    smoothKnob.setRange(0.0, 100.0, 0.1);
    smoothKnob.addListener(this);
    
    // Column 2: Mode, Decay, Const, Color
    addAndMakeVisible(modeSelector);
    addAndMakeVisible(modeLabel);
    
    modeSelector.addItem("A", 1);
    modeSelector.addItem("B", 2);
    
    addAndMakeVisible(decayKnob);
    addAndMakeVisible(decayLabel);
    
    decayKnob.setRange(0.0, 100.0, 0.1);
    decayKnob.addListener(this);
    
    constButton.setButtonText("Const");
    addAndMakeVisible(constButton);
    addAndMakeVisible(constLabel);
    
    addAndMakeVisible(colorKnob);
    addAndMakeVisible(colorLabel);
    
    colorKnob.setRange(0.0, 100.0, 0.1);
    colorKnob.addListener(this);
    
    // Create 5 resonator channels
    for (int i = 0; i < 5; ++i)
    {
        channels[i] = std::make_unique<ResonateChannel>(audioProcessor, i);
        addAndMakeVisible(channels[i].get());
    }
    
    // Column 8: Width, Gain, Dry/Wet, Wet Only
    addAndMakeVisible(widthKnob);
    addAndMakeVisible(gainKnob);
    addAndMakeVisible(dryWetKnob);
    
    addAndMakeVisible(widthLabel);
    addAndMakeVisible(gainLabel);
    addAndMakeVisible(dryWetLabel);
    
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
    
    // Column 9: LFO/Chorus controls (NEW)
    addAndMakeVisible(chorusKnob);
    addAndMakeVisible(chorusLabel);
    
    chorusKnob.setRange(0.0, 100.0, 0.1);
    chorusKnob.setTextValueSuffix(" %");
    chorusKnob.addListener(this);
    
    addAndMakeVisible(lfoRateKnob);
    addAndMakeVisible(lfoRateLabel);
    
    lfoRateKnob.setRange(0.1, 5.0, 0.01);
    lfoRateKnob.setTextValueSuffix(" Hz");
    lfoRateKnob.addListener(this);
    
    addAndMakeVisible(lfoDepthKnob);
    addAndMakeVisible(lfoDepthLabel);
    
    lfoDepthKnob.setRange(0.0, 20.0, 0.1);
    lfoDepthKnob.setTextValueSuffix(" c");
    lfoDepthKnob.addListener(this);
    
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
    
    chorusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "chorus", chorusKnob);
    
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "width", widthKnob);
    
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "gain", gainKnob);
    
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "drywet", dryWetKnob);
    
    wetOnlyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "wet_only", wetOnlyButton);
    
    // LFO parameter attachments (NEW)
    lfoRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "lfo_rate", lfoRateKnob);
    
    lfoDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "lfo_depth", lfoDepthKnob);
    
    // Start timer for updating displays
    startTimer(100);
    
    setSize (1050, 400);  // Increased width to accommodate new column
}

ResonateAudioProcessorEditor::~ResonateAudioProcessorEditor()
{
    stopTimer();
}

void ResonateAudioProcessorEditor::sliderValueChanged(juce::Slider* slider)
{
    // Displays removed, nothing to update
}

void ResonateAudioProcessorEditor::timerCallback()
{
    // Update mode selector to match parameter (A or B)
    int modeIndex = static_cast<int>(audioProcessor.getParameters().getRawParameterValue("mode")->load());
    modeSelector.setSelectedId(modeIndex + 1, juce::dontSendNotification);
}

void ResonateAudioProcessorEditor::paint (juce::Graphics& g)
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
    g.drawText("aqua", 28, getHeight() - 55, 60, 20, juce::Justification::left);
    g.drawText("node", 28, getHeight() - 35, 60, 20, juce::Justification::left);
    g.drawText("reso", 70, getHeight() - 55, 60, 20, juce::Justification::left);
    g.drawText("nate", 70, getHeight() - 35, 60, 20, juce::Justification::left);
}

void ResonateAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    
    int knobSize = 90;  // Standard 90px for ALL knobs
    int labelHeight = 14;
    int spacing = 8;
    
    // Layout: Column 1 (Filter/Smooth) | Column 2 (Mode/Decay/Const/Color) | Columns 3-7 (Resonators I-V) | Column 8 (Width/Gain/Dry-Wet) | Column 9 (Chorus/LFO)
    
    // Column 1: Filter section + Smooth
    auto col1 = bounds.removeFromLeft(100);
    
    filterLabel.setBounds(col1.removeFromTop(labelHeight));
    filterOnButton.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    
    freqLabel.setBounds(col1.removeFromTop(labelHeight));
    filterFreqKnob.setBounds(col1.removeFromTop(knobSize));
    col1.removeFromTop(spacing);
    
    filterTypeLabel.setBounds(col1.removeFromTop(labelHeight));
    filterTypeSelector.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    
    // Smooth knob in first column
    smoothLabel.setBounds(col1.removeFromTop(labelHeight));
    smoothKnob.setBounds(col1.removeFromTop(knobSize));
    
    bounds.removeFromLeft(spacing);
    
    // Column 2: Mode, Decay, Const, Color
    auto col2 = bounds.removeFromLeft(100);
    
    modeLabel.setBounds(col2.removeFromTop(labelHeight));
    modeSelector.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    
    decayLabel.setBounds(col2.removeFromTop(labelHeight));
    decayKnob.setBounds(col2.removeFromTop(knobSize));
    col2.removeFromTop(spacing);
    
    constLabel.setBounds(col2.removeFromTop(labelHeight));
    constButton.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    
    colorLabel.setBounds(col2.removeFromTop(labelHeight));
    colorKnob.setBounds(col2.removeFromTop(knobSize));
    
    bounds.removeFromLeft(spacing);
    
    // Column 9: Chorus/LFO controls (rightmost, NEW)
    auto col9 = bounds.removeFromRight(100);
    
    chorusLabel.setBounds(col9.removeFromTop(labelHeight));
    chorusKnob.setBounds(col9.removeFromTop(knobSize));
    col9.removeFromTop(spacing);
    
    lfoRateLabel.setBounds(col9.removeFromTop(labelHeight));
    lfoRateKnob.setBounds(col9.removeFromTop(knobSize));
    col9.removeFromTop(spacing);
    
    lfoDepthLabel.setBounds(col9.removeFromTop(labelHeight));
    lfoDepthKnob.setBounds(col9.removeFromTop(knobSize));
    
    bounds.removeFromRight(spacing);
    
    // Column 8: Width, Gain, Dry/Wet, Wet Only
    auto col8 = bounds.removeFromRight(100);
    
    widthLabel.setBounds(col8.removeFromTop(labelHeight));
    widthKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    
    gainLabel.setBounds(col8.removeFromTop(labelHeight));
    gainKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    
    dryWetLabel.setBounds(col8.removeFromTop(labelHeight));
    dryWetKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    
    wetOnlyLabel.setBounds(col8.removeFromTop(labelHeight));
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
