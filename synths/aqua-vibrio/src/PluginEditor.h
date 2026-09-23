#pragma once

#include <JuceHeader.h>
#include "AquaVibrioParameters.h"
#include "AquaVibrioRelevance.h"
#include "AquaVibrioLookAndFeel.h"
#include "AquaVibrioPanelLayout.h"
#include "AquaVibrioFactory.h"
#include "PluginProcessor.h"

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
    ParamControl (juce::AudioProcessorValueTreeState& state, const ParamInfo& info, juce::Colour tint)
        : paramInfo (info), apvts (state)
    {
        caption.setText (shortLabel (info), juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setColour (juce::Label::textColourId, Theme::textLabel);
        caption.setFont (juce::Font (juce::FontOptions (10.5f)));
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);

        if (info.scale == ValueScale::Enum)
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

        if (combo != nullptr)
            combo->setBounds (r.removeFromTop (juce::jmin (22, r.getHeight())).reduced (2, 1));
        else
        {
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmax (46, r.getWidth()), 14);
            slider->setBounds (r);
        }
    }

    bool isWide() const { return combo != nullptr; }

    // Dim the control when the current mode switches its effect off, and say
    // why in the tooltip (rules in AquaVibrioRelevance.h).
    void refreshActive()
    {
        const auto reason = inactiveReason (paramInfo.id, [this] (const char* otherId)
        {
            auto* v = apvts.getRawParameterValue (otherId);
            return v != nullptr ? juce::roundToInt (v->load()) : 0;
        });

        if (reason == lastReason)
            return;
        lastReason = reason;

        setAlpha (reason.isEmpty() ? 1.0f : 0.38f);
        const juce::String tip = reason.isEmpty() ? juce::String (paramInfo.displayName)
                                                  : juce::String (paramInfo.displayName) + "\n(inactive: " + reason + ")";
        if (slider != nullptr) slider->setTooltip (tip);
        if (combo != nullptr)  combo->setTooltip (tip);
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
    juce::String lastReason { "?" };
    juce::Label caption;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
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
    explicit StepGrid (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        startTimerHz (20);
        setTooltip ();
    }

    void paint (juce::Graphics& g) override
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
    bool turningOn { true };
};

