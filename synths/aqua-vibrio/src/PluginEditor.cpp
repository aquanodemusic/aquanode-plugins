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
            processor.selectPatch (i);
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

    factoryList.setTextWhenNothingSelected ("Factory presets");
    addAndMakeVisible (factoryList);

    applyButton.onClick = [this]
    {
        const int id = factoryList.getSelectedId();

        if (id < 1 || id > (int) factoryPresets().size())
            return;

        const auto& preset = factoryPresets()[(size_t) (id - 1)];
        applyFactoryPreset (preset, processor.getState());
        patchName.setText (preset.name, juce::dontSendNotification);
    };

    addAndMakeVisible (applyButton);

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
    randomButton.setTooltip ("Generate a new patch of the chosen character.");
    addAndMakeVisible (randomButton);

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

            // The Matrix Slot card is driven from the top bar rather than a
            // selector in its own header - see matrixSlotSelector below.
            const bool ownSelector = group != "Matrix Slot";

            auto panel = std::make_unique<GroupPanel> (processor.getState(), group,
                                                        grouped[group], ownSelector);
            auto* raw = panel.get();
            canvas.addAndMakeVisible (*panel);

            if (! ownSelector)
                matrixSlotGroup = raw;

            cards.push_back ({ std::move (panel), raw->widestColumns(),
                               [raw] (int w) { return raw->preferredHeightForWidth (w); } });
        }
        else
        {
            auto panel = std::make_unique<RegionPanel> (processor.getState(), slot);
            auto* raw = panel.get();
            canvas.addAndMakeVisible (*panel);

            cards.push_back ({ std::move (panel), slot.knobColumns,
                               [raw] (int w) { return raw->preferredHeightForWidth (w); } });
        }
    }

    keyboardPanel = std::make_unique<KeyboardPanel> (processor.getKeyboardState());
    canvas.addAndMakeVisible (*keyboardPanel);

    // The Matrix Slot picker: built now that matrixSlotGroup exists (the
    // cards loop above just ran), and drives that card's own combobox
    // directly rather than keeping a second copy of the item list. This has
    // to happen before the header loop below, so the selector already has
    // its items when it becomes part of the scaled header container.
    if (auto* group = matrixSlotGroup)
    {
        auto& inner = group->getExternalSelector();

        for (int i = 1; i <= inner.getNumItems(); ++i)
            matrixSlotSelector.addItem (inner.getItemText (i - 1), i);

        matrixSlotSelector.setSelectedItemIndex (0, juce::dontSendNotification);
        matrixSlotSelector.onChange = [this, group]
        {
            group->getExternalSelector().setSelectedItemIndex (
                matrixSlotSelector.getSelectedItemIndex());
        };
    }

    canvasHolder.addAndMakeVisible (canvas);
    viewport.setViewedComponent (&canvasHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    // Every header control lives in one container that gets scaled as a unit.
    for (auto* c : { (juce::Component*) &title, (juce::Component*) &patchName,
                     (juce::Component*) &prevButton, (juce::Component*) &nextButton,
                     (juce::Component*) &patchList, (juce::Component*) &factoryList,
                     (juce::Component*) &applyButton, (juce::Component*) &characterBox,
                     (juce::Component*) &randomButton, (juce::Component*) &matrixSlotSelector,
                     (juce::Component*) &loadButton, (juce::Component*) &saveButton })
        headerControls.addAndMakeVisible (*c);

    addAndMakeVisible (headerControls);

    setResizable (true, true);
    setResizeLimits (900, 520, 3200, 2000);
    setSize (1200, 700);
}

