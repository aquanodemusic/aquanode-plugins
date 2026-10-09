#pragma once

#include <JuceHeader.h>
#include "AquaVibrioParameters.h"
#include "AquaVibrioRelevance.h"
#include "AquaVibrioLookAndFeel.h"
#include "AquaVibrioPanelLayout.h"
#include "AquaVibrioFactory.h"
#include "PluginProcessor.h"
#include "AquaVibrioNoteLockView.h"
#include "AquaVibrioLabels.h"

namespace aquavibrio
{

//==============================================================================
// One labelled control: a knob, a dropdown or a button, chosen from the
// parameter's own description. Nothing here is hand-written per parameter -
// all 434 of them come out of the table.
//==============================================================================
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& state, const ParamInfo& info, juce::Colour tint,
                  NoteLayerManager* noteLayers = nullptr)
        : paramInfo (info), apvts (state), layerManager (noteLayers),
          paramIndex (layers::indexOf (info.id)), isGlobal (layers::isGlobal (info.id))
    {
        const auto label = panelLabel (info.id);
        caption.setText (label.isNotEmpty() ? label : shortLabel (info), juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setColour (juce::Label::textColourId, Theme::textLabel);
        caption.setFont (juce::Font (juce::FontOptions (10.5f)));
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);

        if (info.scale != ValueScale::Enum && info.minValue == 0 && info.maxValue == 1)
        {
            // A 0/1 parameter is a switch, not a knob that reads "0" or "1".
            toggle = std::make_unique<juce::TextButton>();
            toggle->setClickingTogglesState (true);
            toggle->setTooltip (info.displayName);
            toggle->onStateChange = [this] { toggle->setButtonText (toggle->getToggleState() ? "On" : "Off"); };
            addAndMakeVisible (*toggle);
            toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, info.id, *toggle);
            toggle->setButtonText (toggle->getToggleState() ? "On" : "Off");
        }
        else if (info.scale == ValueScale::Enum)
        {
            combo = std::make_unique<juce::ComboBox>();
            auto choices = choicesFor (info.choiceList);
            const int wanted = info.maxValue - info.minValue + 1;
            while (choices.size() > wanted) choices.remove (choices.size() - 1);
            combo->addItemList (choices, 1);
            combo->setTooltip (info.displayName);
            addAndMakeVisible (*combo);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                state, info.id, *combo);
        }
        else
        {
            slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                     juce::Slider::TextBoxBelow);
            // Values read in real units ("1.20 kHz", "350 ms"), so the box
            // spans the cell; typing "2 s" or "440 Hz" snaps to the nearest.
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
            slider->setColour (juce::Slider::rotarySliderFillColourId, tint);
            slider->getProperties().set ("bipolar",
                info.scale == ValueScale::Bipolar || info.scale == ValueScale::BipolarPercent);
            slider->setTooltip (info.displayName);
            addAndMakeVisible (*slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, info.id, *slider);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        caption.setBounds (r.removeFromTop (13));

        if (toggle != nullptr)
            toggle->setBounds (r.withSizeKeepingCentre (juce::jmin (54, r.getWidth() - 8), 22).translated (0, -4));
        else if (combo != nullptr)
            combo->setBounds (r.removeFromTop (juce::jmin (22, r.getHeight())).reduced (2, 1));
        else
        {
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmax (46, r.getWidth()), 14);
            slider->setBounds (r);
        }
    }

    bool isWide() const { return combo != nullptr; }

    // Dim the control when the current mode switches its effect off, and say
    // why in the tooltip (rules in AquaVibrioRelevance.h). While notes are
    // selected, the global controls belong to the base patch and are locked,
    // and the knobs the selection has changed are outlined.
    void refreshActive()
    {
        const bool selecting = layerManager != nullptr && layerManager->hasSelection();
        const bool locked = selecting && isGlobal;
        const bool changed = selecting && ! locked && layerManager->isChangedInSelection (paramIndex);

        if (changed != changedInSelection)
        {
            changedInSelection = changed;
            repaint();
        }

        auto reason = inactiveReason (paramInfo.id, [this] (const char* otherId)
        {
            auto* v = apvts.getRawParameterValue (otherId);
            return v != nullptr ? juce::roundToInt (v->load()) : 0;
        });

        if (locked)
            reason = "global - it always follows the whole patch, so it is locked while notes are selected";

        if (reason == lastReason && locked == lastLocked)
            return;
        lastReason = reason;
        lastLocked = locked;

        setAlpha (reason.isEmpty() ? 1.0f : 0.38f);
        if (slider != nullptr) slider->setEnabled (! locked);
        if (combo != nullptr)  combo->setEnabled (! locked);
        if (toggle != nullptr) toggle->setEnabled (! locked);

        const juce::String tip = reason.isEmpty() ? juce::String (paramInfo.displayName)
                                                  : juce::String (paramInfo.displayName) + "\n(inactive: " + reason + ")";
        if (slider != nullptr) slider->setTooltip (tip);
        if (combo != nullptr)  combo->setTooltip (tip);
        if (toggle != nullptr) toggle->setTooltip (tip);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! changedInSelection)
            return;

        // a soft ring: "the selected notes have their own value here"
        g.setColour (Theme::accentGlow.withAlpha (0.85f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 5.0f, 1.4f);
    }