//==============================================================================
// One boxed section of the panel. Flows its controls into a grid, dropdowns
// taking a full row so their text is readable.
//==============================================================================
class RegionPanel : public juce::Component,
                    private juce::Timer
{
public:
    RegionPanel (juce::AudioProcessorValueTreeState& state, const RegionSlot& s, bool drawFrame = true)
        : slot (s), tint (Theme::sectionTint (s.regionId)), framed (drawFrame)
    {
        if (juce::String (slot.regionId) == "arp")
        {
            stepGrid = std::make_unique<StepGrid> (state);
            addAndMakeVisible (*stepGrid);
        }

        for (auto* info : parametersInRegion (slot.regionId))
        {
            // The arpeggiator's 96 step bytes are a pattern editor, not knobs.
            // They stay in the APVTS and out of the panel.
            if (juce::String (info->name).startsWith ("Step "))
                continue;

            auto c = std::make_unique<ParamControl> (state, *info, tint);
            addAndMakeVisible (*c);
            controls.push_back (std::move (c));
        }

        timerCallback();
        startTimerHz (5);   // mode switches can come from presets and automation too
    }

    void timerCallback() override
    {
        for (auto& c : controls)
            c->refreshActive();
    }

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
    int layOut (juce::Rectangle<int> r, bool apply)
    {
        const int headerHeight = framed ? Theme::headerHeight - 2 : 0;
        r.removeFromTop (headerHeight);

        // A module keeps the column count the layout table gives it. Its
        // width is chosen to match, so the knobs never stretch and every
        // block of the same width lines up with its neighbours.
        const int cols = juce::jmax (1, slot.knobColumns);
        const int cellW = juce::jmax (52, r.getWidth() / cols);
        const int knobH = 62;
        const int comboH = 38;

        int x = r.getX(), y = r.getY(), rowHeight = 0;

        for (auto& c : controls)
        {
            const bool wide = c->isWide();

            // A dropdown always takes the full row width so its text is
            // never squeezed next to a knob - it starts a fresh row if one
            // isn't already under way.
            const int w = wide ? r.getWidth() : cellW;
            const int h = wide ? comboH : knobH;

            const bool mustWrap = wide ? (x > r.getX())
                                        : (x > r.getX() && x + w > r.getRight());
            if (mustWrap)
            {
                x = r.getX();
                y += rowHeight;
                rowHeight = 0;
            }

            if (apply)
                c->setBounds (x, y, w, h);

            // The row's height has to account for every control placed in
            // it - including this one - before anything advances past it,
            // or a short dropdown ending a row that also held a taller knob
            // would let the next row start under that knob's still-visible
            // bottom edge.
            rowHeight = juce::jmax (rowHeight, h);
            x += w;

            if (wide)
            {
                x = r.getX();
                y += rowHeight;
                rowHeight = 0;
            }
        }

        if (stepGrid != nullptr)
        {
            y += rowHeight + 6;
            rowHeight = 46;

            if (apply)
                stepGrid->setBounds (r.getX(), y, r.getWidth(), rowHeight);
        }

        contentHeight = (y + rowHeight) - r.getY() + headerHeight + 10;
        return contentHeight;
    }

    RegionSlot slot;
    juce::Colour tint;
    bool framed { true };
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
                bool ownSelector = true)
        : title (groupName), tint (Theme::sectionTint (members.front()->regionId)),
          hasOwnSelector (ownSelector)
    {
        for (auto* m : members)
        {
            auto panel = std::make_unique<RegionPanel> (state, *m, false);
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
class KeyboardPanel : public juce::Component
{
public:
    explicit KeyboardPanel (juce::MidiKeyboardState& state)
        : keyboard (state, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        // Four and a bit octaves, starting around C2, with the rest reachable
        // by the keyboard's own built-in scroll arrows.
        keyboard.setAvailableRange (24, 108);
        keyboard.setLowestVisibleKey (36);
        keyboard.setOctaveForMiddleC (4);
        keyboard.setKeyWidth (15.0f);

        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, Theme::textBright);
        keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colours::limegreen);
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::panelEdge);
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::accentGlow.withAlpha (0.3f));
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent);
        keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, Theme::background);
        keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);

        addAndMakeVisible (keyboard);
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
        keyboard.setBounds (r.reduced (kPad));
    }

    static constexpr int kSpanUnits      = 4;   // how many masonry units wide, like a module
    static constexpr int kHeaderHeight   = Theme::headerHeight;
    static constexpr int kPad            = 8;
    static constexpr int kKeyboardHeight = 90;

private:
    juce::MidiKeyboardComponent keyboard;
};

//==============================================================================
class AquaVibrioEditor : public juce::AudioProcessorEditor
{
public:
    explicit AquaVibrioEditor (AquaVibrioProcessor& p);
    ~AquaVibrioEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
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
    juce::TextButton applyButton { "Apply" };

    // Drives the Matrix Slot group card, whose own selector is hidden - the
    // six slots share one card and this picks which is shown, from the top
    // bar instead of inside the card.
    juce::ComboBox matrixSlotSelector;
    GroupPanel* matrixSlotGroup { nullptr };   // owned by `cards`; not deleted here
    juce::TextButton loadButton { "Load" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton prevButton { "<" };
    juce::TextButton nextButton { ">" };

    // Randomiser: a character to aim for, and the button that rolls it.
    juce::ComboBox characterBox;
    juce::TextButton randomButton { "Randomise" };
    juce::Random rng;
    void randomise();

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
    juce::Component headerControls;
    static constexpr int kHeaderDesignWidth = 1330;
    static constexpr int kHeaderHeight = 46;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquaVibrioEditor)
};

} // namespace aquavibrio