AquaVibrioEditor::~AquaVibrioEditor()
{
    setLookAndFeel (nullptr);
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
    auto header = juce::Rectangle<int> (0, 0, kHeaderDesignWidth, kHeaderHeight).reduced (10, 8);

    saveButton.setBounds (header.removeFromRight (64).reduced (0, 2));
    header.removeFromRight (6);
    loadButton.setBounds (header.removeFromRight (64).reduced (0, 2));
    header.removeFromRight (10);
    matrixSlotSelector.setBounds (header.removeFromRight (140).reduced (0, 2));
    header.removeFromRight (10);
    randomButton.setBounds (header.removeFromRight (86).reduced (0, 2));
    header.removeFromRight (6);
    characterBox.setBounds (header.removeFromRight (132).reduced (0, 2));
    header.removeFromRight (10);

    title.setBounds (header.removeFromLeft (150));
    header.removeFromLeft (8);
    patchName.setBounds (header.removeFromLeft (170).reduced (0, 2));
    header.removeFromLeft (6);
    prevButton.setBounds (header.removeFromLeft (26).reduced (0, 2));
    nextButton.setBounds (header.removeFromLeft (26).reduced (0, 2));
    header.removeFromLeft (6);

    patchList.setBounds (header.removeFromLeft (juce::jmax (120, header.getWidth() * 2 / 5)).reduced (0, 2));
    header.removeFromLeft (10);
    applyButton.setBounds (header.removeFromRight (60).reduced (2, 2));
    factoryList.setBounds (header.reduced (0, 2));

    const float headerScale = juce::jmin (1.0f, (float) getWidth() / (float) kHeaderDesignWidth);
    headerControls.setTransform (juce::AffineTransform::scale (headerScale));
    headerControls.setBounds (0, 0, kHeaderDesignWidth, kHeaderHeight);

    viewport.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight));

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
            int top = 0;
            for (int u = start; u < start + span; ++u)
                top = juce::jmax (top, columnBottom[(size_t) u]);

            if (top < bestTop)
            {
                bestTop = top;
                bestStart = start;
            }
        }

        const int x = gap + juce::roundToInt (unitWidth * (float) bestStart);
        cards[i].component->setBounds (x, bestTop, width, height);

        for (int u = bestStart; u < bestStart + span; ++u)
            columnBottom[(size_t) u] = bestTop + height + gap;
    }

    // Not a parameter panel - no region, no knob grid - so it doesn't come
    // out of panelLayout(), but it lands the same way: dropped into whichever
    // span of units is currently shortest, exactly like any other module.
    {
        const int span = juce::jlimit (1, unitCount, KeyboardPanel::kSpanUnits);
        const int width = juce::roundToInt (unitWidth * (float) span) - gap;
        const int height = keyboardPanel->getPreferredHeight();

        int bestStart = 0;
        int bestTop = std::numeric_limits<int>::max();

        for (int start = 0; start + span <= unitCount; ++start)
        {
            int top = 0;
            for (int u = start; u < start + span; ++u)
                top = juce::jmax (top, columnBottom[(size_t) u]);

            if (top < bestTop)
            {
                bestTop = top;
                bestStart = start;
            }
        }

        const int x = gap + juce::roundToInt (unitWidth * (float) bestStart);
        keyboardPanel->setBounds (x, bestTop, width, height);

        for (int u = bestStart; u < bestStart + span; ++u)
            columnBottom[(size_t) u] = bestTop + height + gap;
    }

    int y = gap;
    for (auto bottom : columnBottom)
        y = juce::jmax (y, bottom);

    const int canvasHeight = y + gap;
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
    const int index = characterBox.getSelectedItemIndex();
    const int superRandom = RandomPreset::NumCharacters;   // the last item in the list

    if (index == superRandom)
    {
        RandomPreset::superRandomise (processor.getState(), rng);
        patchName.setText (RandomPreset::wildName (rng), juce::dontSendNotification);
        return;
    }

    const auto character = (RandomPreset::Character)
        juce::jlimit (0, (int) RandomPreset::Anything, index);

    RandomPreset::generate (processor.getState(), character, rng);
    patchName.setText (RandomPreset::nameFor (character, rng), juce::dontSendNotification);
}

void AquaVibrioEditor::loadBankFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Load a preset", juce::File{}, "*.json");

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
                    "That doesn't look like an Aqua Vibrio preset.");
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
    const int loaded = processor.loadBank (file);

    patchList.clear (juce::dontSendNotification);
    for (int i = 0; i < loaded; ++i)
        patchList.addItem (juce::String (i + 1).paddedLeft ('0', 3) + "  "
                             + processor.getPatchName (i), i + 1);

    const int index = processor.getCurrentPatchIndex();
    if (index >= 0)
    {
        patchList.setSelectedItemIndex (index, juce::dontSendNotification);
        patchName.setText (processor.getCurrentPatchName(), juce::dontSendNotification);
    }
}

} // namespace aquavibrio