private:
    // The table's display names are written for a tooltip, not a 60 pixel
    // caption: "Oscillator 1 Detune In Semitones" has to become "Semitone".
    // Strip the block name the panel already tells you, then shorten.
    static juce::String shortLabel (const ParamInfo& info)
    {
        juce::String s (info.name);

        for (auto prefix : { "Osc1 ", "Osc2 ", "Osc3 ", "Osc ", "LFO1 ", "LFO2 ", "LFO3 ",
                             "Filter1 ", "Filter2 ", "Filter ", "Arp ", "Arpeggiator/",
                             "Delay ", "Reverb ", "Chorus ", "Phaser ", "Vocoder ",
                             "Distortion ", "Input ", "Eq ", "EQ " })
            if (s.startsWith (prefix))
                s = s.fromFirstOccurrenceOf (prefix, false, false);

        s = s.fromLastOccurrenceOf ("/", false, false);
        s = s.replace ("Envelope", "Env").replace ("Amount", "Amt")
             .replace ("Velocity", "Vel").replace ("Keyfollow", "Key Fol")
             .replace ("Destination", "Dest").replace ("Assign", "Asgn");

        return s;
    }

    const ParamInfo& paramInfo;
    juce::AudioProcessorValueTreeState& apvts;
    NoteLayerManager* layerManager { nullptr };
    int paramIndex { -1 };
    bool isGlobal { false };
    bool changedInSelection { false }, lastLocked { false };
    juce::String lastReason { "?" };
    juce::Label caption;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::TextButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
};

