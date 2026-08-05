#include "PluginEditor.h"

namespace
{
    const juce::Colour backgroundColour{ 0xff1b1d21 };
    const juce::Colour panelColour{ 0xff24272c };
    const juce::Colour barColour{ 0xff15171a };
    const juce::Colour textColour{ 0xffd6dae0 };
    const juce::Colour infoColour{ 0xff9fb4c7 };

    const char* defaultInfoText =
        "Hover a knob for an explanation.  "
        "DFG = pitch, DHG = the 63-harmonic bank, "
        "DDF = the dynamic filter, DDA = the amplifier, "
        "DFT = the 11-band formant shape.";
}

//==============================================================================
ParamControl::ParamControl(juce::AudioProcessorValueTreeState& state,
    const juce::String& parameterID,
    const juce::String& displayName,
    juce::String descriptionText,
    std::function<void(const juce::String&)> hoverCallback)
    : description(std::move(descriptionText)),
    onHover(std::move(hoverCallback))
{
    caption.setText(displayName, juce::dontSendNotification);
    caption.setJustificationType(juce::Justification::centredTop);
    caption.setMinimumHorizontalScale(0.6f);
    caption.setFont(juce::Font(juce::FontOptions(11.0f)));
    caption.setColour(juce::Label::textColourId, textColour);
    caption.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(caption);

    auto* parameter = state.getParameter(parameterID);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
    {
        combo = std::make_unique<juce::ComboBox>();
        combo->setJustificationType(juce::Justification::centred);

        // ComboBoxAttachment does NOT populate the list - it assumes the items
        // are already there and only syncs the selected index. Without this
        // loop every dropdown opens empty.
        for (int i = 0; i < choice->choices.size(); ++i)
            combo->addItem(choice->choices[i], i + 1);

        addAndMakeVisible(*combo);
        combo->addMouseListener(this, true);

        comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
            (state, parameterID, *combo);
    }
    else
    {
        slider = std::make_unique<juce::Slider>();
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 15);
        slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setColour(juce::Slider::textBoxTextColourId, textColour);
        slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff7fb2d9));
        slider->setColour(juce::Slider::thumbColourId, textColour);

        // Default is 7 decimal places, which turns a cutoff into
        // "7000.0009766". Scale the precision to the size of the range.
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            const auto& range = ranged->getNormalisableRange();
            const float span = range.end - range.start;
            slider->setNumDecimalPlacesToDisplay(span >= 100.0f ? 1 : (span >= 10.0f ? 2 : 3));
        }

        addAndMakeVisible(*slider);
        slider->addMouseListener(this, true);

        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (state, parameterID, *slider);
    }
}

void ParamControl::mouseEnter(const juce::MouseEvent&)
{
    if (onHover)
        onHover(description);
}

void ParamControl::mouseExit(const juce::MouseEvent&)
{
    if (onHover)
        onHover({});
}

void ParamControl::resized()
{
    auto area = getLocalBounds();
    caption.setBounds(area.removeFromTop(26));

    if (combo != nullptr)
        combo->setBounds(area.reduced(4, 16));
    else
        slider->setBounds(area);
}

//==============================================================================
SectionPanel::SectionPanel(const juce::String& sectionTitle, juce::Colour accentColour)
    : title(sectionTitle), accent(accentColour)
{
}

void SectionPanel::addControl(juce::AudioProcessorValueTreeState& state,
    const juce::String& parameterID,
    const juce::String& displayName,
    const juce::String& description,
    std::function<void(const juce::String&)> hoverCallback)
{
    auto* control = new ParamControl(state, parameterID, displayName,
        description, std::move(hoverCallback));
    controls.add(control);
    addAndMakeVisible(control);
}

int SectionPanel::columnsFor(int availableWidth) const
{
    return juce::jmax(1, (availableWidth - 2 * padding) / cellWidth);
}

int SectionPanel::getPreferredHeight(int availableWidth) const
{
    if (controls.isEmpty())
        return headerHeight + padding;

    const int columns = columnsFor(availableWidth);
    const int rows = (controls.size() + columns - 1) / columns;

    return headerHeight + rows * cellHeight + 2 * padding;
}

void SectionPanel::paint(juce::Graphics& g)
{
    g.setColour(panelColour);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    g.setColour(accent);
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, 4.0f, (float)getHeight()), 2.0f);

    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText(title, padding + 8, 4, getWidth() - padding, headerHeight - 4,
        juce::Justification::centredLeft);
}

