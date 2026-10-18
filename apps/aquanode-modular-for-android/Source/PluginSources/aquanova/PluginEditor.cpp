#include "PluginEditor.h"
#include <vector>

namespace aquanova {

//==============================================================================
namespace
{
    /** Turns a spec name into something readable in a small cell. */
    juce::String shortLabel (const ParamDesc& d)
    {
        juce::String t (d.label);

        // The section header already says which oscillator, envelope or effect
        // this is, so the caption does not need to repeat it.
        for (auto prefix : { "Osc 1 ", "Osc 2 ", "Osc 3 ", "LFO 1 ", "LFO 2 ",
                             "Env 1 ", "Env 2 ", "Env 3 ", "Filter ", "Arp ",
                             "Noise ", "Chorus ", "Delay ", "Reverb ", "Comb Filter ",
                             "Distortion ", "Effects ", "Unison ", "Portamento ",
                             "Ring Mod ", "Panning ", "Special Filter ", "Pan ", "EQ " })
        {
            if (t.startsWith (prefix))
            {
                t = t.substring ((int) juce::String (prefix).length());
                break;
            }
        }

        t = t.replace ("Mod Knob ", "")
             .replace ("Aftertouch", "AT")
             .replace ("Intensity", "Int")
             .replace ("Hardness", "Hard")
             .replace ("Resonance", "Reso")
             .replace ("Frequency", "Freq")
             .replace ("Velocity", "Vel")
             .replace ("Tracking", "Trk")
             .replace ("Level", "Lvl")
             .replace ("Wh ", "Wheel ")
             .trim();

        return t.isEmpty() ? juce::String (d.label) : t;
    }
}

//==============================================================================
ParamControl::ParamControl (juce::AudioProcessorValueTreeState& state, const ParamDesc& desc)
{
    title = shortLabel (desc);

    auto* param = state.getParameter (desc.id);

    // The tooltip carries the full, unabbreviated name plus the range, which is
    // what the Hints toggle in the header surfaces.
    const juce::String hint = juce::String (desc.label) + "  ("
                                + (desc.bipolar ? juce::String ("-64 to +63")
                                                : juce::String (desc.min) + " to " + juce::String (desc.max))
                                + ")";

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (param))
    {
        combo = std::make_unique<juce::ComboBox>();
        combo->setTooltip (hint);

        // ComboBoxAttachment only syncs the index - the items are ours to add.
        combo->addItemList (choiceParam->choices, 1);

        addAndMakeVisible (*combo);
        comboAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                          state, desc.id, *combo);
    }
    else
    {
        slider = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
        slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                     juce::MathConstants<float>::pi * 2.8f, true);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 14);
        slider->setColour (juce::Slider::textBoxTextColourId, Palette::cyanBright());
        slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->getProperties().set ("bipolar", desc.bipolar);
        slider->getProperties().set ("isVolume", juce::String (desc.id) == "MasterVolumeLevel"
                                               || juce::String (desc.id) == "PartProgramVolume");
        slider->setTooltip (hint);
        addAndMakeVisible (*slider);
        sliderAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                           state, desc.id, *slider);
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (kCaptionH);

    if (combo != nullptr)
        combo->setBounds (r.reduced (4).withHeight (20));
    else
        slider->setBounds (r.reduced (2, 0));
}

void ParamControl::paint (juce::Graphics& g)
{
    g.setColour (Palette::textDim());
    g.setFont (juce::Font (juce::FontOptions (11.5f)));

    // minimumHorizontalScale of 1.0 makes JUCE wrap instead of squashing glyphs.
    g.drawFittedText (title, getLocalBounds().removeFromTop (kCaptionH).reduced (3, 2),
                      juce::Justification::centredTop, 2, 1.0f);
}