//==============================================================================
// The arpeggiator's 32-step user pattern. Ninety-six parameters that would be
// meaningless as knobs: a click toggles a step, a vertical drag sets its
// velocity, and the shaded width of each bar is its note length.
//==============================================================================
class StepGrid : public juce::Component,
                 public juce::SettableTooltipClient,
                 private juce::Timer
{
public:
    explicit StepGrid (juce::AudioProcessorValueTreeState& s, NoteLayerManager* noteLayers = nullptr)
        : state (s), layerManager (noteLayers)
    {
        startTimerHz (20);
        setTooltip ();
    }

    // The arp is global: while notes are selected it belongs to the whole
    // patch and is locked, like every other global control.
    bool isLocked() const { return layerManager != nullptr && layerManager->hasSelection(); }

    void paint (juce::Graphics& g) override
    {
        // dimmed like every other control that is off or locked
        const bool dim = isLocked() || valueOf ("ArpMode", 0) == 0;
        if (dim)
            g.beginTransparencyLayer (0.38f);
        paintGrid (g);
        if (dim)
            g.endTransparencyLayer();
    }

    void paintGrid (juce::Graphics& g)
    {
        auto r = getLocalBounds().reduced (1);
        const float stepW = (float) r.getWidth() / 32.0f;
        const int length = valueOf ("ArpeggiatorUserpatternlength", 15) + 1;

        for (int i = 0; i < 32; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + stepW * (float) i, (float) r.getY(),
                                                      stepW - 1.0f, (float) r.getHeight());

            const bool beyondEnd = i >= length;
            const bool on = valueOf (id (i, "Bitfield"), 0) > 0;
            const float velocity = (float) valueOf (id (i, "Velocity"), 100) / 127.0f;
            const float noteLength = (float) valueOf (id (i, "Length"), 64) / 127.0f;

            g.setColour (Theme::trackEmpty.withAlpha (beyondEnd ? 0.35f : 1.0f));
            g.fillRect (cell);

            if (on && ! beyondEnd)
            {
                auto bar = cell.withTrimmedTop (cell.getHeight() * (1.0f - velocity));
                bar = bar.withWidth (juce::jmax (2.0f, bar.getWidth() * (0.25f + noteLength * 0.75f)));

                g.setGradientFill ({ Theme::accentGlow, 0.0f, bar.getY(),
                                     Theme::accent, 0.0f, bar.getBottom(), false });
                g.fillRect (bar);
            }

            // beat markers every four steps
            if (i % 4 == 0)
            {
                g.setColour (Theme::textDim.withAlpha (0.5f));
                g.fillRect (cell.getX(), (float) r.getY(), 1.0f, 3.0f);
            }
        }

        g.setColour (Theme::panelEdge);
        g.drawRect (getLocalBounds(), 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isLocked())
            return;

        const int step = stepAt (e.x);

        if (e.mods.isShiftDown())
        {
            setLengthFromY (step, e.y);
            return;
        }

        // A click toggles the step; the toggle decides what a drag does next,
        // so turning a run of steps on is one gesture rather than 32.
        turningOn = valueOf (id (step, "Bitfield"), 0) == 0;
        setValue (id (step, "Bitfield"), turningOn ? 1 : 0);

        if (turningOn)
            setVelocityFromY (step, e.y);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isLocked())
            return;

        const int step = stepAt (e.x);

        if (e.mods.isShiftDown())
        {
            setLengthFromY (step, e.y);
            return;
        }
        setValue (id (step, "Bitfield"), turningOn ? 1 : 0);

        if (turningOn)
            setVelocityFromY (step, e.y);
    }

private:
    void timerCallback() override { repaint(); }

    void setTooltip()
    {
        juce::SettableTooltipClient::setTooltip ("User pattern (Arp Pattern 0): click to toggle a step, "
                                                 "drag up or down for velocity. Shift-drag up or down sets "
                                                 "the step's note length (the bar's width).");
    }

    static juce::String id (int step, const char* what)
    {
        return "Step" + juce::String (step + 1) + what;
    }

    int stepAt (int x) const
    {
        return juce::jlimit (0, 31, (int) ((float) x / (float) juce::jmax (1, getWidth()) * 32.0f));
    }

    int valueOf (const juce::String& paramId, int fallback) const
    {
        if (auto* v = state.getRawParameterValue (paramId))
            return juce::roundToInt (v->load());
        return fallback;
    }

    void setValue (const juce::String& paramId, int value)
    {
        if (auto* p = state.getParameter (paramId))
        {
            const auto range = p->getNormalisableRange();
            p->setValueNotifyingHost (range.convertTo0to1 ((float) value));
        }
    }

    void setLengthFromY (int step, int y)
    {
        const float v = 1.0f - juce::jlimit (0.0f, 1.0f, (float) y / (float) juce::jmax (1, getHeight()));
        setValue (id (step, "Length"), juce::roundToInt (v * 127.0f));
    }

    void setVelocityFromY (int step, int y)
    {
        const float v = 1.0f - juce::jlimit (0.0f, 1.0f, (float) y / (float) juce::jmax (1, getHeight()));
        setValue (id (step, "Velocity"), juce::roundToInt (v * 127.0f));
    }

    juce::AudioProcessorValueTreeState& state;
    NoteLayerManager* layerManager { nullptr };
    bool turningOn { true };
};

