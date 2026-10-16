#pragma once
//==============================================================================
//  AquaVibrioNoteLockView.h
//
//  The Note Lock strip: a full-range keyboard (C-1 .. G9, middle C = C4)
//  that shows which keys play their own layer, numbered, and lets you choose
//  the next set of keys to change.
//
//      click / drag        select keys (the first key decides select or not)
//      shift-click         select the range from the last key clicked
//      alt-click           return a key to the base patch
//      right-click         the same, and more, from a menu
//      click a number      select that whole layer, to rework it
//==============================================================================

#include <JuceHeader.h>
#include "AquaVibrioLookAndFeel.h"
#include "AquaVibrioNoteLayerManager.h"
#include "AquaVibrioFactory.h"
#include "AquaVibrioRandom.h"
#include "AquaVibrioPatch.h"
#include "AquaVibrioAndroidFiles.h"

namespace aquavibrio
{

// One colour per layer number, spread around the hue circle by the golden
// angle so neighbouring numbers never look alike.
inline juce::Colour layerColour (int number)
{
    const float hue = std::fmod (0.93f + 0.618034f * (float) (number - 1), 1.0f);
    return juce::Colour::fromHSV (hue, 0.55f, 0.97f, 1.0f);
}

//==============================================================================
class NoteLockKeyboard : public juce::Component,
                         public juce::TooltipClient
{
public:
    explicit NoteLockKeyboard (NoteLayerManager& m) : manager (m) {}

    static constexpr int kLaneHeight = 20;

    void paint (juce::Graphics& g) override
    {
        const auto selection = manager.getSelection();
        std::array<int, 128> numbers {};
        for (int n = 0; n < 128; ++n)
            numbers[(size_t) n] = manager.numberForNote (n);

        const auto keys = keyArea();
        g.setColour (Theme::trackEmpty);
        g.fillRect (getLocalBounds().toFloat());

        // white keys
        for (int n = 0; n < 128; ++n)
        {
            if (isBlack (n))
                continue;

            const auto r = keyRect (n);
            juce::Colour fill = juce::Colour (0xffdfeee6);
            if (numbers[(size_t) n] > 0)
                fill = fill.interpolatedWith (layerColour (numbers[(size_t) n]), 0.55f);
            if (selection[(size_t) n])
                fill = Theme::accent;
            if (n == hoverNote)
                fill = fill.brighter (0.15f);

            g.setColour (fill);
            g.fillRect (r.reduced (0.5f, 0.0f));

            if (n % 12 == 0)
            {
                g.setColour (Theme::background.withAlpha (0.75f));
                g.setFont (juce::Font (juce::FontOptions (8.5f)));
                g.drawText (layers::noteName (n), r.withTrimmedTop (r.getHeight() - 12.0f), juce::Justification::centred, false);
            }
        }

        // black keys
        for (int n = 0; n < 128; ++n)
        {
            if (! isBlack (n))
                continue;

            const auto r = keyRect (n);
            juce::Colour fill = juce::Colour (0xff0f1d16);
            if (numbers[(size_t) n] > 0)
                fill = layerColour (numbers[(size_t) n]).darker (0.9f);
            if (selection[(size_t) n])
                fill = Theme::accentDim;
            if (n == hoverNote)
                fill = fill.brighter (0.25f);

            g.setColour (fill);
            g.fillRoundedRectangle (r, 1.5f);
            g.setColour (Theme::panelEdge);
            g.drawRoundedRectangle (r, 1.5f, 0.8f);
        }

        // the number lane: white keys on the lower row, black keys above
        g.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("Bold")));
        for (int n = 0; n < 128; ++n)
        {
            if (numbers[(size_t) n] == 0)
                continue;

            const auto pill = numberRect (n);
            const auto colour = layerColour (numbers[(size_t) n]);
            const bool overwritten = selection[(size_t) n];   // this selection will take it over

            g.setColour (colour.withAlpha (overwritten ? 0.25f : 1.0f));
            g.fillRoundedRectangle (pill, 3.0f);
            g.setColour (Theme::background.withAlpha (overwritten ? 0.5f : 1.0f));
            g.drawText (juce::String (numbers[(size_t) n]), pill, juce::Justification::centred, false);
        }