//==============================================================================
Section::Section (juce::String titleIn, juce::Colour accentIn, const juce::StringArray& bankNames)
    : title (std::move (titleIn)), accent (accentIn)
{
    hasBanks = bankNames.size() > 1;

    if (hasBanks)
    {
        bankSelector.addItemList (bankNames, 1);
        bankSelector.setSelectedId (1, juce::dontSendNotification);
        bankSelector.onChange = [this] { setBank (bankSelector.getSelectedItemIndex()); };
        addAndMakeVisible (bankSelector);
    }
}

void Section::add (juce::AudioProcessorValueTreeState& state, const ParamDesc& desc,
                   int bank, const juce::String& groupName)
{
    auto* c = new ParamControl (state, desc);
    controls.add (c);
    addAndMakeVisible (c);

    int groupIndex = groupNames.indexOf (groupName);
    if (groupIndex < 0)
    {
        groupNames.add (groupName);
        groupIndex = groupNames.size() - 1;
    }

    entries.add ({ c, bank, groupIndex, { } });
}

void Section::performLayout (int columns)
{
    columns = juce::jmax (1, columns);
    rules.clearQuick();
    captions.clearQuick();

    int maxBank = 0;
    for (const auto& e : entries)
        maxBank = juce::jmax (maxBank, e.bank);

    int tallest = 0;

    for (int bank = 0; bank <= maxBank; ++bank)
    {
        int y = 0;
        bool firstGroupInBank = true;

        for (int g = 0; g < groupNames.size(); ++g)
        {
            // Collect this bank's controls for this group, in order.
            juce::Array<int> indices;
            for (int i = 0; i < entries.size(); ++i)
                if (entries.getReference (i).bank == bank && entries.getReference (i).group == g)
                    indices.add (i);

            if (indices.isEmpty())
                continue;

            if (! firstGroupInBank)
            {
                rules.add ({ bank, y + 2 });
                y += 7;
            }
            firstGroupInBank = false;

            if (groupNames[g].isNotEmpty())
            {
                captions.add ({ bank, g, 2, y });
                y += kGroupHeader;
            }

            int x = 0;
            for (int idx : indices)
            {
                entries.getReference (idx).bounds =
                    { x * ParamControl::kWidth, y, ParamControl::kWidth, ParamControl::kHeight };

                if (++x >= columns)
                {
                    x = 0;
                    y += ParamControl::kHeight;
                }
            }

            if (x > 0)
                y += ParamControl::kHeight;
        }

        tallest = juce::jmax (tallest, y);
    }

    preferredWidth  = columns * ParamControl::kWidth + kPad * 2;
    preferredHeight = kHeaderHeight + kPad + tallest + kPad;

    setBank (currentBank);
}

void Section::setBank (int newBank)
{
    currentBank = juce::jmax (0, newBank);

    for (const auto& e : entries)
        e.control->setVisible (e.bank == currentBank);

    resized();
    repaint();
}

void Section::resized()
{
    if (hasBanks)
        bankSelector.setBounds (getWidth() - 104, 4, 96, kHeaderHeight - 8);

    const int ox = kPad;
    const int oy = kHeaderHeight + kPad;

    for (const auto& e : entries)
        if (e.bank == currentBank)
            e.control->setBounds (e.bounds.translated (ox, oy));
}

void Section::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    juce::ColourGradient grad (Palette::panel().brighter (0.10f), r.getX(), r.getY(),
                               Palette::panel(), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 7.0f);

    g.setColour (accent.withAlpha (0.75f));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.2f);

    auto header = r.removeFromTop ((float) kHeaderHeight);
    g.setColour (accent.withAlpha (0.25f));
    g.fillRoundedRectangle (header, 7.0f);
    g.fillRect (header.withTop (header.getBottom() - 7.0f));

    g.setColour (accent.brighter (0.5f));
    g.setFont (juce::Font (juce::FontOptions (14.5f, juce::Font::bold)));
    g.drawFittedText (title.toUpperCase(),
                      header.toNearestInt().reduced (10, 0).withTrimmedRight (hasBanks ? 108 : 0),
                      juce::Justification::centredLeft, 1);

    // Fine rules between groups, and the group captions themselves.
    const int ox = kPad;
    const int oy = kHeaderHeight + kPad;

    g.setColour (Palette::rule());
    for (const auto& rule : rules)
        if (rule.bank == currentBank)
            g.fillRect (ox, oy + rule.y, getWidth() - ox * 2, 1);

    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.setColour (Palette::textDim().withAlpha (0.9f));

    for (const auto& c : captions)
        if (c.bank == currentBank)
            g.drawText (groupNames[c.group].toUpperCase(),
                        ox + c.x, oy + c.y, getWidth() - ox * 2, kGroupHeader,
                        juce::Justification::centredLeft);
}

