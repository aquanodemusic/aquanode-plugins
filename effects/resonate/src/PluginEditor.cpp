/*
  ==============================================================================

    Ableton-Style Resonator UI Implementation

    Layout (logical coordinates -- the whole thing is scaled by a transform):
      Col 1  - Filter (On, Frequency, Type), Smooth, Exp Decay, MIDI In
      Col 2  - Mode, Decay, Const, Color, Per Res, Save/Load Preset
      Col 3-7- Resonators I-V  (Decay + Const + Color + Pan expand on Per Res)
      Col 8  - Width, Gain, Dry/Wet, Wet Only
      Col 9  - Chorus, LFO Rate, LFO Depth, DC Center

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Shared helper: dim a control that the engine is currently ignoring.
static void setControlActive(juce::Component& c, bool active)
{
    c.setEnabled(active);
    c.setAlpha(active ? 1.0f : 0.38f);
}

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

    addAndMakeVisible(midiDisplay);

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

    // Per-res decay/color/const/pan (created but hidden by default)
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

    // Pan: -100 hard left, 0 centre, +100 hard right
    addChildComponent(perResPanKnob);
    addChildComponent(perResPanLabel);
    perResPanKnob.setRange(-100.0, 100.0, 0.1);
    perResPanKnob.addListener(this);
    perResPanKnob.textFromValueFunction = [](double v) -> juce::String
    {
        if (std::abs(v) < 0.05) return "C";
        return (v < 0.0 ? "L" : "R") + juce::String(std::abs(v), 0);
    };

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
    perResPanAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, id + "_pan", perResPanKnob);

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
    perResPanKnob.setVisible(active);
    perResPanLabel.setVisible(active);
    resized();
}

void ResonateChannel::refreshMidiDisplay()
{
    const int note = processor.getMidiNoteForResonator(resonatorIndex);
    midiDisplay.setNote(note);

    // When MIDI is driving this resonator its pitch knobs are along for the
    // ride rather than in charge, so dim them the same way the globals dim.
    const bool knobsInCharge = (note < 0);
    if (resonatorIndex == 0)
    {
        setControlActive(noteKnob, knobsInCharge);
        setControlActive(noteDisplay, knobsInCharge);
    }
    else
    {
        setControlActive(pitchKnob, knobsInCharge);
        setControlActive(pitchLabel, knobsInCharge);
    }
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

    // MIDI readout strip (invisible when no note is assigned)
    midiDisplay.setBounds(bounds.removeFromTop(14));
    bounds.removeFromTop(2);

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

    // Per-res: Decay + Const + Color + Pan (only shown when perResActive)
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
        bounds.removeFromTop(spacing);
        perResPanLabel.setBounds(bounds.removeFromTop(labelH));
        perResPanKnob.setBounds(bounds.removeFromTop(knobSize));
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

    // Everything goes inside the scaled content component
    addAndMakeVisible(content);
    content.setInterceptsMouseClicks(false, true);
    content.addAndMakeVisible(branding);

    // ── Column 1: Filter + Smooth + Exp Decay + MIDI ──────────────────────────
    filterOnButton.setButtonText("On");
    content.addAndMakeVisible(filterOnButton);
    content.addAndMakeVisible(filterLabel);
    content.addAndMakeVisible(filterFreqKnob);
    content.addAndMakeVisible(freqLabel);
    filterFreqKnob.setRange(20.0, 20000.0, 1.0);
    filterFreqKnob.addListener(this);
    content.addAndMakeVisible(filterTypeSelector);
    filterTypeSelector.addItem("Lowpass",  1);
    filterTypeSelector.addItem("Highpass", 2);
    filterTypeSelector.addItem("Bandpass", 3);
    filterTypeSelector.addItem("Notch",    4);
    content.addAndMakeVisible(filterTypeLabel);
    content.addAndMakeVisible(smoothKnob);
    content.addAndMakeVisible(smoothLabel);
    smoothKnob.setRange(0.0, 100.0, 0.1);
    smoothKnob.addListener(this);

    midiEnableButton.setButtonText("MIDI");
    content.addAndMakeVisible(midiEnableButton);
    content.addAndMakeVisible(midiLabel);

    // ── Column 2: Mode / Decay / Const / Color / Per Res / Presets ────────────
    content.addAndMakeVisible(modeSelector);
    content.addAndMakeVisible(modeLabel);
    modeSelector.addItem("A", 1);
    modeSelector.addItem("B", 2);

    content.addAndMakeVisible(decayKnob);
    content.addAndMakeVisible(decayLabel);
    decayKnob.setRange(0.0, 100.0, 0.1);
    decayKnob.addListener(this);

    constButton.setButtonText("Const");
    content.addAndMakeVisible(constButton);
    content.addAndMakeVisible(constLabel);

    content.addAndMakeVisible(colorKnob);
    content.addAndMakeVisible(colorLabel);
    colorKnob.setRange(0.0, 100.0, 0.1);
    colorKnob.addListener(this);

    centerButton.setButtonText("DC Center");
    content.addAndMakeVisible(centerButton);
    content.addAndMakeVisible(centerLabel);

    expDecayButton.setButtonText("Exp Decay");
    content.addAndMakeVisible(expDecayButton);
    content.addAndMakeVisible(expDecayLabel);

    perResButton.setButtonText("Per Res");
    content.addAndMakeVisible(perResButton);
    content.addAndMakeVisible(perResLabel);

    // Preset save / load
    content.addAndMakeVisible(presetLabel);
    savePresetButton.setButtonText("Save");
    loadPresetButton.setButtonText("Load");
    content.addAndMakeVisible(savePresetButton);
    content.addAndMakeVisible(loadPresetButton);
    savePresetButton.onClick = [this]() { showSavePresetDialog(); };
    loadPresetButton.onClick = [this]() { showLoadPresetDialog(); };

    // ── Resonator channel strips ──────────────────────────────────────────────
    for (int i = 0; i < 5; ++i)
    {
        channels[i] = std::make_unique<ResonateChannel>(audioProcessor, i);
        content.addAndMakeVisible(channels[i].get());
    }

    // ── Column 8: Width / Gain / Dry-Wet / Wet Only ───────────────────────────
    content.addAndMakeVisible(widthKnob);
    content.addAndMakeVisible(gainKnob);
    content.addAndMakeVisible(dryWetKnob);
    content.addAndMakeVisible(widthLabel);
    content.addAndMakeVisible(gainLabel);
    content.addAndMakeVisible(dryWetLabel);
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
    content.addAndMakeVisible(wetOnlyButton);
    content.addAndMakeVisible(wetOnlyLabel);

    // ── Column 9: Chorus / LFO ───────────────────────────────────────────────
    content.addAndMakeVisible(chorusKnob);
    content.addAndMakeVisible(chorusLabel);
    chorusKnob.setRange(0.0, 100.0, 0.1);
    chorusKnob.setTextValueSuffix(" %");
    chorusKnob.addListener(this);
    content.addAndMakeVisible(lfoRateKnob);
    content.addAndMakeVisible(lfoRateLabel);
    lfoRateKnob.setRange(0.1, 5.0, 0.01);
    lfoRateKnob.setTextValueSuffix(" Hz");
    lfoRateKnob.addListener(this);
    content.addAndMakeVisible(lfoDepthKnob);
    content.addAndMakeVisible(lfoDepthLabel);
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
    perResAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "per_res_mode", perResButton);
    expDecayAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "exp_decay", expDecayButton);
    midiEnableAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        params, "midi_enabled", midiEnableButton);

    perResButton.onClick = [this]()
    {
        applyPerResMode(perResButton.getToggleState());
    };

    // ── Resizing ──────────────────────────────────────────────────────────────
    currentPerResMode = params.getRawParameterValue("per_res_mode")->load() > 0.5f;
    for (int i = 0; i < 5; ++i)
        channels[i]->setPerResMode(currentPerResMode);
    updateGlobalEnablement();

    uiScale = static_cast<double>(params.state.getProperty("ui_scale", 1.0));
    uiScale = juce::jlimit(MIN_SCALE, MAX_SCALE, uiScale);

    setResizable(true, true);
    setConstrainer(&constrainer);
    updateEditorSize();

    startTimer(100);
}

ResonateAudioProcessorEditor::~ResonateAudioProcessorEditor()
{
    stopTimer();
    setConstrainer(nullptr);
}

//==============================================================================
void ResonateAudioProcessorEditor::updateEditorSize()
{
    const int lh = logicalHeight();

    constrainer.setFixedAspectRatio(static_cast<double>(LOGICAL_WIDTH)
                                    / static_cast<double>(lh));
    constrainer.setSizeLimits(
        juce::roundToInt(LOGICAL_WIDTH * MIN_SCALE), juce::roundToInt(lh * MIN_SCALE),
        juce::roundToInt(LOGICAL_WIDTH * MAX_SCALE), juce::roundToInt(lh * MAX_SCALE));

    setSize(juce::roundToInt(LOGICAL_WIDTH * uiScale),
            juce::roundToInt(lh * uiScale));
}

void ResonateAudioProcessorEditor::applyPerResMode(bool active)
{
    currentPerResMode = active;
    for (int i = 0; i < 5; ++i)
        channels[i]->setPerResMode(active);

    updateGlobalEnablement();
    updateEditorSize();
}

//==============================================================================
// Grey out the globals the engine stops reading in Per Res mode.
//
// In updateParameters() only three values are swapped for their local
// equivalents -- effDecay, effColor and effConst. Everything else (Mode,
// Filter, Smooth, Chorus/LFO, Width, Gain, Dry/Wet, DC Center, Exp Decay)
// stays global and stays live.
void ResonateAudioProcessorEditor::updateGlobalEnablement()
{
    const bool globalsLive = !currentPerResMode;

    setControlActive(decayKnob,   globalsLive);
    setControlActive(decayLabel,  globalsLive);
    setControlActive(constButton, globalsLive);
    setControlActive(constLabel,  globalsLive);
    setControlActive(colorKnob,   globalsLive);
    setControlActive(colorLabel,  globalsLive);
}

//==============================================================================
// Inline feedback under the preset buttons, reverted by the timer a few
// seconds later. Avoids modal dialogs (and their JUCE-version differences).
void ResonateAudioProcessorEditor::flashPresetMessage(const juce::String& msg)
{
    presetLabel.setText(msg, juce::dontSendNotification);
    presetMessageTicks = 30;   // timer runs at 100 ms
}

//==============================================================================
void ResonateAudioProcessorEditor::showSavePresetDialog()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Resonate preset",
        ResonateAudioProcessor::getDefaultPresetDirectory(),
        "*.xml");

    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return;

        if (!file.hasFileExtension("xml"))
            file = file.withFileExtension("xml");

        flashPresetMessage(audioProcessor.savePresetToFile(file) ? "Saved" : "Save failed");
    });
}

void ResonateAudioProcessorEditor::showLoadPresetDialog()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Resonate preset",
        ResonateAudioProcessor::getDefaultPresetDirectory(),
        "*.xml");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File{})
            return;

        if (!audioProcessor.loadPresetFromFile(file))
        {
            flashPresetMessage("Load failed");
            return;
        }

        flashPresetMessage("Loaded");

        // Re-sync the parts of the UI that aren't plain attachments.
        const bool perResNow = audioProcessor.getParameters()
                                   .getRawParameterValue("per_res_mode")->load() > 0.5f;
        applyPerResMode(perResNow);
    });
}

//==============================================================================
void ResonateAudioProcessorEditor::timerCallback()
{
    // Sync mode combo
    int modeIndex = static_cast<int>(
        audioProcessor.getParameters().getRawParameterValue("mode")->load());
    modeSelector.setSelectedId(modeIndex + 1, juce::dontSendNotification);

    // Sync per-res mode (host automation, preset load, undo, ...)
    bool perResNow = audioProcessor.getParameters()
                         .getRawParameterValue("per_res_mode")->load() > 0.5f;
    if (perResNow != currentPerResMode)
        applyPerResMode(perResNow);

    // Reflect incoming MIDI note assignments
    for (int i = 0; i < 5; ++i)
        channels[i]->refreshMidiDisplay();

    // Revert the preset status message
    if (presetMessageTicks > 0 && --presetMessageTicks == 0)
        presetLabel.setText("Preset", juce::dontSendNotification);
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
}

//==============================================================================
void ResonateAudioProcessorEditor::resized()
{
    const int lh = logicalHeight();

    // Scale factor derived from the window width; the constrainer keeps the
    // aspect ratio fixed so height follows automatically.
    uiScale = juce::jlimit(MIN_SCALE, MAX_SCALE,
                           static_cast<double>(getWidth()) / static_cast<double>(LOGICAL_WIDTH));

    content.setTransform(juce::AffineTransform::scale(static_cast<float>(uiScale)));
    content.setBounds(0, 0, LOGICAL_WIDTH, lh);

    layoutContent();

    // Remember the size across sessions (rides along in the APVTS state)
    audioProcessor.getParameters().state.setProperty("ui_scale", uiScale, nullptr);
}

//==============================================================================
// Laid out in LOGICAL coordinates -- unchanged pixel maths, just scaled.
void ResonateAudioProcessorEditor::layoutContent()
{
    auto bounds  = content.getLocalBounds().reduced(10);
    int knobSize = 90;
    int labelH   = 14;
    int spacing  = 8;

    // ── Column 1: Filter + Smooth + Exp Decay + MIDI ─────────────────────────
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
    expDecayLabel.setBounds(col1.removeFromTop(labelH));
    expDecayButton.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    midiLabel.setBounds(col1.removeFromTop(labelH));
    midiEnableButton.setBounds(col1.removeFromTop(24));
    bounds.removeFromLeft(spacing);

    // ── Column 2: Mode / Decay / Const / Color / Per Res / Presets ────────────
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
    perResLabel.setBounds(col2.removeFromTop(labelH));
    perResButton.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    presetLabel.setBounds(col2.removeFromTop(labelH));
    {
        auto presetRow = col2.removeFromTop(24);
        savePresetButton.setBounds(presetRow.removeFromLeft(48));
        presetRow.removeFromLeft(4);
        loadPresetButton.setBounds(presetRow.removeFromLeft(48));
    }
    bounds.removeFromLeft(spacing);

    // ── Column 9 (right-most): Chorus / LFO / DC Center ──────────────────────
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

    // Branding, bottom-left of the content
    branding.setBounds(28, content.getHeight() - 58, 110, 44);
}
