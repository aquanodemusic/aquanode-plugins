#include "PluginEditor.h"
#include "AquaVibrioPatch.h"
#include "AquaVibrioRandom.h"

#include <map>
#include <set>

#include <limits>

namespace aquavibrio
{

AquaVibrioEditor::AquaVibrioEditor (AquaVibrioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("AQUA VIBRIO", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    title.setColour (juce::Label::textColourId, Theme::textBright);
    addAndMakeVisible (title);

    patchName.setText ("Init", juce::dontSendNotification);
    patchName.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Bold")));
    patchName.setEditable (true);
    patchName.setColour (juce::Label::backgroundColourId, Theme::trackEmpty);
    patchName.setColour (juce::Label::textColourId, Theme::accent);
    patchName.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (patchName);

    patchList.setTextWhenNothingSelected ("No presets loaded");
    patchList.onChange = [this]
    {
        const int i = patchList.getSelectedItemIndex();
        if (i >= 0)
        {
            // With notes selected the preset goes to those notes only, and
            // the whole patch keeps its name.
            const bool forSelection = processor.getNoteLayers().hasSelection();
            processor.selectPatch (i);
            if (! forSelection)
                patchName.setText (processor.getCurrentPatchName(), juce::dontSendNotification);
        }
    };
    addAndMakeVisible (patchList);

    // Factory presets. The list is built straight from the table, with a
    // heading whenever the category changes, so adding a preset there is the
    // only step needed to have it appear here.
    {
        juce::String lastCategory;
        int itemId = 1;

        for (const auto& preset : factoryPresets())
        {
            const juce::String category (preset.category);

            if (category != lastCategory)
            {
                factoryList.addSectionHeading (category);
                lastCategory = category;
            }

            factoryList.addItem (preset.name, itemId++);
        }
    }

    // A pick applies straight away and the menu goes back to its title, so
    // picking the same preset again re-applies it (no Apply button needed).
    factoryList.setTextWhenNothingSelected ("Factory presets");
    factoryList.setTooltip ("Load a factory preset (into the selected notes, while Note Lock has notes selected).");
    factoryList.onChange = [this]
    {
        const int id = factoryList.getSelectedId();
        if (id < 1 || id > (int) factoryPresets().size())
            return;

        const auto& preset = factoryPresets()[(size_t) (id - 1)];
        auto& noteLayers = processor.getNoteLayers();
        const bool forSelection = noteLayers.hasSelection();
        noteLayers.loadWholePatch ([this, &preset] { applyFactoryPreset (preset, processor.getState()); }, {});
        if (! forSelection)
            patchName.setText (preset.name, juce::dontSendNotification);

        factoryList.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (factoryList);

    engineButton.setClickingTogglesState (true);
    engineButton.setTooltip ("Native: Aqua Vibrio's own engine.\n"
                             "Original: an engine calibrated by measurement - filter, envelopes, "
                             "levels, unison and saturation behave the way classic hardware "
                             "patches expect.");
    engineButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent.withAlpha (0.85f));
    engineButton.setColour (juce::TextButton::textColourOnId, Theme::background);
    engineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getState(), "EngineMode", engineButton);
    engineButton.onStateChange = [this] { updateEngineButton(); };
    updateEngineButton();

    wavesButton.onClick = [this] { chooseWaveFolder(); };
    updateWavesButton();

    // Master volume: a small knob at the end of the header. Not part of a
    // preset; the engine switch sets it (Original +12 dB, Native 0 dB).
    masterLabel.setText ("Master", juce::dontSendNotification);
    masterLabel.setJustificationType (juce::Justification::centredRight);
    masterLabel.setColour (juce::Label::textColourId, Theme::accent);
    masterKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
    masterKnob.setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
    masterKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    masterKnob.getProperties().set ("bipolar", true);   // the arc grows from 0 dB
    masterKnob.setTooltip ("Master volume, -24 to +24 dB (double-click: 0 dB). Not stored in presets.\n"
                           "Switching to ORIGINAL sets it to +12 dB, switching to NATIVE to 0 dB.");
    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getState(), "MasterVolume", masterKnob);
    masterKnob.setDoubleClickReturnValue (true, 0.0);