//==============================================================================
KeyboardCard::KeyboardCard (juce::MidiKeyboardState& state, juce::String titleIn, juce::Colour accentIn)
    : title (std::move (titleIn)), accent (accentIn),
      keyboard (state, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // Four and a bit octaves, starting around C2, with the rest reachable by
    // the keyboard's own built-in scroll arrows.
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setKeyWidth (15.0f);

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, Palette::textDim());
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, Palette::backgroundLo());
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Palette::panel());
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        Palette::cyan().withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Palette::cyan());
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, Palette::panel());
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);

    addAndMakeVisible (keyboard);
}

void KeyboardCard::resized()
{
    keyboard.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (kPad));
}

void KeyboardCard::paint (juce::Graphics& g)
{
    // Same chrome as Section::paint() - a card floating on the water, not a
    // hole cut into it - kept separate rather than shared, since this card
    // has no bank selector and no control grid to make room for.
    auto r = getLocalBounds().toFloat();

    juce::ColourGradient grad (Palette::panel().brighter (0.10f), r.getX(), r.getY(),
                               Palette::panel(), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 7.0f);

    g.setColour (accent.withAlpha (0.75f));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.2f);

    auto header = r.removeFromTop ((float) kHeaderHeight);
    g.setColour (accent.withAlpha (0.25f));
    g.fillRoundedRectangle (header, 7.0f);
    g.fillRect (header.withTop (header.getBottom() - 7.0f));

    g.setColour (accent.brighter (0.5f));
    g.setFont (juce::Font (juce::FontOptions (14.5f, juce::Font::bold)));
    g.drawFittedText (title.toUpperCase(), header.toNearestInt().reduced (10, 0),
                      juce::Justification::centredLeft, 1);
}

//==============================================================================
namespace
{
    bool has (const juce::String& n, std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (n.contains (w))
                return true;
        return false;
    }

    struct GroupRule
    {
        const char* name;
        std::function<bool (const juce::String&)> match;
    };

    struct SectionSpec
    {
        const char* title;
        juce::Colour (*accent)();
        juce::StringArray banks;
        /** Returns the bank a parameter belongs to, or -1 if it is not ours. */
        std::function<int (const juce::String&)> bankOf;
        std::vector<GroupRule> groups;
        const char* fallbackGroup;
    };

    int prefixBank (const juce::String& n, std::initializer_list<const char*> prefixes)
    {
        int i = 0;
        for (auto* p : prefixes)
        {
            if (n.startsWith (p))
                return i;
            ++i;
        }
        return -1;
    }
}

//==============================================================================
AquaNovaEditor::AquaNovaEditor (AquaNovaProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    viewport.setViewedComponent (&canvasHolder, false);
    viewport.setScrollBarsShown (true, false, true, false);
    addAndMakeVisible (viewport);
    canvasHolder.addAndMakeVisible (canvas);

    buildHeader();
    buildSections();
    buildKeyboardCard();
    layoutCanvas();

    processor.onPatchChanged = [this] { refreshPatchName(); };
    refreshPatchName();

    setResizable (true, true);
    setResizeLimits (820, 480, 3600, 2200);
    setSize (1200, 700);
}

AquaNovaEditor::~AquaNovaEditor()
{
    processor.onPatchChanged = nullptr;
    setLookAndFeel (nullptr);
}

