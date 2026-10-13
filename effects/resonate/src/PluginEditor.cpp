/*
  ==============================================================================

    Ableton-Style Resonator UI Implementation

    Layout (logical coordinates -- the whole thing is scaled by a transform):
      Col 1  - Filter (On/Off, Type, Frequency), [Smooth], MIDI In, logo, Engine
      Col 2  - Res Mode, Shape Mode, Color, Decay, Const, Save/Load Preset
      Col 3-9- Resonators I-VII (Decay + Const + Color + Pan expand on Per Res)
      Col 8  - Width, Gain, Dry/Wet, Wet Only, [Exp Decay]
      Col 9  - [Chorus, LFO Rate, LFO Depth, DC Center, 2x OS]
      [..] = Crisp engine only; the window grows to fit when Crisp is selected.
      All engine-specific controls are re-bound to the selected engine's own
      parameter set when the engine changes.

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

    bindEngine({});

    updateDisplays();
}

void ResonateChannel::bindEngine(const juce::String& prefix)
{
    // Drop the old attachments first (each one must go before a new one on
    // the same control is created).
    noteAttachment.reset(); pitchAttachment.reset(); fineAttachment.reset(); gainAttachment.reset();
    enableAttachment.reset(); perResDecayAttachment.reset(); perResColorAttachment.reset();
    perResConstAttachment.reset(); perResPanAttachment.reset();

    const juce::String id = prefix + "res" + juce::String(resonatorIndex + 1);
    auto& params = processor.getParameters();
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    if (resonatorIndex == 0)
        noteAttachment = std::make_unique<SA>(params, prefix + "res1_note", noteKnob);
    else
        pitchAttachment = std::make_unique<SA>(params, id + "_pitch", pitchKnob);

    fineAttachment        = std::make_unique<SA>(params, id + "_fine", fineKnob);
    gainAttachment        = std::make_unique<SA>(params, id + "_gain", gainKnob);
    enableAttachment      = std::make_unique<BA>(params, id + "_enabled", enableButton);
    perResDecayAttachment = std::make_unique<SA>(params, id + "_decay", perResDecayKnob);
    perResColorAttachment = std::make_unique<SA>(params, id + "_color", perResColorKnob);
    perResConstAttachment = std::make_unique<BA>(params, id + "_const", perResConstButton);
    perResPanAttachment   = std::make_unique<SA>(params, id + "_pan", perResPanKnob);

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
    case 5: return "V";    case 6: return "VI";
    case 7: return "VII";  default: return juce::String(num);
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
    auto bounds = getLocalBounds().reduced(4);
    int  knobSize = KNOB_SIZE;
    int  labelH = LABEL_H;
    int  spacing = ROW_SPACING;

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

    // Per-res: Decay + Const + Color + Pan (only shown when perResActive).
    // Smaller knob size and tighter spacing than the main strip above, so
    // switching Per Res on doesn't blow the window height out.
    if (perResActive)
    {
        int prKnobSize = PER_RES_KNOB_SIZE;
        int prSpacing = PER_RES_ROW_SPACING;

        bounds.removeFromTop(prSpacing);
        perResDecayLabel.setBounds(bounds.removeFromTop(labelH));
        perResDecayKnob.setBounds(bounds.removeFromTop(prKnobSize));
        bounds.removeFromTop(prSpacing);
        perResConstButton.setBounds(bounds.removeFromTop(BUTTON_H));
        bounds.removeFromTop(prSpacing);
        perResColorLabel.setBounds(bounds.removeFromTop(labelH));
        perResColorKnob.setBounds(bounds.removeFromTop(prKnobSize));
        bounds.removeFromTop(prSpacing);
        perResPanLabel.setBounds(bounds.removeFromTop(labelH));
        perResPanKnob.setBounds(bounds.removeFromTop(prKnobSize));
    }
}

//==============================================================================
// Main Editor

ResonateAudioProcessorEditor::ResonateAudioProcessorEditor(ResonateAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // ── Color scheme ─────────────────────────────────────────────────────────
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
    getLookAndFeel().setColour(juce::ToggleButton::textColourId, juce::Colours::black);
    getLookAndFeel().setColour(juce::Label::textColourId, juce::Colours::black);
    getLookAndFeel().setColour(juce::ComboBox::backgroundColourId, yellow);
    getLookAndFeel().setColour(juce::ComboBox::outlineColourId, darkGray);
    getLookAndFeel().setColour(juce::ComboBox::textColourId, juce::Colours::black);

    // Everything goes inside the scaled content component
    addAndMakeVisible(content);
    content.setInterceptsMouseClicks(false, true);
    content.addAndMakeVisible(branding);

    // ── Column 1: Filter + Smooth + MIDI ──────────────────────────────────────
    content.addAndMakeVisible(filterOnSelector);
    content.addAndMakeVisible(filterLabel);
    filterOnSelector.addItem("Off", 1);
    filterOnSelector.addItem("On", 2);
    content.addAndMakeVisible(filterFreqKnob);
    content.addAndMakeVisible(freqLabel);
    filterFreqKnob.setRange(20.0, 20000.0, 1.0);
    filterFreqKnob.addListener(this);
    content.addAndMakeVisible(filterTypeSelector);
    filterTypeSelector.addItem("Lowpass", 1);
    filterTypeSelector.addItem("Highpass", 2);
    filterTypeSelector.addItem("Bandpass", 3);
    filterTypeSelector.addItem("Notch", 4);
    content.addAndMakeVisible(filterTypeLabel);

    midiEnableButton.setButtonText("MIDI In");
    content.addAndMakeVisible(midiEnableButton);

    // Engine selector, under the branding: Modelled (default) / Native / Crisp / Dark
    content.addAndMakeVisible(engineSelector);
    content.addAndMakeVisible(engineLabel);
    engineSelector.addItem("Modelled", 1);
    engineSelector.addItem("Native", 2);

    // ── Column 2: Res Mode / Shape Mode / Color / Decay / Const / Presets ──
    content.addAndMakeVisible(modeSelector);
    content.addAndMakeVisible(modeLabel);
    modeSelector.addItem("Full", 1);
    modeSelector.addItem("Odd", 2);

    content.addAndMakeVisible(colorKnob);
    content.addAndMakeVisible(colorLabel);
    colorKnob.setRange(0.0, 100.0, 0.1);
    colorKnob.addListener(this);

    // Replaces the old Per Res toggle button with a yellow combobox,
    // matching the Filter/Mode/Type style: Global=id1 -> false, Individual=id2 -> true
    content.addAndMakeVisible(perResSelector);
    content.addAndMakeVisible(perResLabel);
    perResSelector.addItem("Global", 1);
    perResSelector.addItem("Individual", 2);

    content.addAndMakeVisible(decayKnob);
    content.addAndMakeVisible(decayLabel);
    decayKnob.setRange(0.0, 100.0, 0.1);
    decayKnob.addListener(this);

    constButton.setButtonText("Const");
    content.addAndMakeVisible(constButton);

    // Preset save / load
    content.addAndMakeVisible(presetLabel);
    savePresetButton.setButtonText("Save");
    loadPresetButton.setButtonText("Load");
    content.addAndMakeVisible(savePresetButton);
    content.addAndMakeVisible(loadPresetButton);
    savePresetButton.onClick = [this]() { showSavePresetDialog(); };
    loadPresetButton.onClick = [this]() { showLoadPresetDialog(); };

    // ── Resonator channel strips (I-VII, always shown) ────────────────────────
    for (int i = 0; i < ResonateAudioProcessor::MAX_RESONATORS; ++i)
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

    // ── Crisp-only controls (beta layout) ─────────────────────────────────────
    content.addChildComponent(smoothKnob);   content.addChildComponent(smoothLabel);
    content.addChildComponent(chorusKnob);   content.addChildComponent(chorusLabel);
    content.addChildComponent(lfoRateKnob);  content.addChildComponent(lfoRateLabel);
    content.addChildComponent(lfoDepthKnob); content.addChildComponent(lfoDepthLabel);
    content.addChildComponent(expDecayButton);
    content.addChildComponent(centerButton);
    content.addChildComponent(oversample2xButton);
    chorusKnob.setTextValueSuffix(" %");
    lfoRateKnob.setTextValueSuffix(" Hz");
    lfoDepthKnob.setTextValueSuffix(" c");
    expDecayButton.setButtonText("Exp Decay");
    centerButton.setButtonText("DC Center");
    oversample2xButton.setButtonText("2x OS");

    engineSelector.addItem("Crisp", 3);
    engineSelector.addItem("Dark", 4);

    // ── APVTS Attachments: globals (shared by all engines) ────────────────────
    auto& params = audioProcessor.getParameters();
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    engineAttachment     = std::make_unique<CA>(params, "engine", engineSelector);   // id = index + 1
    gainAttachment       = std::make_unique<SA>(params, "gain", gainKnob);
    dryWetAttachment     = std::make_unique<SA>(params, "drywet", dryWetKnob);
    wetOnlyAttachment    = std::make_unique<BA>(params, "wet_only", wetOnlyButton);
    midiEnableAttachment = std::make_unique<BA>(params, "midi_enabled", midiEnableButton);

    perResSelector.onChange = [this]()
        {
            applyPerResMode(perResSelector.getSelectedId() == 2);
        };
    engineSelector.onChange = [this]()
        {
            const int e = engineSelector.getSelectedId() - 1;
            if (e >= 0 && e != currentEngine)
                bindEngine(e);
        };

    // Engine-specific controls are bound to the selected engine's parameters
    bindEngine(audioProcessor.getSelectedEngine());

    // ── Resizing ──────────────────────────────────────────────────────────────

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
    const int lw = logicalWidth();

    constrainer.setFixedAspectRatio(static_cast<double>(lw) / static_cast<double>(lh));
    constrainer.setSizeLimits(
        juce::roundToInt(lw * MIN_SCALE), juce::roundToInt(lh * MIN_SCALE),
        juce::roundToInt(lw * MAX_SCALE), juce::roundToInt(lh * MAX_SCALE));

    setSize(juce::roundToInt(lw * uiScale), juce::roundToInt(lh * uiScale));
}

//==============================================================================
// Point every engine-specific control at the chosen engine's parameter set.
// Ranges (e.g. Crisp's own decay curve) come along with the attachments.
void ResonateAudioProcessorEditor::bindEngine(int engine)
{
    currentEngine = juce::jlimit(0, ResonateAudioProcessor::NUM_ENGINES - 1, engine);
    const juce::String p = enginePrefix();
    auto& params = audioProcessor.getParameters();
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    filterAttachment.reset(); filterFreqAttachment.reset(); filterTypeAttachment.reset();
    modeAttachment.reset(); decayAttachment.reset(); constAttachment.reset();
    colorAttachment.reset(); widthAttachment.reset(); perResAttachment.reset();
    smoothAttachment.reset(); chorusAttachment.reset(); lfoRateAttachment.reset();
    lfoDepthAttachment.reset(); expDecayAttachment.reset(); centerAttachment.reset();
    oversample2xAttachment.reset();

    filterAttachment     = std::make_unique<CA>(params, p + "filter_enabled", filterOnSelector);
    filterFreqAttachment = std::make_unique<SA>(params, p + "filter_freq", filterFreqKnob);
    filterTypeAttachment = std::make_unique<CA>(params, p + "filter_type", filterTypeSelector);
    modeAttachment       = std::make_unique<CA>(params, p + "mode", modeSelector);
    decayAttachment      = std::make_unique<SA>(params, p + "decay", decayKnob);
    constAttachment      = std::make_unique<BA>(params, p + "const_mode", constButton);
    colorAttachment      = std::make_unique<SA>(params, p + "color", colorKnob);
    widthAttachment      = std::make_unique<SA>(params, p + "width", widthKnob);
    perResAttachment     = std::make_unique<CA>(params, p + "per_res_mode", perResSelector);

    const bool crisp = isCrisp();
    if (crisp)
    {
        smoothAttachment       = std::make_unique<SA>(params, p + "smooth", smoothKnob);
        chorusAttachment       = std::make_unique<SA>(params, p + "chorus", chorusKnob);
        lfoRateAttachment      = std::make_unique<SA>(params, p + "lfo_rate", lfoRateKnob);
        lfoDepthAttachment     = std::make_unique<SA>(params, p + "lfo_depth", lfoDepthKnob);
        expDecayAttachment     = std::make_unique<BA>(params, p + "exp_decay", expDecayButton);
        centerAttachment       = std::make_unique<BA>(params, p + "center_mode", centerButton);
        oversample2xAttachment = std::make_unique<BA>(params, p + "oversample_2x", oversample2xButton);
    }
    for (juce::Component* comp : { (juce::Component*) &smoothKnob, (juce::Component*) &smoothLabel,
                                   (juce::Component*) &chorusKnob, (juce::Component*) &chorusLabel,
                                   (juce::Component*) &lfoRateKnob, (juce::Component*) &lfoRateLabel,
                                   (juce::Component*) &lfoDepthKnob, (juce::Component*) &lfoDepthLabel,
                                   (juce::Component*) &expDecayButton, (juce::Component*) &centerButton,
                                   (juce::Component*) &oversample2xButton })
        comp->setVisible(crisp);

    for (int i = 0; i < ResonateAudioProcessor::MAX_RESONATORS; ++i)
        channels[i]->bindEngine(p);

    // Res Mode is per engine as well -> re-apply it (also resizes the window)
    const bool perRes = params.getRawParameterValue(p + "per_res_mode")->load() > 0.5f;
    currentPerResMode = !perRes;   // force the update below
    applyPerResMode(perRes);
}

void ResonateAudioProcessorEditor::applyPerResMode(bool active)
{
    currentPerResMode = active;
    for (int i = 0; i < ResonateAudioProcessor::MAX_RESONATORS; ++i)
        channels[i]->setPerResMode(active);

    updateGlobalEnablement();
    updateEditorSize();
}

//==============================================================================
// Grey out the globals the engine stops reading in Per Res mode.
//
// In updateParameters() only three values are swapped for their local
// equivalents -- effDecay, effColor and effConst. Everything else (Mode,
// Filter, Width, Gain, Dry/Wet) stays global and stays live.
void ResonateAudioProcessorEditor::updateGlobalEnablement()
{
    const bool globalsLive = !currentPerResMode;

    setControlActive(decayKnob, globalsLive);
    setControlActive(decayLabel, globalsLive);
    setControlActive(constButton, globalsLive);
    setControlActive(colorKnob, globalsLive);
    setControlActive(colorLabel, globalsLive);
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

            // Re-sync the parts of the UI that aren't plain attachments
            // (the preset may also have switched the engine).
            bindEngine(audioProcessor.getSelectedEngine());
        });
}

//==============================================================================
void ResonateAudioProcessorEditor::timerCallback()
{
    // Engine changed from outside (automation, preset, host state)?
    const int engineNow = audioProcessor.getSelectedEngine();
    if (engineNow != currentEngine)
        bindEngine(engineNow);

    // Sync mode combo
    int modeIndex = static_cast<int>(
        audioProcessor.getParameters().getRawParameterValue(enginePrefix() + "mode")->load());
    modeSelector.setSelectedId(modeIndex + 1, juce::dontSendNotification);

    // Sync per-res mode (host automation, preset load, undo, ...)
    bool perResNow = audioProcessor.getParameters()
        .getRawParameterValue(enginePrefix() + "per_res_mode")->load() > 0.5f;
    if (perResNow != currentPerResMode)
        applyPerResMode(perResNow);

    // Reflect incoming MIDI note assignments
    for (int i = 0; i < ResonateAudioProcessor::MAX_RESONATORS; ++i)
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
        static_cast<double>(getWidth()) / static_cast<double>(logicalWidth()));

    content.setTransform(juce::AffineTransform::scale(static_cast<float>(uiScale)));
    content.setBounds(0, 0, logicalWidth(), lh);

    layoutContent();

    // Remember the size across sessions (rides along in the APVTS state)
    audioProcessor.getParameters().state.setProperty("ui_scale", uiScale, nullptr);
}

//==============================================================================
// Laid out in LOGICAL coordinates -- unchanged pixel maths, only scaled.
void ResonateAudioProcessorEditor::layoutContent()
{
    auto bounds = content.getLocalBounds().reduced(10);
    int knobSize = 90;
    int labelH = 14;
    int spacing = 8;
    const bool crisp = isCrisp();

    // ── Column 1: Filter -> (Smooth) -> MIDI -> Branding -> Engine ───────────
    //    (beta layout; Smooth only exists in Crisp)
    auto col1 = bounds.removeFromLeft(100);
    filterLabel.setBounds(col1.removeFromTop(labelH));
    filterOnSelector.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    filterTypeLabel.setBounds(col1.removeFromTop(labelH));
    filterTypeSelector.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    freqLabel.setBounds(col1.removeFromTop(labelH));
    filterFreqKnob.setBounds(col1.removeFromTop(knobSize));
    col1.removeFromTop(spacing);
    if (crisp)
    {
        smoothLabel.setBounds(col1.removeFromTop(labelH));
        smoothKnob.setBounds(col1.removeFromTop(knobSize));
        col1.removeFromTop(spacing);
    }
    midiEnableButton.setBounds(col1.removeFromTop(24));
    col1.removeFromTop(spacing);
    auto brandingArea = col1.removeFromTop(44);
    branding.setBounds(brandingArea.withSizeKeepingCentre(80, 40));
    col1.removeFromTop(4);
    engineLabel.setBounds(col1.removeFromTop(labelH));
    engineSelector.setBounds(col1.removeFromTop(24));
    bounds.removeFromLeft(spacing);

    // ── Column 2: Res Mode / Shape Mode / Color / Decay / Const / Presets ────
    auto col2 = bounds.removeFromLeft(100);
    perResLabel.setBounds(col2.removeFromTop(labelH));
    perResSelector.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    modeLabel.setBounds(col2.removeFromTop(labelH));
    modeSelector.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    colorLabel.setBounds(col2.removeFromTop(labelH));
    colorKnob.setBounds(col2.removeFromTop(knobSize));
    col2.removeFromTop(spacing);
    decayLabel.setBounds(col2.removeFromTop(labelH));
    decayKnob.setBounds(col2.removeFromTop(knobSize));
    col2.removeFromTop(spacing);
    constButton.setBounds(col2.removeFromTop(24));
    col2.removeFromTop(spacing);
    presetLabel.setBounds(col2.removeFromTop(labelH));
    {
        auto presetRow = col2.removeFromTop(24);
        savePresetButton.setBounds(presetRow.removeFromLeft(48));
        presetRow.removeFromLeft(4);
        loadPresetButton.setBounds(presetRow.removeFromLeft(48));
    }
    bounds.removeFromLeft(spacing);

    // ── Column 9 (Crisp only, right-most): Chorus / LFO / DC Center / 2x OS ──
    if (crisp)
    {
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
        centerButton.setBounds(col9.removeFromTop(24));
        col9.removeFromTop(spacing);
        oversample2xButton.setBounds(col9.removeFromTop(24));
        bounds.removeFromRight(spacing);
    }

    // ── Column 8: Width / Gain / Dry-Wet / Wet Only / (Exp Decay) ────────────
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
    wetOnlyButton.setBounds(col8.removeFromTop(24));
    if (crisp)
    {
        col8.removeFromTop(spacing);
        expDecayButton.setBounds(col8.removeFromTop(24));
    }
    bounds.removeFromRight(spacing);

    // ── Resonator channel strips I-VII ────────────────────────────────────────
    int channelWidth = bounds.getWidth() / ResonateAudioProcessor::MAX_RESONATORS;
    for (int i = 0; i < ResonateAudioProcessor::MAX_RESONATORS; ++i)
    {
        auto channelBounds = bounds.removeFromLeft(channelWidth).reduced(3);
        channels[i]->setBounds(channelBounds);
    }
}