    loadButton.onClick = [this] { loadBankFile(); };
    saveButton.onClick = [this] { savePatchFile(); };
    prevButton.onClick = [this]
    {
        const int i = patchList.getSelectedItemIndex();
        if (i > 0) patchList.setSelectedItemIndex (i - 1);
    };
    nextButton.onClick = [this]
    {
        const int i = patchList.getSelectedItemIndex();
        if (i + 1 < patchList.getNumItems()) patchList.setSelectedItemIndex (i + 1);
    };

    for (auto* b : { &loadButton, &saveButton, &prevButton, &nextButton })
        addAndMakeVisible (*b);

    // Randomiser. The character list ends with "Anything" and "Super Random";
    // everything above it aims at a recognisable kind of patch.
    characterBox.addItemList (RandomPreset::characterNames(), 1);
    characterBox.setSelectedId ((int) RandomPreset::Pad + 1, juce::dontSendNotification);
    characterBox.setTooltip ("What to aim for when randomising. Super Random, at the "
                             "bottom of the list, randomises everything with only the "
                             "guards needed to keep it audible.");
    addAndMakeVisible (characterBox);

    randomButton.onClick = [this] { randomise(); };
    randomButton.setTooltip ("Generate a new patch of the chosen character (for the selected notes, "
                             "when Note Lock has notes selected).");
    addAndMakeVisible (randomButton);

    // Mutator
    strengthBox.addItemList (Mutator::strengthNames(), 1);
    strengthBox.setSelectedItemIndex ((int) Mutator::Medium, juce::dontSendNotification);
    strengthBox.setTooltip ("How far Mutate and Random assign move the knobs.");

    mutateButton.setTooltip ("Nudge the feel of the patch - brighter, snappier, wider, wobblier, dirtier - "
                             "without touching pitch or structure. With notes selected, only those notes.");
    mutateButton.onClick = [this] { processor.getNoteLayers().mutate (currentStrength()); };

    historyBack.setTooltip ("Step back to the previous mutation.");
    historyForward.setTooltip ("Step forward again.");
    historyBack.onClick = [this] { processor.getNoteLayers().stepBack(); };
    historyForward.onClick = [this] { processor.getNoteLayers().stepForward(); };

    // Note Lock
    noteLockStrip = std::make_unique<NoteLockStrip> (processor.getState(), processor.getNoteLayers(),
                                                     [this] { return currentStrength(); });
    addChildComponent (*noteLockStrip);

    noteLockButton.setClickingTogglesState (true);
    noteLockButton.setTooltip ("Give single notes, or sets of notes, their own variation of the patch.");
    noteLockButton.onClick = [this]
    {
        // Closing the strip keeps what was being edited.
        if (! noteLockButton.getToggleState())
            processor.getNoteLayers().finishSelection();
        noteLockStrip->setVisible (noteLockButton.getToggleState());
        resized();
        repaint();
    };

    // A patch that already has layers opens with the strip showing them.
    noteLockButton.setToggleState (! processor.getNoteLayers().getLayers().empty(), juce::dontSendNotification);
    noteLockStrip->setVisible (noteLockButton.getToggleState());
    processor.getNoteLayers().addChangeListener (this);

    // Build the cards. Panels that name a group are collected into one card
    // with a selector; the rest stand alone. A group takes the position of
    // its first member, so the panel order still reads the way the table
    // lists it.
    std::vector<juce::String> groupOrder;
    std::map<juce::String, std::vector<const RegionSlot*>> grouped;

    for (const auto& slot : panelLayout())
    {
        const juce::String group (slot.group != nullptr ? slot.group : "");

        if (group.isEmpty())
            continue;

        if (grouped.find (group) == grouped.end())
            groupOrder.push_back (group);

        grouped[group].push_back (&slot);
    }

    std::set<juce::String> placed;

    for (const auto& slot : panelLayout())
    {
        const juce::String group (slot.group != nullptr ? slot.group : "");

        if (group.isNotEmpty())
        {
            if (placed.count (group) > 0)
                continue;                       // already made, at its first member

            placed.insert (group);

            auto panel = std::make_unique<GroupPanel> (processor.getState(), group,
                                                        grouped[group], true,
                                                        &processor.getNoteLayers());
            auto* raw = panel.get();
            canvas.addAndMakeVisible (*panel);

            cards.push_back ({ std::move (panel), raw->widestColumns(),
                               [raw] (int w) { return raw->preferredHeightForWidth (w); } });
        }
        else
        {
            auto panel = std::make_unique<RegionPanel> (processor.getState(), slot, true,
                                                         &processor.getNoteLayers());
            auto* raw = panel.get();
            canvas.addAndMakeVisible (*panel);

            cards.push_back ({ std::move (panel), slot.knobColumns,
                               [raw] (int w) { return raw->preferredHeightForWidth (w); } });
        }
    }