//==============================================================================
void AquaNovaEditor::buildHeader()
{
    titleLabel.setText ("AQUANOVA", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (23.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, Palette::cyanBright());
    headerControls.addAndMakeVisible (titleLabel);

    patchNameEditor.setFont (juce::Font (juce::FontOptions (13.0f)));
    patchNameEditor.setJustification (juce::Justification::centredLeft);
    patchNameEditor.onReturnKey = [this]
    {
        processor.setPatchName (patchNameEditor.getText());
        patchNameEditor.giveAwayKeyboardFocus();
    };
    patchNameEditor.onFocusLost = [this] { processor.setPatchName (patchNameEditor.getText()); };
    headerControls.addAndMakeVisible (patchNameEditor);

    characterBox.addItemList (RandomPreset::characterNames(), 1);
    characterBox.setSelectedId ((int) RandomPreset::Pad + 1, juce::dontSendNotification);
    headerControls.addAndMakeVisible (characterBox);

    randomButton.onClick = [this] { randomise(); };
    loadButton.onClick   = [this] { loadPatch(); };
    saveButton.onClick   = [this] { savePatch(); };

    characterBox.setTooltip ("The character to generate. Super Random, at the bottom "
                             "of the list, randomises every parameter with only the "
                             "guards needed to keep the result audible.");

    for (auto* b : { &loadButton, &saveButton, &randomButton })
        headerControls.addAndMakeVisible (b);

    // Ours rather than the hardware's, so it sits in the header next to Hints
    // instead of in a panel: a leaky-integrator high pass on the output that
    // stops a DC offset building up. On unless it is switched off.
    dcBlockButton.setColour (juce::ToggleButton::textColourId, Palette::textDim());
    dcBlockButton.setColour (juce::ToggleButton::tickColourId, Palette::cyan());
    dcBlockButton.setTooltip ("Removes any DC offset from the output with a leaky "
                              "integrator, so offsets from asymmetric waves, distortion "
                              "or the feedback lines cannot stack up and eat headroom. "
                              "Switch it off only if you want the output exactly as the "
                              "voice path leaves it.");
    headerControls.addAndMakeVisible (dcBlockButton);
    dcBlockAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                        processor.apvts, kDcBlockId, dcBlockButton);

    // The speed that DC block runs at: slow is click-free (the default),
    // fast reclaims headroom sooner but can clack on a sudden DC-heavy onset.
    dcSpeedLabel.setText ("Vel", juce::dontSendNotification);
    dcSpeedLabel.setFont (juce::Font (juce::FontOptions (7.0f)));
    dcSpeedLabel.setColour (juce::Label::textColourId, Palette::textDim());
    dcSpeedLabel.setJustificationType (juce::Justification::centredRight);
    headerControls.addAndMakeVisible (dcSpeedLabel);

    dcSpeedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    dcSpeedSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    dcSpeedSlider.setColour (juce::Slider::textBoxTextColourId, Palette::cyanBright());
    dcSpeedSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    dcSpeedSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    dcSpeedSlider.setColour (juce::Slider::trackColourId, Palette::cyan());
    dcSpeedSlider.setColour (juce::Slider::backgroundColourId, Palette::panelAlt());
    dcSpeedSlider.setColour (juce::Slider::thumbColourId, Palette::cyanBright());
    dcSpeedSlider.setTooltip ("How fast DC Block cancels an offset. Slow (left) is"
                              "click-free, fast (right) is quicker but occasionally"
                              "clacky.");
    headerControls.addAndMakeVisible (dcSpeedSlider);
    dcSpeedAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        processor.apvts, kDcSpeedId, dcSpeedSlider);

    tooltipButton.setColour (juce::ToggleButton::textColourId, Palette::textDim());
    tooltipButton.setColour (juce::ToggleButton::tickColourId, Palette::cyan());
    tooltipButton.setToggleState (true, juce::dontSendNotification);
    tooltipButton.onClick = [this] { updateTooltips(); };
    headerControls.addAndMakeVisible (tooltipButton);

    // Performance data, not a patch parameter: no APVTS attachment, so it is
    // not saved with the patch and does not show up on Undo. Moving it sends
    // an ordinary MIDI CC1 straight to the engine, exactly as a hardware mod
    // wheel would, so it behaves identically to whatever a host or a
    // connected keyboard would send.
    modWheelLabel.setText ("Mod", juce::dontSendNotification);
    modWheelLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    modWheelLabel.setColour (juce::Label::textColourId, Palette::textDim());
    modWheelLabel.setJustificationType (juce::Justification::centredRight);
    headerControls.addAndMakeVisible (modWheelLabel);

    modWheelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    modWheelSlider.setRange (0.0, 127.0, 1.0);
    modWheelSlider.setValue (0.0, juce::dontSendNotification);
    modWheelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 20);
    modWheelSlider.setColour (juce::Slider::textBoxTextColourId, Palette::cyanBright());
    modWheelSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    modWheelSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    modWheelSlider.setColour (juce::Slider::trackColourId, Palette::cyan());
    modWheelSlider.setColour (juce::Slider::backgroundColourId, Palette::panelAlt());
    modWheelSlider.setColour (juce::Slider::thumbColourId, Palette::cyanBright());
    modWheelSlider.setTooltip ("The mod wheel (MIDI CC1). Drag it here when you are not "
                               "playing from a controller that has one - it reaches the "
                               "same modulation routings either way. Change Osc Pitch "
                               "Wheels to activate");
    modWheelSlider.onValueChange = [this]
    {
        processor.setModWheelFromUI ((float) (modWheelSlider.getValue() / 127.0));
    };
    headerControls.addAndMakeVisible (modWheelSlider);

    addAndMakeVisible (headerControls);
    updateTooltips();
}