void SectionPanel::resized()
{
    const int columns = columnsFor(getWidth());

    for (int i = 0; i < controls.size(); ++i)
    {
        const int column = i % columns;
        const int row = i / columns;

        controls.getUnchecked(i)->setBounds(padding + column * cellWidth,
            headerHeight + padding + row * cellHeight,
            cellWidth,
            cellHeight);
    }
}

//==============================================================================
juce::String K5AudioProcessorEditor::describeParameter(const juce::String& parameterID,
    const juce::String& displayName)
{
    // ---- The shared formant filter ----
    if (parameterID.startsWith("dftBand"))
    {
        const int band = parameterID.fromFirstOccurrenceOf("dftBand", false, false)
            .upToFirstOccurrenceOf("_", false, false).getIntValue() + 1;

        const juce::String prefix =
            "DFT = Digital Formant Filter: a fixed spectral shape laid across the whole "
            "harmonic series, shared by both sources. B" + juce::String(band) + " is band "
            + juce::String(band) + " of 11. Bands add on top of a flat response, so with every "
            "gain at zero the DFT is transparent. This is how the K5 gets vocal and instrument-body "
            "colours that no envelope can produce. ";

        if (parameterID.endsWith("_gain")) return prefix + "GAIN: how much this band boosts. Zero = band off.";
        if (parameterID.endsWith("_freq")) return prefix + "FREQ: where the band sits, in Hz.";
        if (parameterID.endsWith("_q"))    return prefix + "Q: how narrow the band is. Higher = tighter, more vowel-like.";
        return prefix;
    }

    // ---- Globals ----
    if (parameterID == "masterGain")
        return "Overall output level. 16 voices x 2 sources x 63 partials adds up fast, so this is "
        "the headroom control rather than a fine trim.";

    if (parameterID == "lfoShape")   return "LFO waveform. Random is a sample-and-hold: a new value each cycle.";
    if (parameterID == "lfoRate")    return "LFO speed in Hz.";
    if (parameterID == "lfoDelay")   return "How long the LFO takes to fade in after a note starts. Classic for delayed vibrato.";
    if (parameterID == "lfoVibrato") return "LFO to pitch, in semitones. Vibrato.";
    if (parameterID == "lfoTremolo") return "LFO to level. Tremolo.";
    if (parameterID == "lfoFilterMod") return "LFO to the DDF cutoff, in octaves. Wobble and growl.";

    // ---- Per-source ----
    const bool isSource = parameterID.startsWith("s1_") || parameterID.startsWith("s2_");

    if (isSource)
    {
        const juce::String which = parameterID.startsWith("s1_") ? "Source 1" : "Source 2";
        const juce::String id = parameterID.substring(3);
        const juce::String head = which + " - ";

        if (id == "level")  return head + "output level of this source. The two sources are summed.";
        if (id == "detune") return head + "detune in cents. A few cents between S1 and S2 gives movement; larger values give intervals.";

        if (id.startsWith("pitch"))
        {
            const juce::String p =
                head + "DFG = Digital Frequency Generator, the pitch section. Its envelope bends pitch "
                "over time - small amounts give a struck or blown attack, large amounts give effects. ";
            if (id == "pitchDepth") return p + "DEPTH: how far the envelope bends pitch, in semitones. At zero the whole pitch envelope does nothing.";
            if (id == "pitchA") return p + "ATTACK: time to reach full bend.";
            if (id == "pitchD") return p + "DECAY: time to fall to the sustain level.";
            if (id == "pitchS") return p + "SUSTAIN: the level held while the key is down.";
            if (id == "pitchR") return p + "RELEASE: time to fall back after key release.";
        }

        if (id.startsWith("amp"))
        {
            const juce::String p =
                head + "DDA = Digital Dynamic Amplifier, the volume envelope of this source. ";
            if (id == "ampA") return p + "ATTACK: time from silence to full level. Short = percussive, long = a pad or swell.";
            if (id == "ampD") return p + "DECAY: time to fall to the sustain level.";
            if (id == "ampS") return p + "SUSTAIN: level held while the key is down. Zero makes a plucked sound that fades away.";
            if (id == "ampR") return p + "RELEASE: fade-out time after key release.";
        }

        if (id == "cutoff" || id == "resonance" || id == "filtEnvAmount"
            || id == "slope24" || id.startsWith("filt"))
        {
            const juce::String p =
                head + "DDF = Digital Dynamic Filter. On the real K5 this is not a filter across the "
                "finished sound - it scales the individual harmonics, which is why it sounds "
                "cleaner than an analogue filter. ";
            if (id == "cutoff")        return p + "CUTOFF: harmonics above this frequency are turned down.";
            if (id == "resonance")     return p + "RESONANCE: emphasises harmonics sitting near the cutoff.";
            if (id == "filtEnvAmount") return p + "ENV AMOUNT: how far the envelope sweeps the cutoff, up to +/-6 octaves. Negative sweeps downward.";
            if (id == "slope24")       return p + "SLOPE: how steeply harmonics above the cutoff fall away. 24 dB/oct is the darker, more abrupt option.";
            if (id == "filtA") return p + "ENV ATTACK: time for the cutoff sweep to open.";
            if (id == "filtD") return p + "ENV DECAY: time to fall to the sustain level.";
            if (id == "filtS") return p + "ENV SUSTAIN: cutoff level held while the key is down.";
            if (id == "filtR") return p + "ENV RELEASE: time for the sweep to fall back after key release.";
        }

        if (id == "harmMode")
            return head + "DHG = Digital Harmonic Generator, the 63-harmonic bank at the heart of the K5. "
            "MODE selects which harmonics sound at all: ALL, ODD only (hollow, clarinet-like), "
            "EVEN, OCTAVE (1, 2, 4, 8...  pure and bell-like) or FIFTH (octaves plus their fifths).";

        if (id == "harmTilt")
            return head + "DHG spectral tilt. Negative rolls the upper harmonics off for a dark, round tone; "
            "positive keeps them for a bright, buzzy one. This sets the static level of each of "
            "the 63 harmonics before the envelopes touch them.";

        if (id.startsWith("grp"))
        {
            const int bus = id.substring(3, 4).getIntValue() + 1;
            const juce::String p =
                head + "DHG harmonic bus " + juce::String(bus) + " of 4. The 63 harmonics are shared out "
                "between four envelopes, so each bus animates roughly a sixteenth of the spectrum. "
                "Giving the busses different shapes is what makes an additive sound move - harmonics "
                "arriving and leaving at different times, rather than one static timbre. ";
            if (id.endsWith("_A")) return p + "ATTACK: how fast these harmonics come in.";
            if (id.endsWith("_D")) return p + "DECAY: time to fall to the sustain level.";
            if (id.endsWith("_S")) return p + "SUSTAIN: level held while the key is down.";
            if (id.endsWith("_R")) return p + "RELEASE: fade-out after key release.";
        }
    }

    return displayName;
}

