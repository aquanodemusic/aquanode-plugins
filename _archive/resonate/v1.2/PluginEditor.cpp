/*
  ==============================================================================

    Ableton-Style Resonator UI Implementation

    Layout:
      Col 1  – Filter (On, Frequency, Type), Smooth
      Col 2  – Mode, Decay, Const, Color, Center (NEW), Per Res (NEW)
      Col 3-7– Resonators I-V  (per-res Decay+Color expand when Per Res ON)
      Col 8  – Width, Gain, Dry/Wet, Wet Only
      Col 9  – Chorus, LFO Rate, LFO Depth

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// ResonateChannel

ResonateChannel::ResonateChannel(ResonateAudioProcessor& proc, int index)
    : resonatorIndex(index), processor(proc)
{
    numberLabel.setText(getRomanNumeral(index + 1), juce::dontSendNotification);
    numberLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    addAndMakeVisible(numberLabel);

    enableButton.setButtonText("On");
    addAndMakeVisible(enableButton);

    if (index == 0)
    {
        addAndMakeVisible(noteKnob);
        addAndMakeVisible(noteDisplay);
        noteDisplay.setIsNoteDisplay(true);
        noteKnob.setRange(0.0, 127.0, 1.0);
        noteKnob.addListener(this);
        noteKnob.textFromValueFunction = [](double v) {
            return midiNoteToNoteName(static_cast<int>(v));
        };
    }
    else
    {
        addAndMakeVisible(pitchKnob);
        addAndMakeVisible(pitchLabel);
        pitchKnob.setRange(-24, 24, 1);
        pitchKnob.setTextValueSuffix(" st");
        pitchKnob.addListener(this);
    }

    addAndMakeVisible(fineKnob);
    addAndMakeVisible(fineLabel);
    fineKnob.setRange(-50.0, 50.0, 0.1);
    fineKnob.setTextValueSuffix(" c");
    fineKnob.addListener(this);

    addAndMakeVisible(gainKnob);
    addAndMakeVisible(gainLabel);
    gainKnob.setRange(-48.0, 12.0, 0.1);
    gainKnob.setTextValueSuffix(" dB");
    gainKnob.addListener(this);

    // Per-res decay/color/const (created but hidden by default)
    addChildComponent(perResDecayKnob);
    addChildComponent(perResDecayLabel);
    perResDecayKnob.setRange(0.0, 100.0, 0.1);
    perResDecayKnob.addListener(this);

    addChildComponent(perResColorKnob);
    addChildComponent(perResColorLabel);
    perResColorKnob.setRange(0.0, 100.0, 0.1);
    perResColorKnob.addListener(this);

    addChildComponent(perResConstButton);
    addChildComponent(perResConstLabel);
    perResConstButton.setButtonText("Const");

    // Attachments
    juce::String id = "res" + juce::String(index + 1);
    auto& params = proc.getParameters();

    if (index == 0)
        noteAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, id + "_note", noteKnob);
    else
        pitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, id + "_pitch", pitchKnob);

    fineAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, id + "_fine", fineKnob);
    gainAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, id + "_gain", gainKnob);
    enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, id + "_enabled", enableButton);
    perResDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, id + "_decay", perResDecayKnob);
    perResColorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, id + "_color", perResColorKnob);
    perResConstAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, id + "_const", perResConstButton);

    updateDisplays();
}

void ResonateChannel::setPerResMode(bool active)
{
    perResActive = active;
    perResDecayKnob.setVisible(active);
    perResDecayLabel.setVisible(active);
    perResColorKnob.setVisible(active);
    perResColorLabel.setVisible(active);
    perResConstButton.setVisible(active);
    perResConstLabel.setVisible(active);
    resized();
}

void ResonateChannel::sliderValueChanged(juce::Slider*) { updateDisplays(); }

void ResonateChannel::updateDisplays()
{
    if (resonatorIndex == 0)
        noteDisplay.setValue(midiNoteToNoteName(static_cast<int>(noteKnob.getValue())));
}

juce::String ResonateChannel::getRomanNumeral(int num)
{
    switch (num)
    {
        case 1: return "I";    case 2: return "II";
        case 3: return "III";  case 4: return "IV";
        case 5: return "V";    default: return juce::String(num);
    }
}

void ResonateChannel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xffc8d0d8));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(juce::Colour(0xffa0a8b0));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
}

void ResonateChannel::resized()
{
    auto bounds    = getLocalBounds().reduced(4);
    int  knobSize  = 90;
    int  labelH    = 14;
    int  spacing   = 4;

    // Header: Roman numeral + On button
    auto topSection = bounds.removeFromTop(22);
    numberLabel.setBounds(topSection.removeFromLeft(35));
    topSection.removeFromLeft(2);
    enableButton.setBounds(topSection.removeFromLeft(50));
    bounds.removeFromTop(spacing);

    // Note / Pitch block
    if (resonatorIndex == 0)
    {
        noteDisplay.setBounds(bounds.removeFromTop(14));
        noteKnob.setBounds(bounds.removeFromTop(knobSize));
    }
    else
    {
        pitchLabel.setBounds(bounds.removeFromTop(labelH));
        pitchKnob.setBounds(bounds.removeFromTop(knobSize));
    }
    bounds.removeFromTop(spacing);

    // Fine
    fineLabel.setBounds(bounds.removeFromTop(labelH));
    fineKnob.setBounds(bounds.removeFromTop(knobSize));
    bounds.removeFromTop(spacing);

    // Gain
    gainLabel.setBounds(bounds.removeFromTop(labelH));
    gainKnob.setBounds(bounds.removeFromTop(knobSize));

    // Per-res: Decay + Const + Color (only shown when perResActive)
    if (perResActive)
    {
        bounds.removeFromTop(spacing);
        perResDecayLabel.setBounds(bounds.removeFromTop(labelH));
        perResDecayKnob.setBounds(bounds.removeFromTop(knobSize));
        bounds.removeFromTop(spacing);
        perResConstLabel.setBounds(bounds.removeFromTop(labelH));
        perResConstButton.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(spacing);
        perResColorLabel.setBounds(bounds.removeFromTop(labelH));
        perResColorKnob.setBounds(bounds.removeFromTop(knobSize));
    }
}

//==============================================================================
// Main Editor

ResonateAudioProcessorEditor::ResonateAudioProcessorEditor(ResonateAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // ── Color scheme ─────────────────────────────────────────────────────────
    juce::Colour cyan     = juce::Colour(0xff00d4ff);
    juce::Colour darkGray = juce::Colour(0xff505860);
    juce::Colour yellow   = juce::Colour(0xffffff00);

    getLookAndFeel().setColour(juce::Slider::thumbColourId,              cyan);
    getLookAndFeel().setColour(juce::Slider::rotarySliderFillColourId,   cyan);
    getLookAndFeel().setColour(juce::Slider::rotarySliderOutlineColourId, darkGray);
    getLookAndFeel().setColour(juce::Slider::textBoxTextColourId,        juce::Colours::black);
    getLookAndFeel().setColour(juce::Slider::textBoxOutlineColourId,     juce::Colours::grey);
    getLookAndFeel().setColour(juce::ToggleButton::tickColourId,         cyan);
    getLookAndFeel().setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff808080));
    getLookAndFeel().setColour(juce::Label::textColourId,                juce::Colours::black);
    getLookAndFeel().setColour(juce::ComboBox::backgroundColourId,       yellow);
    getLookAndFeel().setColour(juce::ComboBox::outlineColourId,          darkGray);
    getLookAndFeel().setColour(juce::ComboBox::textColourId,             juce::Colours::black);

    // ── Column 1: Filter + Smooth ─────────────────────────────────────────────
    filterOnButton.setButtonText("On");
    addAndMakeVisible(filterOnButton);
    addAndMakeVisible(filterLabel);
    addAndMakeVisible(filterFreqKnob);
    addAndMakeVisible(freqLabel);
    filterFreqKnob.setRange(20.0, 20000.0, 1.0);
    filterFreqKnob.addListener(this);
    addAndMakeVisible(filterTypeSelector);
    filterTypeSelector.addItem("Lowpass",  1);
    filterTypeSelector.addItem("Highpass", 2);
    filterTypeSelector.addItem("Bandpass", 3);
    filterTypeSelector.addItem("Notch",    4);
    addAndMakeVisible(filterTypeLabel);
    addAndMakeVisible(smoothKnob);
    addAndMakeVisible(smoothLabel);
    smoothKnob.setRange(0.0, 100.0, 0.1);
    smoothKnob.addListener(this);

    // ── Column 2: Mode / Decay / Const / Color / Center / Per Res ────────────
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

    // Center toggle
    centerButton.setButtonText("DC Center");
    addAndMakeVisible(centerButton);
    addAndMakeVisible(centerLabel);

    // Exp decay toggle
    expDecayButton.setButtonText("Exp Decay");
    addAndMakeVisible(expDecayButton);
    addAndMakeVisible(expDecayLabel);

    // Per Res toggle
    perResButton.setButtonText("Per Res");
    addAndMakeVisible(perResButton);
    addAndMakeVisible(perResLabel);

    // ── Resonator channel strips ──────────────────────────────────────────────
    for (int i = 0; i < 5; ++i)
    {
        channels[i] = std::make_unique<ResonateChannel>(audioProcessor, i);
        addAndMakeVisible(channels[i].get());
    }

    // ── Column 8: Width / Gain / Dry-Wet / Wet Only ───────────────────────────
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

    // ── Column 9: Chorus / LFO ───────────────────────────────────────────────
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

    // ── APVTS Attachments ─────────────────────────────────────────────────────
    auto& params = audioProcessor.getParameters();

    filterAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "filter_enabled", filterOnButton);
    filterFreqAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "filter_freq", filterFreqKnob);
    filterTypeAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        params, "filter_type", filterTypeSelector);
    modeAttachment        = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        params, "mode", modeSelector);
    decayAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "decay", decayKnob);
    constAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "const_mode", constButton);
    colorAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "color", colorKnob);
    centerAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "center_mode", centerButton);
    smoothAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "smooth", smoothKnob);
    chorusAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "chorus", chorusKnob);
    widthAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "width", widthKnob);
    gainAttachment        = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "gain", gainKnob);
    dryWetAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "drywet", dryWetKnob);
    wetOnlyAttachment     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "wet_only", wetOnlyButton);
    lfoRateAttachment     = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "lfo_rate", lfoRateKnob);
    lfoDepthAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "lfo_depth", lfoDepthKnob);

    // Per Res attachment (NEW) — also wire up the onClick to resize the plugin
    perResAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "per_res_mode", perResButton);
    expDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "exp_decay", expDecayButton);

    perResButton.onClick = [this]()
    {
        bool active = perResButton.getToggleState();
        applyPerResMode(active);
    };

    startTimer(100);
    setSize(1050, BASE_HEIGHT);
}

ResonateAudioProcessorEditor::~ResonateAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void ResonateAudioProcessorEditor::applyPerResMode(bool active)
{
    currentPerResMode = active;
    for (int i = 0; i < 5; ++i)
        channels[i]->setPerResMode(active);

    int newHeight = active ? (BASE_HEIGHT + PER_RES_EXTRA) : BASE_HEIGHT;
    setSize(getWidth(), newHeight);
}

//==============================================================================
void ResonateAudioProcessorEditor::timerCallback()
{
    // Sync mode combo
    int modeIndex = static_cast<int>(
        audioProcessor.getParameters().getRawParameterValue("mode")->load());
    modeSelector.setSelectedId(modeIndex + 1, juce::dontSendNotification);

    // Sync per-res mode (in case it was set programmatically / preset load)
    bool perResNow = audioProcessor.getParameters()
                         .getRawParameterValue("per_res_mode")->load() > 0.5f;
    if (perResNow != currentPerResMode)
        applyPerResMode(perResNow);
}

void ResonateAudioProcessorEditor::sliderValueChanged(juce::Slider*) {}

//==============================================================================
void ResonateAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bgGradient(
        juce::Colour(0xffe0e8f0), 0.0f, 0.0f,
        juce::Colour(0xffc8d0d8), 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGradient);
    g.fillAll();

    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("aqua", 28, getHeight() - 55, 60, 20, juce::Justification::left);
    g.drawText("node", 28, getHeight() - 35, 60, 20, juce::Justification::left);
    g.drawText("reso", 70, getHeight() - 55, 60, 20, juce::Justification::left);
    g.drawText("nate", 70, getHeight() - 35, 60, 20, juce::Justification::left);
}

//==============================================================================
void ResonateAudioProcessorEditor::resized()
{
    auto bounds  = getLocalBounds().reduced(10);
    int knobSize = 90;
    int labelH   = 14;
    int spacing  = 8;

    // ── Column 1: Filter + Smooth + DC Center ────────────────────────────────
    auto col1 = bounds.removeFromLeft(100);
    filterLabel.setBounds(col1.removeFromTop(labelH));
    filterOnButton.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    freqLabel.setBounds(col1.removeFromTop(labelH));
    filterFreqKnob.setBounds(col1.removeFromTop(knobSize));
    col1.removeFromTop(spacing);
    filterTypeLabel.setBounds(col1.removeFromTop(labelH));
    filterTypeSelector.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    smoothLabel.setBounds(col1.removeFromTop(labelH));
    smoothKnob.setBounds(col1.removeFromTop(knobSize));
    col1.removeFromTop(spacing);
    // Exp Decay sits at same vertical height as Per Res in col2
    expDecayLabel.setBounds(col1.removeFromTop(labelH));
    expDecayButton.setBounds(col1.removeFromTop(24));
    bounds.removeFromLeft(spacing);

    // ── Column 2: Mode / Decay / Const / Color / Per Res ─────────────────────
    auto col2 = bounds.removeFromLeft(100);
    modeLabel.setBounds(col2.removeFromTop(labelH));
    modeSelector.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    decayLabel.setBounds(col2.removeFromTop(labelH));
    decayKnob.setBounds(col2.removeFromTop(knobSize));
    col2.removeFromTop(spacing);
    constLabel.setBounds(col2.removeFromTop(labelH));
    constButton.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    colorLabel.setBounds(col2.removeFromTop(labelH));
    colorKnob.setBounds(col2.removeFromTop(knobSize));
    col2.removeFromTop(spacing);
    // Per Res now sits where Center used to be — same row as DC Center in col1
    perResLabel.setBounds(col2.removeFromTop(labelH));
    perResButton.setBounds(col2.removeFromTop(24));
    bounds.removeFromLeft(spacing);

    // ── Column 9 (right-most): Chorus / LFO ──────────────────────────────────
    auto col9 = bounds.removeFromRight(100);
    chorusLabel.setBounds(col9.removeFromTop(labelH));
    chorusKnob.setBounds(col9.removeFromTop(knobSize));
    col9.removeFromTop(spacing);
    lfoRateLabel.setBounds(col9.removeFromTop(labelH));
    lfoRateKnob.setBounds(col9.removeFromTop(knobSize));
    col9.removeFromTop(spacing);
    lfoDepthLabel.setBounds(col9.removeFromTop(labelH));
    lfoDepthKnob.setBounds(col9.removeFromTop(knobSize));
    col9.removeFromTop(spacing);
    centerLabel.setBounds(col9.removeFromTop(labelH));
    centerButton.setBounds(col9.removeFromTop(24));

    bounds.removeFromRight(spacing);

    // ── Column 8: Width / Gain / Dry-Wet / Wet Only ───────────────────────────
    auto col8 = bounds.removeFromRight(100);
    widthLabel.setBounds(col8.removeFromTop(labelH));
    widthKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    gainLabel.setBounds(col8.removeFromTop(labelH));
    gainKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    dryWetLabel.setBounds(col8.removeFromTop(labelH));
    dryWetKnob.setBounds(col8.removeFromTop(knobSize));
    col8.removeFromTop(spacing);
    wetOnlyLabel.setBounds(col8.removeFromTop(labelH));
    wetOnlyButton.setBounds(col8.removeFromTop(24));
    bounds.removeFromRight(spacing);

    // ── Columns 3-7: Resonator channel strips ────────────────────────────────
    int channelWidth = bounds.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
    {
        auto channelBounds = bounds.removeFromLeft(channelWidth).reduced(3);
        channels[i]->setBounds(channelBounds);
    }
}