void AquaNovaEditor::updateTooltips()
{
    // Creating the window switches hints on; destroying it switches them off.
    if (tooltipButton.getToggleState())
    {
        if (tooltipWindow == nullptr)
            tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 600);
    }
    else
    {
        tooltipWindow.reset();
    }
}

//==============================================================================
void AquaNovaEditor::buildSections()
{
    const std::vector<SectionSpec> specs
    {
        { "Oscillator", Palette::cyan, { "Osc 1", "Osc 2", "Osc 3" },
          [] (const juce::String& n) { return prefixBank (n, { "Osc1", "Osc2", "Osc3" }); },
          { { "Pitch",     [] (const juce::String& n) { return has (n, { "Octave", "Semitone", "FineTune", "Pitch", "BendRange", "Interval" }); } },
            { "Mix",       [] (const juce::String& n) { return has (n, { "Mix" }); } },
            { "Pulse Width", [] (const juce::String& n) { return has (n, { "Width" }) && ! has (n, { "Formant" }); } },
            { "Sync",      [] (const juce::String& n) { return has (n, { "Sync", "Skew", "Formant" }); } },
            { "Tone",      [] (const juce::String& n) { return has (n, { "Soften", "Hard", "Type", "Wave" }); } } },
          "More" },

        { "Mixer", Palette::aqua, { },
          [] (const juce::String& n)
          {
              return (n.startsWith ("Noise") || n.startsWith ("RingMod") || n.startsWith ("FM")
                       || n.startsWith ("ModKnob")) ? 0 : -1;
          },
          { { "Noise",    [] (const juce::String& n) { return n.startsWith ("Noise"); } },
            { "Ring Mod", [] (const juce::String& n) { return n.startsWith ("RingMod"); } },
            { "FM",       [] (const juce::String& n) { return n.startsWith ("FM"); } } },
          "Mod Depths" },

        { "Filter", Palette::sun, { },
          [] (const juce::String& n) { return (n.startsWith ("Filter") || n.startsWith ("SpecialFilter")) ? 0 : -1; },
          { { "Main",       [] (const juce::String& n) { return has (n, { "Cutoff", "Resonance", "Type", "Slope", "Overdrive", "QNormalise", "Width", "Tracking", "Bypass" }); } },
            { "Modulation", [] (const juce::String& n) { return has (n, { "Env", "LFO", "Wheel", "Aftertouch" }); } } },
          "More" },

        { "Envelope", Palette::aqua, { "Env 1", "Env 2", "Env 3" },
          [] (const juce::String& n) { return prefixBank (n, { "Env1", "Env2", "Env3" }); },
          { { "Shape",    [] (const juce::String& n) { return has (n, { "Attack", "Decay", "Sustain", "Release", "Delay" }) && ! has (n, { "SustainTime", "SustainRate" }); } },
            { "Response", [] (const juce::String& n) { return has (n, { "Velocity", "KeyTracking", "LevelTrack", "LevelNote" }); } },
            { "Extras",   [] (const juce::String& n) { return has (n, { "ADRepeat", "SustainTime", "SustainRate", "Trigger" }); } } },
          "More" },

        { "LFO", Palette::cyan, { "LFO 1", "LFO 2" },
          [] (const juce::String& n) { return prefixBank (n, { "LFO1", "LFO2" }); },
          { { "Shape",      [] (const juce::String& n) { return has (n, { "Type", "Speed", "Range", "Sync", "Phase", "Offset" }) && ! has (n, { "SpeedEnv", "SpeedWh", "SpeedAfter" }); } },
            { "Delay",      [] (const juce::String& n) { return has (n, { "Delay", "Fade", "Trigger" }); } },
            { "Modulation", [] (const juce::String& n) { return has (n, { "SpeedEnv", "SpeedWh", "SpeedAfter", "Soften" }); } } },
          "More" },

        { "Voice", Palette::sun, { },
          [] (const juce::String& n)
          {
              return has (n, { "Unison", "Portamento", "Glide", "PolyMode", "Polyphony",
                               "OscTriggerMode", "OSCsStartPhase", "VCODrift", "KbdTranspose",
                               "EnvsTriggering", "ConstantGate" }) ? 0 : -1;
          },
          { { "Unison",     [] (const juce::String& n) { return has (n, { "Unison" }); } },
            { "Portamento", [] (const juce::String& n) { return has (n, { "Portamento", "Glide" }); } } },
          "Behaviour" },

        { "Arpeggiator", Palette::coral, { },
          [] (const juce::String& n)
          {
              // ArpPatternSelect/ArpPatternBank are real hardware params for a
              // step-pattern arpeggiator this engine doesn't implement yet (it
              // only does the simple up/down/as-played ordering below) - hide
              // them rather than show a control that does nothing.
              if (n == "ArpPatternSelect" || n == "ArpPatternBank")
                  return -1;
              return n.startsWith ("Arp") ? 0 : -1;
          },
          { { "Timing",  [] (const juce::String& n) { return has (n, { "Speed", "Sync", "GateTime", "Quantise" }); } },
            { "Pattern", [] (const juce::String& n) { return has (n, { "Pattern", "Octave", "Ordering", "Latch", "Keysync" }); } } },
          "More" },

        { "Effects", Palette::coral,
          { "Distortion", "Chorus", "Delay", "Reverb", "Comb", "EQ", "Pan", "Vocoder", "Routing" },
          [] (const juce::String& n)
          {
              if (n.startsWith ("Distortion"))  return 0;
              if (n.startsWith ("Chorus") || n.startsWith ("ExtraChorus")) return 1;
              if (n.startsWith ("Delay"))       return 2;
              if (n.startsWith ("Reverb"))      return 3;
              if (n.startsWith ("CombFilter"))  return 4;
              if (n.startsWith ("EQ"))          return 5;
              if (n.startsWith ("Pan"))         return 6;
              if (n.startsWith ("Voc"))         return 7;
              if (n.startsWith ("Effects") || n.startsWith ("FxOrder")
                    || n.startsWith ("FxDigital") || n.startsWith ("MasterVolume")
                    || n.startsWith ("PartProgramVolume")) return 8;
              return -1;
          },
          { },
          "" }
    };

    std::vector<Section*> made;

    for (const auto& spec : specs)
    {
        auto* sec = new Section (spec.title, spec.accent(), spec.banks);
        sections.add (sec);
        canvas.addAndMakeVisible (sec);
        made.push_back (sec);
    }

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = kParams[i];
        const juce::String name (d.id);

        for (size_t s = 0; s < specs.size(); ++s)
        {
            const int bank = specs[s].bankOf (name);
            if (bank < 0)
                continue;

            juce::String group (specs[s].fallbackGroup);

            for (const auto& gr : specs[s].groups)
                if (gr.match (name))
                {
                    group = gr.name;
                    break;
                }

            made[s]->add (processor.apvts, d, bank, group);
            break;
        }

        // Anything that matches no section - vocoder parameters, drum map
        // entries, the MIDI stream slots - stays in the APVTS and stays
        // automatable, it simply has no knob on the panel.
    }

    // The grid inside each panel is worked out in layoutCanvas(), once the
    // column width is known.
}