    keyboardPanel = std::make_unique<KeyboardPanel> (processor.getKeyboardState(), processor.getNoteLayers());
    canvas.addAndMakeVisible (*keyboardPanel);

    canvasHolder.addAndMakeVisible (canvas);
    viewport.setViewedComponent (&canvasHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    // Every header control lives in one container that gets scaled as a unit.
    for (auto* c : { (juce::Component*) &title, (juce::Component*) &engineButton, (juce::Component*) &wavesButton, (juce::Component*) &patchName,
                     (juce::Component*) &prevButton, (juce::Component*) &nextButton,
                     (juce::Component*) &patchList, (juce::Component*) &loadButton,
                     (juce::Component*) &saveButton, (juce::Component*) &factoryList,
                     (juce::Component*) &characterBox, (juce::Component*) &randomButton,
                     (juce::Component*) &mutateButton, (juce::Component*) &strengthBox,
                     (juce::Component*) &historyBack, (juce::Component*) &historyForward,
                     (juce::Component*) &noteLockButton, (juce::Component*) &masterLabel,
                     (juce::Component*) &masterKnob })
        headerControls.addAndMakeVisible (*c);

    addAndMakeVisible (headerControls);

    refreshMutationButtons();
    refreshPatchList();   // a bank loaded before the editor was (re)opened

    addAndMakeVisible (headerControls);

    setResizable (true, true);
    setResizeLimits (900, 520, 3200, 2000);
    setSize (1200, 700);
}

AquaVibrioEditor::~AquaVibrioEditor()
{
    processor.getNoteLayers().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void AquaVibrioEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshMutationButtons();

    // Only the frame around the panel shows the selection: repaint just that,
    // not four hundred knobs, on every key clicked.
    const auto v = viewport.getBounds();
    repaint (v.withHeight (24));
    repaint (v.withTrimmedTop (v.getHeight() - 4));
    repaint (v.withWidth (4));
    repaint (v.withTrimmedLeft (v.getWidth() - 4));
}

void AquaVibrioEditor::refreshMutationButtons()
{
    auto& noteLayers = processor.getNoteLayers();
    historyBack.setEnabled (noteLayers.canStepBack());
    historyForward.setEnabled (noteLayers.canStepForward());
    historyBack.setAlpha (historyBack.isEnabled() ? 1.0f : 0.3f);
    historyForward.setAlpha (historyForward.isEnabled() ? 1.0f : 0.3f);

    const bool selecting = noteLayers.hasSelection();
    mutateButton.setButtonText (selecting ? "Mutate notes" : "Mutate");
    if (selecting && ! noteLockButton.getToggleState())
    {
        noteLockButton.setToggleState (true, juce::dontSendNotification);
        noteLockStrip->setVisible (true);
        resized();
    }
}

// While notes are selected every knob edits them, not the whole patch: the
// panel says so with a frame in the colour the selection's layer will have.
void AquaVibrioEditor::paintOverChildren (juce::Graphics& g)
{
    auto& noteLayers = processor.getNoteLayers();
    if (! noteLayers.hasSelection())
        return;

    const auto colour = layerColour (noteLayers.upcomingNumber());
    g.setColour (colour);
    g.drawRect (viewport.getBounds(), 3);

    const auto text = "Knobs edit " + noteLayers.describeSelection() + juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  "))
                    + "layer " + juce::String (noteLayers.upcomingNumber());
    const auto font = juce::Font (juce::FontOptions (12.0f).withStyle ("Bold"));
    const int w = juce::GlyphArrangement::getStringWidthInt (font, text) + 20;
    auto tag = juce::Rectangle<int> (viewport.getRight() - w - 24, viewport.getY(), w, 20);
    g.fillRoundedRectangle (tag.toFloat(), 3.0f);
    g.setColour (Theme::background);
    g.setFont (font);
    g.drawText (text, tag, juce::Justification::centred, false);
}

void AquaVibrioEditor::paint (juce::Graphics& g)
{
    g.setGradientFill ({ Theme::background.brighter (0.06f), 0.0f, 0.0f,
                         Theme::background.darker (0.25f), 0.0f, (float) getHeight(), false });
    g.fillAll();

    auto header = getLocalBounds().removeFromTop (46);
    g.setGradientFill ({ Theme::groupTop, 0.0f, (float) header.getY(),
                         Theme::groupBottom, 0.0f, (float) header.getBottom(), false });
    g.fillRect (header);
    g.setColour (Theme::headerBar);
    g.fillRect (header.removeFromBottom (2));
}

void AquaVibrioEditor::resized()
{
    // The header row is laid out at a fixed design width and then scaled to
    // the window, exactly like the panel below it - so at any size the whole
    // plugin is the same picture, just larger or smaller.
    // Four groups, left to right: the patch (name, browse, load, save), new
    // patches (factory, random), evolving the one you have (mutate and its
    // history), and Note Lock. Thin rules between them, drawn by the strip.
    auto header = juce::Rectangle<int> (0, 0, kHeaderDesignWidth, kHeaderHeight).reduced (12, 9);
    const auto take = [&header] (int w) { auto r = header.removeFromLeft (w); return r; };
    const auto space = [&header] (int w) { header.removeFromLeft (w); };
    std::vector<int> rules;
    const auto rule = [&] { space (14); rules.push_back (header.getX()); space (14); };

    title.setBounds (take (118));
    space (6);
    engineButton.setBounds (take (96));
    space (4);
    wavesButton.setBounds (take (64));
    space (8);
    patchName.setBounds (take (140));
    space (4);
    prevButton.setBounds (take (24));
    nextButton.setBounds (take (24));
    space (4);
    patchList.setBounds (take (140));
    space (4);
    loadButton.setBounds (take (54));
    space (4);
    saveButton.setBounds (take (54));
    rule();

    factoryList.setBounds (take (140));
    space (6);
    characterBox.setBounds (take (110));
    space (4);
    randomButton.setBounds (take (92));
    rule();

    mutateButton.setBounds (take (96));
    space (4);
    strengthBox.setBounds (take (82));
    space (2);
    historyBack.setBounds (take (22).reduced (3, 5));
    historyForward.setBounds (take (22).reduced (3, 5));
    rule();

    noteLockButton.setBounds (take (100));
    rule();

    masterLabel.setBounds (take (46));
    space (2);
    masterKnob.setBounds (take (92));
    headerControls.setRules (rules);

    const float headerScale = juce::jmin (1.0f, (float) getWidth() / (float) kHeaderDesignWidth);
    headerControls.setTransform (juce::AffineTransform::scale (headerScale));
    headerControls.setBounds (0, 0, kHeaderDesignWidth, kHeaderHeight);

    // The Note Lock strip sits right under the header, scaled the same way.
    int top = kHeaderHeight;
    if (noteLockStrip != nullptr && noteLockStrip->isVisible())
    {
        const int stripHeight = NoteLockStrip::kDesignHeight;
        noteLockStrip->setTransform (juce::AffineTransform::scale (headerScale).translated (0.0f, (float) kHeaderHeight + 2.0f));
        noteLockStrip->setBounds (0, 0, kHeaderDesignWidth, stripHeight);
        top += juce::roundToInt ((float) stripHeight * headerScale) + 4;
    }

    viewport.setBounds (getLocalBounds().withTrimmedTop (top));

    // The vertical scrollbar is always present. Letting it auto-hide means the
    // canvas width changes the moment the content gets tall enough to need
    // one, which relays out the panels, which changes the height again - that
    // loop is what made panels overlap while dragging a window edge.
    viewport.getVerticalScrollBar().setAutoHide (false);

    //== masonry ===============================================================
    // Rows were the wrong idea: a row is only as short as its tallest panel,
    // so a 29-parameter block next to a 2-parameter one left most of the row
    // empty. Instead the canvas is a grid of narrow units, each module is a
    // fixed number of units wide - its own knob columns, so knobs never
    // stretch - and each one drops into whichever position leaves it highest.
    // Short modules tuck under tall neighbours and the holes close up.
    // Always the design width: the canvas is a fixed picture that gets scaled,
    // never re-flowed, so a module's width in pixels is the same share of the
    // window at every size.
    const int contentWidth = kCanvasWidth;

    const int gap = 6;

    // The grid is a fixed 12 units wide (used to be 14, at a fixed 82px per
    // unit) and every module is exactly 3 of those units - one module per
    // knob column plus a touch of breathing room - so four modules sit on a
    // row at the plugin's default size and each one now gets more pixels per
    // unit than before, which is what actually makes the knobs bigger.
    const int unitCount = 12;
    const float unitWidth = (float) (contentWidth - gap) / (float) unitCount;

    std::vector<int> columnBottom ((size_t) unitCount, gap);
    std::vector<std::pair<int, int>> placed;   // each card's first unit and span

    for (size_t i = 0; i < cards.size(); ++i)
    {
        const int span = juce::jlimit (1, unitCount, cards[i].span);

        const int width = juce::roundToInt (unitWidth * (float) span) - gap;
        const int height = cards[i].preferredHeight (width);

        // the leftmost starting unit whose span sits highest
        int bestStart = 0;
        int bestTop = std::numeric_limits<int>::max();

        for (int start = 0; start + span <= unitCount; ++start)
        {
            int spanTop = 0;
            for (int u = start; u < start + span; ++u)
                spanTop = juce::jmax (spanTop, columnBottom[(size_t) u]);

            if (spanTop < bestTop)
            {
                bestTop = spanTop;
                bestStart = start;
            }
        }

        const int x = gap + juce::roundToInt (unitWidth * (float) bestStart);
        cards[i].component->setBounds (x, bestTop, width, height);
        placed.emplace_back (bestStart, span);

        for (int u = bestStart; u < bestStart + span; ++u)
            columnBottom[(size_t) u] = bestTop + height + gap;
    }

    // Even bottoms: the last card in every column reaches down to the same
    // line as the tallest column, so the panel ends in one straight edge
    // above the keyboard instead of a skyline.
    int y = gap;
    for (auto bottom : columnBottom)
        y = juce::jmax (y, bottom);

    // And when the window is taller than the panel, that line moves down so
    // the keyboard sits on the window's bottom edge - no empty strip below
    // it. (Shorter than the panel, it scrolls as before.)
    const float viewScale = (float) juce::jmax (200, viewport.getMaximumVisibleWidth()) / (float) kCanvasWidth;
    const int visibleCanvasHeight = (int) std::floor ((float) viewport.getMaximumVisibleHeight() / viewScale);
    y = juce::jmax (y, visibleCanvasHeight - keyboardPanel->getPreferredHeight() - gap);

    for (size_t i = 0; i < cards.size(); ++i)
    {
        auto* c = cards[i].component.get();
        const auto [start, span] = placed[i];

        bool lastInColumn = true;
        for (int u = start; u < start + span; ++u)
            lastInColumn = lastInColumn && columnBottom[(size_t) u] == c->getBottom() + gap;

        if (lastInColumn)
            c->setBounds (c->getBounds().withBottom (y - gap));
    }

    // The keyboard: the full range, the full width, along the bottom.
    keyboardPanel->setBounds (gap, y, contentWidth - 2 * gap, keyboardPanel->getPreferredHeight());

    // the same margin below the keyboard as above the first row of cards
    const int canvasHeight = keyboardPanel->getBottom() + gap;
    canvas.setSize (contentWidth, canvasHeight);

    // Fit the width, scroll for whatever is left over vertically.
    const int visibleWidth = juce::jmax (200, viewport.getMaximumVisibleWidth());
    const float scale = (float) visibleWidth / (float) kCanvasWidth;

    canvas.setTransform (juce::AffineTransform::scale (scale));
    canvasHolder.setSize (visibleWidth, juce::roundToInt ((float) canvasHeight * scale));
}

//==============================================================================
void AquaVibrioEditor::randomise()
{
    // With notes selected the new patch is theirs; otherwise it is a fresh
    // patch for the whole instrument, without layers.
    auto& noteLayers = processor.getNoteLayers();
    const bool forSelection = noteLayers.hasSelection();

    const int index = characterBox.getSelectedItemIndex();
    const int superRandom = RandomPreset::NumCharacters;   // the last item in the list

    if (index == superRandom)
    {
        noteLayers.loadWholePatch ([this] { RandomPreset::superRandomise (processor.getState(), rng); }, {});
        if (! forSelection)
            patchName.setText (RandomPreset::wildName (rng), juce::dontSendNotification);
        return;
    }

    const auto character = (RandomPreset::Character)
        juce::jlimit (0, (int) RandomPreset::Anything, index);

    noteLayers.loadWholePatch ([this, character] { RandomPreset::generate (processor.getState(), character, rng); }, {});
    if (! forSelection)
        patchName.setText (RandomPreset::nameFor (character, rng), juce::dontSendNotification);
}

void AquaVibrioEditor::updateEngineButton()
{
    const bool original = engineButton.getToggleState();
    const juce::String text = original ? "ORIGINAL" : "NATIVE";
    if (engineButton.getButtonText() != text)
        engineButton.setButtonText (text);

    // The knobs print their values from the selected engine's curves.
    if (display::originalMode().exchange (original) != original)
    {
        std::function<void (juce::Component&)> refresh = [&refresh] (juce::Component& c)
        {
            if (auto* sl = dynamic_cast<juce::Slider*> (&c))
                sl->updateText();
            for (auto* child : c.getChildren())
                refresh (*child);
        };
        refresh (*this);
        repaint();
    }
}

void AquaVibrioEditor::updateWavesButton()
{
    const int n = processor.getNumSampledWaves();
    wavesButton.setTooltip (n > 0
        ? "Original engine: " + juce::String (n) + " sampled waveforms loaded from\n"
            + processor.getSampledWaveFolder().getFullPathName() + "\nClick to choose another folder."
        : "Original engine: choose the folder with your sampled waveforms (Wave 03.wav .. "
          "Wave 64.wav, and optionally a folder of \"NN Name.wav\" wavetables). Without them, the "
          "wave numbers play Aqua Vibrio's own generated waves.");
    wavesButton.setColour (juce::TextButton::textColourOffId, n > 0 ? Theme::accent : Theme::textBright);
}

void AquaVibrioEditor::chooseWaveFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Folder with your sampled waveforms (Wave 03.wav ..)",
                                                   processor.getSampledWaveFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory())
                return;
            if (processor.setSampledWaveFolder (dir) == 0)
                juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Aqua Vibrio",
                    "No sampled waveforms (Wave 03.wav .. Wave 64.wav) found in that folder.");
            updateWavesButton();
        });
}