//==============================================================================
// One boxed section of the panel. Flows its controls into a grid, dropdowns
// taking a full row so their text is readable.
//==============================================================================
class RegionPanel : public juce::Component,
                    private juce::Timer,
                    private juce::ChangeListener
{
public:
    RegionPanel (juce::AudioProcessorValueTreeState& state, const RegionSlot& s, bool drawFrame = true,
                 NoteLayerManager* noteLayers = nullptr)
        : slot (s), tint (Theme::sectionTint (s.regionId)), framed (drawFrame)
    {
        if (juce::String (slot.regionId) == "arp")
        {
            stepGrid = std::make_unique<StepGrid> (state, noteLayers);
            addAndMakeVisible (*stepGrid);
        }

        for (auto* info : parametersInRegion (slot.regionId))
        {
            // The arpeggiator's 96 step bytes are a pattern editor, not knobs.
            // They stay in the APVTS and out of the panel.
            if (juce::String (info->name).startsWith ("Step "))
                continue;

            auto c = std::make_unique<ParamControl> (state, *info, tint, noteLayers);
            addAndMakeVisible (*c);
            controls.push_back (std::move (c));
        }

        // Selectors first, then the knobs: a dropdown in the middle of the
        // knobs breaks their grid and strands a knob on a row of its own.
        std::stable_partition (controls.begin(), controls.end(),
                               [] (const std::unique_ptr<ParamControl>& c) { return c->isWide(); });

        refreshControls();
        startTimerHz (6);   // mode switches can come from presets and automation too

        // Opening or closing a note selection locks and unlocks controls at
        // once, rather than at the next tick.
        if (noteLayers != nullptr)
        {
            layerManager = noteLayers;
            layerManager->addChangeListener (this);
        }
    }

    ~RegionPanel() override
    {
        if (layerManager != nullptr)
            layerManager->removeChangeListener (this);
    }

    // Hidden panels (the other members of a group card) skip the work and
    // catch up the moment they are shown.
    void timerCallback() override
    {
        if (isShowing())
            refreshControls();
    }

    void visibilityChanged() override
    {
        if (isVisible())
            refreshControls();
    }

    void refreshControls()
    {
        for (auto& c : controls)
            c->refreshActive();
        if (stepGrid != nullptr)
            stepGrid->repaint();
    }

    // Opening or closing a selection is rare and changes what is locked:
    // every panel catches up at once, hidden or not.
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshControls(); }

    void paint (juce::Graphics& g) override
    {
        // Inside a group card the surrounding card draws the frame and the
        // header, so the member only draws its controls.
        if (framed)
            AquaVibrioLookAndFeel::drawSectionPanel (g, getLocalBounds(), slot.title, tint);
    }

    const RegionSlot& getSlot() const { return slot; }

    void resized() override
    {
        layOut (getLocalBounds().reduced (4), true);
    }

    // How tall this panel needs to be to show every control at the given
    // width, header bar included. The editor asks each panel in a row and
    // takes the tallest, which is the only way a block like LFO 1 - sixteen
    // parameters in four columns - stops being cut off at the bottom.
    int preferredHeightForWidth (int width)
    {
        return layOut (juce::Rectangle<int> (0, 0, juce::jmax (40, width - 8), 10000), false);
    }

    int getContentHeight() const { return contentHeight; }