void AquaNovaEditor::buildKeyboardCard()
{
    keyboardCard = std::make_unique<KeyboardCard> (processor.keyboardState, "Keyboard", Palette::coral());
    canvas.addAndMakeVisible (*keyboardCard);
}

//==============================================================================
void AquaNovaEditor::layoutCanvas()
{
    const int pad = 10;
    const int numColumns = 3;

    // Every panel is the same width and sits in one of three columns. Panels
    // keep their natural height and are dropped into whichever column is
    // currently shortest, so the canvas fills from the top with no stranded
    // panel on a row of its own and no dead space below a short one.
    const int columnWidth = (kCanvasWidth - pad * (numColumns + 1)) / numColumns;
    const int gridColumns = juce::jmax (2, (columnWidth - Section::kPad * 2)
                                              / ParamControl::kWidth);

    int columnBottom[numColumns];
    for (auto& c : columnBottom)
        c = pad;

    for (auto* s : sections)
    {
        if (s->numControls() == 0)
            continue;

        s->performLayout (gridColumns);

        int shortest = 0;
        for (int c = 1; c < numColumns; ++c)
            if (columnBottom[c] < columnBottom[shortest])
                shortest = c;

        const int x = pad + shortest * (columnWidth + pad);
        const int h = s->getPreferredHeight();

        s->setBounds (x, columnBottom[shortest], columnWidth, h);
        columnBottom[shortest] += h + pad;
    }

    // Not a Section - no bank, no control grid - so it does not go through
    // performLayout(), but it lands the same way: in whichever column the
    // panels above left shortest, which in the usual 3-column layout is the
    // bottom right.
    if (keyboardCard != nullptr)
    {
        int shortest = 0;
        for (int c = 1; c < numColumns; ++c)
            if (columnBottom[c] < columnBottom[shortest])
                shortest = c;

        const int x = pad + shortest * (columnWidth + pad);
        const int h = keyboardCard->getPreferredHeight();

        keyboardCard->setBounds (x, columnBottom[shortest], columnWidth, h);
        columnBottom[shortest] += h + pad;
    }

    int tallest = pad;
    for (auto c : columnBottom)
        tallest = juce::jmax (tallest, c);

    canvasHeight = tallest;
    canvas.setBounds (0, 0, kCanvasWidth, canvasHeight);
}