void AquaVibrioEditor::loadBankFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Load a preset or a sound bank (.mid / .syx)", juce::File{}, "*.json;*.mid;*.midi;*.syx");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            refreshPatchListAfter (file);

            if (processor.getCurrentPatchIndex() < 0)
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Aqua Vibrio",
                    "That doesn't look like an Aqua Vibrio preset or a sound bank.");
        });
}

void AquaVibrioEditor::savePatchFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Save preset as JSON", juce::File{}, "*.json");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            if (! file.hasFileExtension ("json"))
                file = file.withFileExtension ("json");

            processor.saveCurrentPatch (file, patchName.getText());

            // Re-scan the folder we just saved into, exactly like a Load
            // does, so the new preset takes its place in the patch list and
            // prev / next can reach it right away.
            refreshPatchListAfter (file);
        });
}

// Shared by Load and Save: re-scans the preset's folder and brings the
// patch list, and the current selection within it, back in sync with what
// the processor now has loaded.
void AquaVibrioEditor::refreshPatchListAfter (const juce::File& file)
{
    processor.loadBank (file);
    refreshPatchList();
}

// The bank lives in the processor, so a closed and reopened editor shows the
// same list and selection instead of "No presets loaded".
void AquaVibrioEditor::refreshPatchList()
{
    const int loaded = processor.getNumPatches();

    patchList.clear (juce::dontSendNotification);
    for (int i = 0; i < loaded; ++i)
        patchList.addItem (juce::String (i + 1).paddedLeft ('0', 3) + "  "
                             + processor.getPatchName (i), i + 1);

    const int index = processor.getCurrentPatchIndex();
    if (index >= 0)
    {
        patchList.setSelectedItemIndex (index, juce::dontSendNotification);
        if (! processor.getNoteLayers().hasSelection())
            patchName.setText (processor.getCurrentPatchName(), juce::dontSendNotification);
    }
}

} // namespace aquavibrio