private:
    // Flows the controls into a grid and returns the height used. With
    // 'apply' false nothing is moved - it is the same pass used purely as a
    // measurement, so the measuring and the placing can never disagree.
    //
    // Selectors sit two to a row (an odd one out takes the whole row), then
    // the knobs fill a grid of the module's own column count.
    int layOut (juce::Rectangle<int> r, bool apply)
    {
        const int headerHeight = framed ? Theme::headerHeight - 2 : 0;
        r.removeFromTop (headerHeight);

        const int cols = juce::jmax (1, slot.knobColumns);
        const int cellW = juce::jmax (52, r.getWidth() / cols);
        constexpr int knobH = 64, comboH = 40, rowGap = 5;

        std::vector<ParamControl*> combos, knobs;
        for (auto& c : controls)
            (c->isWide() ? combos : knobs).push_back (c.get());

        int y = r.getY();

        for (size_t i = 0; i < combos.size(); i += 2)
        {
            const bool pair = i + 1 < combos.size();
            const int w = pair ? r.getWidth() / 2 : r.getWidth();
            if (apply)
            {
                combos[i]->setBounds (r.getX(), y, w, comboH);
                if (pair)
                    combos[i + 1]->setBounds (r.getX() + w, y, r.getWidth() - w, comboH);
            }
            y += comboH;
        }

        if (! combos.empty() && ! knobs.empty())
            y += rowGap + 2;

        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int col = (int) i % cols;
            if (col == 0 && i > 0)
                y += knobH + rowGap;
            if (apply)
                knobs[i]->setBounds (r.getX() + col * cellW, y, cellW, knobH);
        }
        if (! knobs.empty())
            y += knobH;

        if (stepGrid != nullptr)
        {
            y += 8;
            if (apply)
                stepGrid->setBounds (r.getX(), y, r.getWidth(), 46);
            y += 46;
        }

        contentHeight = (y - r.getY()) + headerHeight + 12;
        return contentHeight;
    }

    RegionSlot slot;
    juce::Colour tint;
    bool framed { true };
    NoteLayerManager* layerManager { nullptr };
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::unique_ptr<StepGrid> stepGrid;
    int contentHeight { 60 };
};

//==============================================================================
// The on-screen keyboard, in the same card chrome as every parameter panel
// and dropped into the same masonry grid as one - not stretched full width,
// just another card that lands wherever the packing leaves the most room.
// It plays notes into the engine and, because it shares the processor's
// MidiKeyboardState with processBlock, lights up for notes played from the
// host or a hardware controller too - it's a window onto the same buffer
// either way, not a separate note source.
//==============================================================================
// Several panels of the same kind stacked into one card, with a selector in
// the header: the three oscillators, the two filters, the four envelopes, the
// six matrix slots, and the whole effects rack.
//
// The card is sized for whichever member needs the most room, so choosing a
// smaller one (Osc 3 after Osc 1, say) leaves empty space rather than
// reflowing the entire panel around it.
//==============================================================================
class GroupPanel : public juce::Component
{
public:
    // ownSelector = false is for a group whose picker lives in the top bar
    // instead of the card's own header - the matrix slots, so the six slots
    // share the panel space that used to hold six separate cards.
    GroupPanel (juce::AudioProcessorValueTreeState& state,
                const juce::String& groupName,
                const std::vector<const RegionSlot*>& members,
                bool ownSelector = true,
                NoteLayerManager* noteLayers = nullptr)
        : title (groupName), tint (Theme::sectionTint (members.front()->regionId)),
          hasOwnSelector (ownSelector)
    {
        for (auto* m : members)
        {
            auto panel = std::make_unique<RegionPanel> (state, *m, false, noteLayers);
            addChildComponent (*panel);
            selector.addItem (m->title, selector.getNumItems() + 1);
            panels.push_back (std::move (panel));
        }

        selector.setSelectedItemIndex (0, juce::dontSendNotification);
        selector.onChange = [this] { showPanel (selector.getSelectedItemIndex()); };

        if (hasOwnSelector)
            addAndMakeVisible (selector);

        showPanel (0);
    }

    // Lets an external combobox (in the top bar) drive which member shows.
    juce::ComboBox& getExternalSelector() { return selector; }