//==============================================================================
void AquaNovaEditor::refreshPatchName()
{
    patchNameEditor.setText (processor.getPatchName(), juce::dontSendNotification);
}

void AquaNovaEditor::randomise()
{
    const int index = characterBox.getSelectedItemIndex();

    // The entry after "Anything" is Super Random.
    if (index > (int) RandomPreset::Anything)
    {
        RandomPreset::superRandomise (processor.apvts, rng);
        processor.setPatchName (RandomPreset::wildName (rng));
        return;
    }

    const auto character = (RandomPreset::Character)
                              juce::jlimit (0, (int) RandomPreset::Anything, index);

    RandomPreset::generate (processor.apvts, character, rng);
    processor.setPatchName (RandomPreset::nameFor (character, rng));
}

void AquaNovaEditor::loadPatch()
{
    chooser = std::make_unique<juce::FileChooser> ("Load an AquaNova patch",
                                                   juce::File(), "*.aquanova");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        if (! processor.loadPatch (file))
            juce::NativeMessageBox::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon, "Could not read that patch",
                file.getFullPathName());
    });
}

void AquaNovaEditor::savePatch()
{
    chooser = std::make_unique<juce::FileChooser> ("Save AquaNova patch",
                                                   juce::File(), "*.aquanova");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;

        if (! file.hasFileExtension ("aquanova"))
            file = file.withFileExtension ("aquanova");

        if (! processor.savePatch (file))
            juce::NativeMessageBox::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon, "Could not write that patch",
                file.getFullPathName());
    });
}