        g.setColour (Theme::panelEdge);
        g.drawRect (keys);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int n = noteAt (e.position);
        if (n != hoverNote)
        {
            hoverNote = n;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverNote = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const int number = numberAt (e.position); number > 0 && ! e.mods.isPopupMenu())
        {
            manager.selectLayer (number);
            return;
        }

        const int n = noteAt (e.position);
        if (n < 0)
            return;

        if (e.mods.isPopupMenu())
        {
            showMenu (n);
            return;
        }

        if (e.mods.isAltDown())
        {
            NoteLayerManager::Notes one;
            one.set ((size_t) n);
            manager.resetToBase (one);
            return;
        }

        if (e.mods.isShiftDown() && lastClicked >= 0)
        {
            auto sel = manager.getSelection();
            for (int k = juce::jmin (n, lastClicked); k <= juce::jmax (n, lastClicked); ++k)
                sel.set ((size_t) k);
            manager.setSelection (sel);
            lastClicked = n;
            return;
        }

        dragSelects = ! manager.isSelected (n);
        manager.setSelected (n, dragSelects);
        lastClicked = lastDragged = n;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown() || e.mods.isShiftDown())
            return;

        const int n = noteAt (e.position);
        if (n >= 0 && n != lastDragged)
        {
            manager.setSelected (n, dragSelects);
            lastDragged = n;
        }
    }

    juce::String getTooltip() override
    {
        if (hoverNote < 0)
            return "Click or drag to select keys. Shift-click selects a range, alt-click returns a key "
                   "to the base patch, a number selects its whole layer.";

        const int number = manager.numberForNote (hoverNote);
        return layers::noteName (hoverNote) + (number > 0 ? "  -  layer " + juce::String (number)
                                                          : "  -  base patch");
    }

private:
    static bool isBlack (int n)
    {
        const int pc = n % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    static int whiteIndex (int n)
    {
        static const int whitesBefore[] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
        return (n / 12) * 7 + whitesBefore[n % 12];
    }

    juce::Rectangle<float> keyArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop ((float) kLaneHeight);
    }

    float whiteWidth() const { return keyArea().getWidth() / 75.0f; }   // 128 keys hold 75 white ones

    juce::Rectangle<float> keyRect (int n) const
    {
        const auto area = keyArea();
        const float w = whiteWidth();

        if (! isBlack (n))
            return { area.getX() + w * (float) whiteIndex (n), area.getY(), w, area.getHeight() };

        const float bw = w * 0.62f;
        const float x = area.getX() + w * (float) (whiteIndex (n - 1) + 1) - bw * 0.5f;
        return { x, area.getY(), bw, area.getHeight() * 0.6f };
    }

    juce::Rectangle<float> numberRect (int n) const
    {
        const auto key = keyRect (n);
        const float h = (float) kLaneHeight * 0.5f - 1.0f;
        const float w = juce::jmax (key.getWidth() + (isBlack (n) ? 4.0f : -1.0f), 15.0f);
        const float y = isBlack (n) ? 0.5f : (float) kLaneHeight * 0.5f;
        return { key.getCentreX() - w * 0.5f, y, w, h };
    }

    int noteAt (juce::Point<float> p) const
    {
        if (! keyArea().contains (p))
            return -1;
        for (int n = 0; n < 128; ++n)
            if (isBlack (n) && keyRect (n).contains (p))
                return n;
        for (int n = 0; n < 128; ++n)
            if (! isBlack (n) && keyRect (n).contains (p))
                return n;
        return -1;
    }

    int numberAt (juce::Point<float> p) const
    {
        if (p.y >= (float) kLaneHeight)
            return 0;
        for (int n = 0; n < 128; ++n)
            if (const int number = manager.numberForNote (n); number > 0 && numberRect (n).contains (p))
                return number;
        return 0;
    }

    void showMenu (int n)
    {
        const int number = manager.numberForNote (n);
        const auto name = layers::noteName (n);

        juce::PopupMenu m;
        m.addSectionHeader (name + (number > 0 ? "  (layer " + juce::String (number) + ")" : "  (base patch)"));
        m.addItem (1, manager.isSelected (n) ? "Deselect " + name : "Select " + name);
        m.addItem (2, "Return " + name + " to the base patch", number > 0 || manager.isSelected (n));
        if (number > 0)
        {
            m.addItem (3, "Select all of layer " + juce::String (number));
            m.addItem (4, "Return all of layer " + juce::String (number) + " to the base patch");
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this, n, number] (int result)
            {
                NoteLayerManager::Notes one;
                one.set ((size_t) n);

                if (result == 1) manager.setSelected (n, ! manager.isSelected (n));
                if (result == 2) manager.resetToBase (one);
                if (result == 3) manager.selectLayer (number);
                if (result == 4)
                    for (const auto& l : manager.getLayers())
                        if (l.number == number)
                            manager.resetToBase (l.notes);
            });
    }

    NoteLayerManager& manager;
    int hoverNote { -1 }, lastClicked { -1 }, lastDragged { -1 };
    bool dragSelects { true };
};