    void paint (juce::Graphics& g) override
    {
        AquaVibrioLookAndFeel::drawSectionPanel (g, getLocalBounds(), title, tint);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);

        if (hasOwnSelector)
        {
            // The selector sits in the header bar itself, inset by a pixel so
            // the bar's edge still reads as a frame around it.
            auto header = r.removeFromTop (Theme::headerHeight - 4).reduced (0, 1);
            selector.setBounds (header.withTrimmedLeft (juce::jmax (64, header.getWidth() / 3)));
            r.removeFromTop (4);
        }
        else
        {
            r.removeFromTop (Theme::headerHeight - 4);   // just the title, drawn by paint()
        }

        for (auto& p : panels)
            p->setBounds (r);
    }

    // The card has to fit its largest member, whichever one is on show.
    int preferredHeightForWidth (int width)
    {
        int tallest = 0;
        for (auto& p : panels)
            tallest = juce::jmax (tallest, p->preferredHeightForWidth (width - 8));

        return tallest + Theme::headerHeight + (hasOwnSelector ? 8 : 4);
    }

    int widestColumns() const
    {
        int widest = 1;
        for (auto& p : panels)
            widest = juce::jmax (widest, p->getSlot().knobColumns);
        return widest;
    }

private:
    void showPanel (int index)
    {
        for (size_t i = 0; i < panels.size(); ++i)
            panels[i]->setVisible ((int) i == index);
    }

    juce::String title;
    juce::Colour tint;
    bool hasOwnSelector;
    juce::ComboBox selector;
    std::vector<std::unique_ptr<RegionPanel>> panels;
};

//==============================================================================
class KeyboardPanel : public juce::Component,
                      private juce::ChangeListener
{
public:
    KeyboardPanel (juce::MidiKeyboardState& state, NoteLayerManager& noteLayers)
        : layers (noteLayers), keyboard (state, noteLayers)
    {
        // The whole MIDI range, C-1 to G9, laid out under the panel like the
        // Note Lock strip above it, so a key sits in the same place in both.
        keyboard.setAvailableRange (0, 127);
        keyboard.setLowestVisibleKey (0);
        keyboard.setScrollButtonsVisible (false);
        keyboard.setOctaveForMiddleC (4);

        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, Theme::textBright);
        keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colours::limegreen);
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::panelEdge);
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::accentGlow.withAlpha (0.3f));
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent);
        keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, Theme::background);
        keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);

        addAndMakeVisible (keyboard);
        layers.addChangeListener (this);
    }

    ~KeyboardPanel() override
    {
        layers.removeChangeListener (this);
    }

    int getPreferredHeight() const noexcept
    {
        return kHeaderHeight + kPad + kKeyboardHeight + kPad;
    }

    void paint (juce::Graphics& g) override
    {
        AquaVibrioLookAndFeel::drawSectionPanel (g, getLocalBounds(), "Keyboard", Theme::accent);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (kHeaderHeight);
        r = r.reduced (kPad);
        keyboard.setKeyWidth ((float) r.getWidth() / 75.0f);   // 128 keys hold 75 white ones
        keyboard.setBounds (r);
        keyboard.setLowestVisibleKey (0);
    }

    static constexpr int kHeaderHeight   = Theme::headerHeight;
    static constexpr int kPad            = 8;
    static constexpr int kKeyboardHeight = 84;