//==============================================================================
void AquaNovaEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient grad (Palette::background(), 0.0f, (float) kHeaderHeight,
                               Palette::backgroundLo(), 0.0f, (float) getHeight(), false);
    g.setGradientFill (grad);
    g.fillAll();

    g.setColour (Palette::panel());
    g.fillRect (0, 0, getWidth(), kHeaderHeight);
    g.setColour (Palette::cyan().withAlpha (0.8f));
    g.drawHorizontalLine (kHeaderHeight - 1, 0.0f, (float) getWidth());
}

void AquaNovaEditor::resized()
{
    // Lay every header control out once at the fixed design width, in local
    // (0,0)-based coordinates - the container's own bounds and transform,
    // set below, take care of fitting it to the actual window.
    auto header = juce::Rectangle<int> (0, 0, kHeaderDesignWidth, kHeaderContentHeight);

    titleLabel.setBounds (header.removeFromLeft (132));
    header.removeFromLeft (8);
    patchNameEditor.setBounds (header.removeFromLeft (180).reduced (0, 1));
    header.removeFromLeft (6);
    loadButton.setBounds (header.removeFromLeft (58));
    header.removeFromLeft (4);
    saveButton.setBounds (header.removeFromLeft (58));
    header.removeFromLeft (16);
    characterBox.setBounds (header.removeFromLeft (150));
    header.removeFromLeft (4);
    randomButton.setBounds (header.removeFromLeft (92));
    header.removeFromLeft (16);
    modWheelLabel.setBounds (header.removeFromLeft (46));
    header.removeFromLeft (4);
    modWheelSlider.setBounds (header.removeFromLeft (130));

    tooltipButton.setBounds (header.removeFromRight (70));
    header.removeFromRight (6);
    dcBlockButton.setBounds (header.removeFromRight (92));
    header.removeFromRight (10);
    dcSpeedSlider.setBounds (header.removeFromRight (150));
    header.removeFromRight (4);
    dcSpeedLabel.setBounds (header.removeFromRight (44));

    // Fit that fixed-width row into however much width the window actually
    // has. On a window at least as wide as the design, this is 1:1 and the
    // row simply sits left-aligned with room to spare; on a narrower window
    // it shrinks as a whole, so nothing overlaps and no label truncates -
    // it just gets a little smaller, like zooming out.
    const int availableW = juce::jmax (1, getWidth() - 2 * kHeaderInsetX);
    const float headerScale = availableW < kHeaderDesignWidth
                                 ? (float) availableW / (float) kHeaderDesignWidth
                                 : 1.0f;

    headerControls.setTransform (juce::AffineTransform::scale (headerScale));
    headerControls.setBounds (kHeaderInsetX, kHeaderInsetY, kHeaderDesignWidth, kHeaderContentHeight);

    viewport.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (2));

    // Always fit the width; scroll vertically for whatever is left over.
    const float scale = (float) viewport.getMaximumVisibleWidth() / (float) kCanvasWidth;

    canvas.setTransform (juce::AffineTransform::scale (scale));
    canvasHolder.setSize (viewport.getMaximumVisibleWidth(),
                          juce::roundToInt ((float) canvasHeight * scale));
}

} // namespace aquanova