//==============================================================================
K5AudioProcessorEditor::K5AudioProcessorEditor(K5AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    titleLabel.setText("VirtualK5", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, textColour);
    addAndMakeVisible(titleLabel);

    infoLabel.setText(defaultInfoText, juce::dontSendNotification);
    infoLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    infoLabel.setColour(juce::Label::textColourId, infoColour);
    infoLabel.setJustificationType(juce::Justification::centredLeft);
    infoLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(infoLabel);

    presetBox.setTextWhenNothingSelected("(no preset)");
    presetBox.onChange = [this] { presetSelected(); };
    addAndMakeVisible(presetBox);

    saveButton.onClick = [this] { saveButtonClicked(); };
    deleteButton.onClick = [this] { deleteButtonClicked(); };
    folderButton.onClick = [this] { processor.presetManager.getPresetsFolder().revealToUser(); };

    addAndMakeVisible(saveButton);
    addAndMakeVisible(deleteButton);
    addAndMakeVisible(folderButton);

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    buildSections();
    refreshPresetList(processor.presetManager.getCurrentPresetName());

    setResizable(true, true);
    setResizeLimits(820, 520, 2400, 1600);
    setSize(1100, 780);
}

void K5AudioProcessorEditor::showDescription(const juce::String& text)
{
    infoLabel.setText(text.isEmpty() ? defaultInfoText : text, juce::dontSendNotification);
}

juce::String K5AudioProcessorEditor::shortenName(const juce::String& fullName,
    const juce::String& prefixToDrop)
{
    if (prefixToDrop.isNotEmpty() && fullName.startsWith(prefixToDrop))
        return fullName.substring(prefixToDrop.length()).trim();

    return fullName;
}