private:
    // A key that plays a layer of its own is underlined in that layer's
    // colour; keys in the selection being edited, in the accent.
    struct LayerKeyboard : public juce::MidiKeyboardComponent
    {
        LayerKeyboard (juce::MidiKeyboardState& s, NoteLayerManager& m)
            : juce::MidiKeyboardComponent (s, horizontalKeyboard), manager (m) {}

        void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override
        {
            juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);
            stripe (note, g, area.reduced (2.0f, 0.0f).removeFromBottom (5.0f));   // under the key, clear of the black ones
        }

        void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour fill) override
        {
            juce::MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, fill);
            stripe (note, g, area.reduced (1.5f, 0.0f).removeFromBottom (5.0f));
        }

        void stripe (int note, juce::Graphics& g, juce::Rectangle<float> r)
        {
            if (manager.isSelected (note))
                g.setColour (Theme::accent);
            else if (const int number = manager.numberForNote (note); number > 0)
                g.setColour (layerColour (number));
            else
                return;
            g.fillRect (r);
        }

        NoteLayerManager& manager;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { keyboard.repaint(); }

    NoteLayerManager& layers;
    LayerKeyboard keyboard;
};

//==============================================================================
class AquaVibrioEditor : public juce::AudioProcessorEditor,
                         private juce::ChangeListener
{
public:
    explicit AquaVibrioEditor (AquaVibrioProcessor& p);
    ~AquaVibrioEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // for the test harness's screenshots
    void showNoteLock (bool show) { noteLockButton.setToggleState (show, juce::sendNotificationSync); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshMutationButtons();
    Mutator::Strength currentStrength() const
    {
        return (Mutator::Strength) juce::jlimit (0, 2, strengthBox.getSelectedItemIndex());
    }

    void loadBankFile();
    void savePatchFile();
    void refreshPatchListAfter (const juce::File& file);

    AquaVibrioProcessor& processor;
    AquaVibrioLookAndFeel lookAndFeel;

    // header
    juce::Label title;
    juce::Label patchName;
    juce::ComboBox patchList;

    // Factory presets, grouped by category with headings in the list.
    juce::ComboBox factoryList;

    juce::TextButton loadButton { "Load" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton prevButton { "<" };
    juce::TextButton nextButton { ">" };

    // Randomiser: a character to aim for, and the button that rolls it.
    juce::ComboBox characterBox;
    juce::TextButton randomButton { "Randomise" };
    juce::Random rng;
    void randomise();

    // Mutator: nudges the feel of the patch (or of the selected notes), with
    // a strength and a history of generations to step back and forth in.
    juce::TextButton mutateButton { "Mutate" };
    juce::ComboBox strengthBox;
    juce::ArrowButton historyBack { "back", 0.5f, Theme::accent }, historyForward { "forward", 0.0f, Theme::accent };

    // Note Lock: the keyboard strip for giving notes their own sound.
    juce::TextButton noteLockButton { "Note Lock" };
    std::unique_ptr<NoteLockStrip> noteLockStrip;

    // A card is either a single panel or a group of them behind a selector;
    // the masonry only needs its width in grid units and its height.
    struct Card
    {
        std::unique_ptr<juce::Component> component;
        int span { 3 };
        std::function<int (int)> preferredHeight;
    };

    std::vector<Card> cards;
    std::unique_ptr<KeyboardPanel> keyboardPanel;

    // The panel is laid out ONCE at a fixed design width and then the whole
    // thing is scaled to the window, so resizing zooms the plugin instead of
    // re-flowing it: modules keep their proportions and their neighbours, and
    // knobs never change size relative to their card. Anything that does not
    // fit vertically is scrolled to.
    juce::Viewport viewport;
    juce::Component canvasHolder;
    juce::Component canvas;
    static constexpr int kCanvasWidth = 1180;

    // The header row is laid out at its own design width and scaled the same
    // way, so its controls shrink together instead of overlapping.
    // The top bar's controls, with a thin rule between each group of them.
    struct HeaderStrip : public juce::Component
    {
        void setRules (std::vector<int> x) { rules = std::move (x); repaint(); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::comboBorder.withAlpha (0.55f));
            for (int x : rules)
                g.fillRect ((float) x, 10.0f, 1.0f, (float) getHeight() - 20.0f);
        }
        std::vector<int> rules;
    };

    HeaderStrip headerControls;
    static constexpr int kHeaderDesignWidth = 1380;
    static constexpr int kHeaderHeight = 46;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquaVibrioEditor)
};

} // namespace aquavibrio