//==============================================================================
class NoteLockStrip : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    // `strength` is how far Random assign moves things (the header's choice).
    NoteLockStrip (juce::AudioProcessorValueTreeState& state, NoteLayerManager& m,
                   std::function<Mutator::Strength()> strength)
        : apvts (state), manager (m), keyboard (m), currentStrength (std::move (strength))
    {
        addAndMakeVisible (keyboard);

        status.setColour (juce::Label::textColourId, Theme::textLabel);
        status.setFont (juce::Font (juce::FontOptions (12.5f)));
        status.setJustificationType (juce::Justification::centredLeft);
        status.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (status);

        // The one action that finishes a selection reads as the primary one.
        selectButton.setToggleState (true, juce::dontSendNotification);
        selectButton.setClickingTogglesState (false);
        selectButton.setTooltip ("Keep it: what you changed becomes a numbered layer, the knobs go back "
                                 "to the whole patch, and you can pick the next notes.");
        selectButton.onClick = [this] { manager.finishSelection(); };

        patchButton.setTooltip ("Give the selected notes a different patch altogether: a random "
                                "character, a factory preset or a preset file.");
        patchButton.onClick = [this] { showPatchMenu(); };

        randomButton.setTooltip ("Selected notes: one shared mutation, one number. Nothing selected: "
                                 "every key gets a mutation and a number of its own.");
        randomButton.onClick = [this] { manager.randomAssign (currentStrength()); };

        resetButton.setTooltip ("Return the selected notes to the base patch.");
        resetButton.onClick = [this] { manager.resetSelectionToBase(); };

        cancelButton.setTooltip ("Forget this selection and what was changed in it.");
        cancelButton.onClick = [this] { manager.cancelSelection(); };

        clearButton.setTooltip ("Every key plays the base patch again.");
        clearButton.onClick = [this] { manager.clearAll(); };

        for (auto* b : { &selectButton, &patchButton, &randomButton, &resetButton, &cancelButton, &clearButton })
            addChildComponent (*b);

        manager.addChangeListener (this);
        refresh();
        startTimerHz (8);
    }

    ~NoteLockStrip() override
    {
        manager.removeChangeListener (this);
    }

    static constexpr int kDesignHeight = 108;

    void paint (juce::Graphics& g) override
    {
        AquaVibrioLookAndFeel::drawSectionPanel (g, getLocalBounds(), "Note Lock", Theme::accent);
    }

    // The header bar carries the status on the left and only the actions
    // that make sense right now on the right; the keyboard gets the rest.
    void resized() override
    {
        auto header = getLocalBounds().removeFromTop (Theme::headerHeight).reduced (6, 3);
        header.removeFromLeft (98);   // the title, drawn by paint()

        for (auto* b : { &randomButton, &clearButton, &cancelButton, &resetButton, &patchButton, &selectButton })
        {
            if (! b->isVisible())
                continue;
            const int w = b == &selectButton ? 104 : 98;
            b->setBounds (header.removeFromRight (w));
            header.removeFromRight (4);
        }
        status.setBounds (header.withTrimmedRight (8));

        keyboard.setBounds (getLocalBounds().withTrimmedTop (Theme::headerHeight + 3).reduced (8, 0).withTrimmedBottom (7));
    }

    void refresh()
    {
        const bool sel = manager.hasSelection();
        const auto layersNow = manager.getLayers();
        const juce::String arrow (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")), dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));

        juce::String text;
        if (sel)
        {
            const auto notes = manager.describeSelection();
            const auto number = "layer " + juce::String (manager.upcomingNumber());
            text = manager.selectionIsEdited() ? "Editing " + notes + arrow + number
                                               : notes + dot + "turn knobs, Mutate or pick a patch" + arrow + number;
        }
        else if (layersNow.empty())
        {
            text = "From the patch you have loaded, only the notes you select differ. Click keys to begin.";
        }
        else
        {
            text = juce::String ((int) layersNow.size()) + (layersNow.size() == 1 ? " layer" : " layers")
                 + dot + "click keys to select, a number to rework its layer, alt-click to reset a key";
        }

        status.setText (text, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, sel ? Theme::accent : Theme::textLabel);

        // Only what applies now is shown.
        const bool layersExist = ! layersNow.empty();
        bool changed = false;
        const auto show = [&changed] (juce::Component& c, bool visible)
        {
            if (c.isVisible() != visible) { c.setVisible (visible); changed = true; }
        };
        show (selectButton, sel);
        show (patchButton, sel);
        show (resetButton, sel);
        show (cancelButton, sel);
        show (clearButton, ! sel && layersExist);
        show (randomButton, true);
        if (changed)
            resized();

        keyboard.repaint();
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override
    {
        // turning knobs changes the status without a message
        if (manager.hasSelection())
            refresh();
    }

    void showPatchMenu()
    {
        juce::PopupMenu random, factory;

        const auto names = RandomPreset::characterNames();
        for (int i = 0; i < names.size(); ++i)
            random.addItem (1000 + i, names[i]);

        juce::String category;
        juce::PopupMenu sub;
        for (size_t i = 0; i < factoryPresets().size(); ++i)
        {
            const auto& p = factoryPresets()[i];
            if (category != p.category)
            {
                if (category.isNotEmpty())
                    factory.addSubMenu (category, sub);
                sub = juce::PopupMenu();
                category = p.category;
            }
            sub.addItem (2000 + (int) i, p.name);
        }
        if (category.isNotEmpty())
            factory.addSubMenu (category, sub);

        juce::PopupMenu m;
        m.addSectionHeader ("A different patch for " + manager.describeSelection());
        m.addSubMenu ("Random character", random);
        m.addSubMenu ("Factory presets", factory);
        m.addItem (3000, "Load preset file...");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patchButton),
                         [this] (int result) { applyPatchChoice (result); });
    }

    void applyPatchChoice (int result)
    {
        if (result >= 1000 && result < 2000)
        {
            const int index = result - 1000;
            manager.loadWholePatch ([this, index]
            {
                if (index >= (int) RandomPreset::NumCharacters)
                    RandomPreset::superRandomise (apvts, rng);
                else
                    RandomPreset::generate (apvts, (RandomPreset::Character) index, rng);
            }, {});
        }
        else if (result >= 2000 && result < 3000)
        {
            const auto& preset = factoryPresets()[(size_t) (result - 2000)];
            manager.loadWholePatch ([this, &preset] { applyFactoryPreset (preset, apvts); }, {});
        }
        else if (result == 3000)
        {
            chooser = std::make_unique<juce::FileChooser> ("A preset for the selected notes", juce::File{}, "*.json");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                   #if JUCE_ANDROID
                    // a content:// document has no path: read it as a stream
                    juce::MemoryBlock data;
                    juce::String name;
                    if (manager.hasSelection() && androidfiles::read (fc, data, name))
                    {
                        const auto json = data.toString();
                        manager.loadWholePatch ([this, json] { applyJsonPreset (json, apvts); }, {});
                    }
                   #else
                    const auto file = fc.getResult();
                    if (file.existsAsFile() && manager.hasSelection())
                    {
                        const auto json = file.loadFileAsString();
                        manager.loadWholePatch ([this, json] { applyJsonPreset (json, apvts); }, {});
                    }
                   #endif
                });
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    NoteLayerManager& manager;
    NoteLockKeyboard keyboard;
    std::function<Mutator::Strength()> currentStrength;

    juce::Label status;
    juce::TextButton selectButton { "Select notes" }, patchButton { "Different patch" },
                     randomButton { "Random assign" }, resetButton { "Reset to base" },
                     cancelButton { "Cancel" }, clearButton { "Clear all" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Random rng;
};

} // namespace aquavibrio