void K5AudioProcessorEditor::buildSections()
{
    auto hover = [this](const juce::String& text) { showDescription(text); };

    auto makeSection = [this, hover](const juce::String& sectionTitle,
        juce::Colour accent,
        const std::function<bool(const juce::String&)>& matches,
        const std::function<juce::String(const juce::String&)>& shorten)
        {
            auto* panel = new SectionPanel(sectionTitle, accent);

            for (auto* parameter : processor.getParameters())
            {
                if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                {
                    if (!matches(withID->paramID))
                        continue;

                    const auto name = withID->getName(64);
                    panel->addControl(processor.apvts, withID->paramID, shorten(name),
                        describeParameter(withID->paramID, name), hover);
                }
            }

            sections.add(panel);
            content.addAndMakeVisible(panel);
        };

    makeSection("SOURCE 1", juce::Colour(0xffe8a33d),
        [](const juce::String& id) { return id.startsWith("s1_"); },
        [](const juce::String& name) { return shortenName(name, "S1"); });

    makeSection("SOURCE 2", juce::Colour(0xff4fa3d1),
        [](const juce::String& id) { return id.startsWith("s2_"); },
        [](const juce::String& name) { return shortenName(name, "S2"); });

    makeSection("GLOBAL / LFO", juce::Colour(0xff9ad14f),
        [](const juce::String& id) { return id == "masterGain" || id.startsWith("lfo"); },
        [](const juce::String& name) { return name; });

    makeSection("FORMANT FILTER (DFT)", juce::Colour(0xffd15f9a),
        [](const juce::String& id) { return id.startsWith("dftBand"); },
        [](const juce::String& name) { return juce::String(name).replace("DFT Band ", "B"); });
}

void K5AudioProcessorEditor::refreshPresetList(const juce::String& nameToSelect)
{
    presetBox.onChange = nullptr;
    presetBox.clear(juce::dontSendNotification);

    const auto names = processor.presetManager.getAllPresetNames();

    for (int i = 0; i < names.size(); ++i)
        presetBox.addItem(names[i], i + 1);

    const int index = names.indexOf(nameToSelect);

    if (index >= 0)
        presetBox.setSelectedItemIndex(index, juce::dontSendNotification);
    else
        presetBox.setText(nameToSelect, juce::dontSendNotification);

    presetBox.onChange = [this] { presetSelected(); };
}

void K5AudioProcessorEditor::presetSelected()
{
    const auto name = presetBox.getText();

    if (name.isNotEmpty())
        processor.presetManager.loadPresetByName(name);
}

void K5AudioProcessorEditor::saveButtonClicked()
{
    saveDialog = std::make_unique<juce::AlertWindow>("Save Preset",
        "Name for this patch:",
        juce::MessageBoxIconType::NoIcon);

    saveDialog->addTextEditor("presetName", processor.presetManager.getCurrentPresetName());
    saveDialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    saveDialog->enterModalState(true,
        juce::ModalCallbackFunction::create([this](int result) { finishSave(result); }),
        false);
}

void K5AudioProcessorEditor::finishSave(int modalResult)
{
    juce::String name;

    if (saveDialog != nullptr && modalResult == 1)
        name = saveDialog->getTextEditorContents("presetName").trim();

    saveDialog.reset();

    if (name.isEmpty())
        return;

    if (processor.presetManager.savePreset(name))
        refreshPresetList(name);
}

void K5AudioProcessorEditor::deleteButtonClicked()
{
    const auto name = presetBox.getText();

    if (name.isEmpty())
        return;

    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
        "Delete Preset",
        "Delete \"" + name + "\"? This cannot be undone.",
        "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create([this, name](int result)
            {
                if (result == 1 && processor.presetManager.deletePreset(name))
                    refreshPresetList("Init");
            }));
}

void K5AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    g.setColour(barColour);
    g.fillRect(0, 0, getWidth(), 60);
}

void K5AudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop(60).reduced(10, 8);

    titleLabel.setBounds(bar.removeFromLeft(100));
    presetBox.setBounds(bar.removeFromLeft(200).reduced(0, 8));

    bar.removeFromLeft(12);

    folderButton.setBounds(bar.removeFromRight(72).reduced(0, 8));
    bar.removeFromRight(6);
    deleteButton.setBounds(bar.removeFromRight(72).reduced(0, 8));
    bar.removeFromRight(6);
    saveButton.setBounds(bar.removeFromRight(92).reduced(0, 8));
    bar.removeFromRight(12);

    // Whatever is left in the middle is the hover description panel.
    infoLabel.setBounds(bar);

    viewport.setBounds(area);

    const int width = juce::jmax(200, viewport.getMaximumVisibleWidth());
    int y = 8;

    for (auto* panel : sections)
    {
        const int height = panel->getPreferredHeight(width - 16);
        panel->setBounds(8, y, width - 16, height);
        y += height + 10;
    }

    content.setSize(width, y);
}