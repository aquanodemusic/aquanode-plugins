/*  PluginEditor.cpp  -  see PluginEditor.h.  GPLv3.  */
#include "PluginEditor.h"

using namespace vdx7ui;

//==============================================================================
// A file the user picked with a FileChooser. On desktop it's a plain File; on
// Android (target SDK 30+) the picker returns a document URL that must be read
// and written through AndroidDocument streams, not filesystem paths. This type
// hides that difference so the three SysEx dialogs below stay identical, and it
// is copyable so it can be carried through nested async callbacks (the
// read-modify-write in saveIntoBankFile needs the target to survive two menus).
namespace
{
    // ROM locations can be long absolute paths or, on Android, opaque document
    // URIs. Menus only need enough to tell two files apart.
    juce::String shortenPath (const juce::String& p)
    {
        auto name = p.fromLastOccurrenceOf ("/", false, false);
        if (name.isEmpty()) name = p;
        return name.length() > 28 ? name.getLastCharacters (28) : name;
    }

    struct PickedTarget
    {
       #if JUCE_ANDROID
        juce::URL url;

        bool valid() const { return ! url.isEmpty(); }
        juce::String name() const { return url.getFileName(); }
        juce::String source() const { return url.toString (true); }

        bool read (juce::MemoryBlock& dest) const
        {
            auto doc = juce::AndroidDocument::fromDocument (url);
            if (! doc.hasValue())
                return false;
            auto in = doc.createInputStream();
            if (in == nullptr)
                return false;
            dest.reset();
            in->readIntoMemoryBlock (dest);
            return dest.getSize() > 0;
        }

        bool write (const void* data, size_t size) const
        {
            auto doc = juce::AndroidDocument::fromDocument (url);
            if (! doc.hasValue())
                return false;
            auto out = doc.createOutputStream();
            if (out == nullptr)
                return false;
            return out->write (data, size);
        }
       #else
        juce::File file;

        bool valid() const { return file != juce::File{}; }
        juce::String name() const { return file.getFileName(); }
        juce::String source() const { return file.getFullPathName(); }

        bool read (juce::MemoryBlock& dest) const { return file.loadFileAsData (dest); }

        bool write (const void* data, size_t size) const
        {
            auto f = file;
            if (! f.hasFileExtension ("syx"))
                f = f.withFileExtension ("syx");
            return f.replaceWithData (data, size);
        }
       #endif

        // filename without extension, for display / bank naming
        juce::String nameNoExt() const { return name().upToLastOccurrenceOf (".", false, false); }
    };

    // What a knob returns to when double-clicked: its value in the INIT
    // voice - the very voice the INIT button loads (vdx7::Voice{}: a plain
    // sine on OP1 at full level, OP2-OP6 silent, flat envelopes, algorithm 1,
    // no transposition...). One definition, so the two can never disagree.
    double initVoiceValue (int vcedOffset)
    {
        static const vdx7::Voice init;
        return (double) init.get (vcedOffset);
    }

    PickedTarget pickedFrom (const juce::FileChooser& fc)
    {
        PickedTarget t;
       #if JUCE_ANDROID
        t.url = fc.getURLResult();
       #else
        t.file = fc.getResult();
       #endif
        return t;
    }
}

VDX7AudioProcessorEditor::VDX7AudioProcessorEditor (VDX7AudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p),
      keyboard (p.keyboardState, p.soundingNotes)
{
    setLookAndFeel (&lnf);

    // `content` is the only direct child of the editor; it holds the whole UI
    // at a fixed design resolution and gets scaled/centred to fit whatever
    // size the editor actually is (see resized()).
    addAndMakeVisible (content);
    content.setSize (kDesignW, kDesignH);

    // The emulator only produces sound across C1..C6, so constrain the
    // on-screen keyboard to that range (MIDI 24..84 with C1 = note 24).
    keyboard.setTitle ("On-screen keyboard");
    content.addAndMakeVisible (keyboard);

    title.setText ("VirtualDX7", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    title.getProperties().set (DXLookAndFeel::keepFontProperty(), true);   // keep 22 px bold
    title.setBorderSize (juce::BorderSize<int> (0, 2, 0, 0));
    title.setColour (juce::Label::textColourId, col::accent);
    title.setMinimumHorizontalScale (0.85f);   // squeeze a touch rather than ever show "..."
    content.addAndMakeVisible (title);

    // bank / program browser
    bankBox.setTitle ("Bank");
    progBox.setTitle ("Program");
    rebuildBankList();
    // A project saved with a voice ROM can name a bank that no longer exists.
    if (bankBox.indexOfItemId (processor.currentBank() + 1) >= 0)
        bankBox.setSelectedId (processor.currentBank() + 1, juce::dontSendNotification);
    bankBox.onChange = [this]{
        if (updatingUI) return;
        rebuildProgramList (true);
    };
    content.addAndMakeVisible (bankBox);

    rebuildProgramList (false);
    progBox.setSelectedId (processor.currentProg() + 1, juce::dontSendNotification);
    progBox.onChange = [this]{
        if (updatingUI) return;
        const int prog = progBox.getSelectedId() - 1;
        if (bankBox.getSelectedId() == kUserBankId) processor.selectUserProgram (prog);
        else processor.selectFactory (bankBox.getSelectedId() - 1, prog);
    };
    content.addAndMakeVisible (progBox);

    auto step = [this](int delta){
        int pr = processor.currentProg() + delta;
        if (pr < 0) pr = 31; else if (pr > 31) pr = 0;
        if (bankBox.getSelectedId() == kUserBankId) processor.selectUserProgram (pr);
        else processor.selectFactory (processor.currentBank(), pr);
    };
    prevBtn.onClick = [step]{ step (-1); };
    nextBtn.onClick = [step]{ step (+1); };
    prevBtn.setTitle ("Previous program"); prevBtn.setTooltip ("Previous program");
    nextBtn.setTitle ("Next program");     nextBtn.setTooltip ("Next program");
    initBtn.setTooltip ("Load a blank INIT voice");
    fileBtn.setTooltip ("Import / export SysEx (.syx)");

    initBtn.onClick = [this]{ processor.setVoice (vdx7::Voice{}); };
    fileBtn.onClick = [this]{
        juce::PopupMenu m;
        m.addItem (1, "Import bank (.syx)...");
        m.addSeparator();
        m.addItem (2, "Export current voice (.syx)...");
        m.addItem (3, "Export bank (.syx)...");
        m.addSeparator();
        m.addItem (4, "Save voice into existing bank (.syx)...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileBtn),
            [this](int choice){
                if      (choice == 1) importBankFromFile();
                else if (choice == 2) exportToFile (false);
                else if (choice == 3) exportToFile (true);
                else if (choice == 4) saveIntoBankFile();
            });
    };
    content.addAndMakeVisible (prevBtn);
    content.addAndMakeVisible (nextBtn);
    content.addAndMakeVisible (initBtn);
    content.addAndMakeVisible (fileBtn);

    // ---- ROM loading ----------------------------------------------------
    // No copyrighted data ships with the plugin, so the bit-accurate engine has
    // to be pointed at a firmware image before it can run. Until then the
    // native engine covers for it and this button is how the user upgrades.
    romBtn.setTitle ("ROM menu");
    romBtn.setTooltip ("Load the DX7 firmware ROM and factory voices");
    romBtn.onClick = [this]
    {
        const auto& roms = processor.roms();   // this instance's own ROM state

        juce::PopupMenu m;
        m.addSectionHeader (processor.engineDescription());
        m.addItem (1, "Load firmware ROM (dx7.bin)...");
        m.addItem (2, "Load factory voices (voices.bin)...");
        m.addSeparator();
        m.addItem (3, roms.hasFirmware() ? "Firmware: " + shortenPath (roms.firmwareSource())
                                         : juce::String ("Firmware: not loaded"), false, false);
        m.addItem (4, roms.hasVoices()   ? "Voices: "   + shortenPath (roms.voicesSource())
                                         : juce::String ("Voices: not loaded (starter bank)"), false, false);
        m.addSeparator();
        m.addItem (5, "Unload Firmware",       roms.hasFirmware());
        m.addItem (6, "Unload Factory Voices", roms.hasVoices());

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (romBtn),
            [this](int choice)
            {
                if      (choice == 1) loadRomFile (true);
                else if (choice == 2) loadRomFile (false);
                else if (choice == 5 || choice == 6)
                {
                    if (choice == 5) processor.unloadFirmware();
                    else             processor.unloadFactoryVoices();
                    rebuildBankList();
                    rebuildProgramList (false);
                    refreshAll();
                }
            });
    };
    content.addAndMakeVisible (romBtn);

    setupFunctionRow();
    refreshEngineStatus();


    // LCD wiring
    lcd.lcdProvider       = [this](char l1[17], char l2[17]){ processor.getLcd (l1, l2); };
    lcd.ledNumberProvider = [this]{ return processor.currentProg() + 1; };
    lcd.readyProvider     = [this]{ return processor.engineReady(); };
    content.addAndMakeVisible (lcd);

    // Binds a small knob / an on-off button to its APVTS parameter. The
    // attachment sets the knob's double-click value to the parameter's
    // default (which is just whatever the first starter patch happens to
    // use), so point it at the INIT voice's value instead, as everywhere else.
    SliderBinder bindSlider = [this] (juce::Slider& s, int off) {
        if (auto* param = processor.paramForOffset (off)) {
            attachments.push_back (std::make_unique<juce::SliderParameterAttachment> (*param, s));
            s.setDoubleClickReturnValue (true, initVoiceValue (off));
        }
    };
    ButtonBinder bindButton = [this] (juce::Button& b, int off) {
        if (auto* param = processor.paramForOffset (off))
            buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (*param, b));
    };

    // ALGORITHM card: its Algorithm / Feedback knobs and Key Sync switch, and
    // the routing diagram fed from the live voice.
    algoCard.bind (bindSlider, bindButton);
    algoCard.diagram.algoProvider     = [this]{ return processor.getVoiceCopy().get (vdx7::G_ALG); };
    algoCard.diagram.feedbackProvider = [this]{ return processor.getVoiceCopy().get (vdx7::G_FB); };
    algoCard.diagram.opLevelProvider  = [this]{
        auto v = processor.getVoiceCopy();
        std::array<int,6> a{};
        for (int i = 0; i < 6; ++i) a[(size_t) i] = v.getOp (5 - i, vdx7::OP_OL); // OP1..OP6
        return a;
    };
    content.addAndMakeVisible (algoCard);

    // slider factory shared by all panels.  Each knob is bound to its APVTS
    // parameter with a SliderParameterAttachment, so knob moves write to the
    // parameter (and thus reach the host + emulator) and incoming host
    // automation moves the knob - both directions, no manual callbacks.
    SliderFactory make =
        [this](const juce::String& cap, const juce::String& full,
               int mn, int mx, int off) -> ParamSlider*
    {
        auto* s = new ParamSlider (cap, full, mn, mx, off);
        if (auto* param = processor.paramForOffset (off)) {
            attachments.push_back (
                std::make_unique<juce::SliderParameterAttachment> (*param, s->getSlider()));
            // The attachment resets double-click to the parameter's default
            // (the first starter patch's value); double-click means "as in
            // the INIT voice" instead.
            s->getSlider().setDoubleClickReturnValue (true, initVoiceValue (off));
        }
        allSliders.push_back (s);
        return s;
    };

    // six operator panels, displayed OP1..OP6 (vced op index 5..0)
    for (int d = 1; d <= 6; ++d) {
        int vcedOp = 6 - d;
        auto panel = std::make_unique<OperatorPanel> (d, vcedOp, make);
        panel->setOutputLevelProvider ([this, d] { return (int) opLevel[(size_t) (d - 1)].getValue(); });
        content.addAndMakeVisible (*panel);
        ops.push_back (std::move (panel));
    }

    // Operator selector: six green buttons in two rows of three, only one
    // operator panel shown at a time, all sitting on their own OPERATORS card
    // (added first, so it is painted underneath them).
    content.addAndMakeVisible (opCard);
    for (int i = 0; i < 6; ++i) {
        auto& b = opSelect[(size_t) i];
        b.setButtonText ("OP " + juce::String (i + 1));
        b.setTitle ("Operator " + juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (9001);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        b.onClick = [this, i] { selectOperator (i); };
        b.setLookAndFeel (&compactLnf);
        content.addAndMakeVisible (b);
    }

    // Each operator's output level, as a small knob right beside its button,
    // so all six can be balanced without switching operators. Bound to the
    // same APVTS parameter the operator panel's knob used to be, so host
    // automation, undo and the emulator all see exactly the same thing.
    for (int i = 0; i < 6; ++i) {
        auto& k = opLevel[(size_t) i];
        const int off = (5 - i) * vdx7::kOpVcedStride + vdx7::OP_OL;   // OP1 lives in VCED block 5
        const juce::String name = "OP" + juce::String (i + 1) + " Output Level";
        k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.setRange (0.0, 99.0, 1.0);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setWantsKeyboardFocus (true);
        k.setTitle (name);
        k.setName (name);
        k.setTooltip (name + "  (0-99)");
        k.setLookAndFeel (&compactLnf);
        bindSlider (k, off);          // double-click = INIT voice: OP1 99, the others 0 (silent)
        content.addAndMakeVisible (k);
    }

    // Envelope scope under the selector. It reads the live voice, so knob
    // moves and host automation redraw it too; it zooms and scrolls itself.
    envView.voiceProvider     = [this] { return processor.getVoiceCopy(); };
    envView.onOperatorClicked = [this] (int op) { selectOperator (op); };
    content.addAndMakeVisible (envView);

    global = std::make_unique<GlobalPanel> (make, bindSlider, bindButton);
    content.addAndMakeVisible (*global);

    // FX page. Bound straight to the processor's APVTS, so nothing here needs
    // to route through the VCED model or the emulator.
    fxPanel = std::make_unique<FxPanel> (processor.apvts);
    content.addChildComponent (*fxPanel);   // hidden until the FX button is on

    fxBtn.setClickingTogglesState (true);
    fxBtn.setColour (juce::TextButton::buttonOnColourId, col::accent);
    fxBtn.setColour (juce::TextButton::textColourOnId,   col::bg);
    fxBtn.setTitle ("Effects page");
    fxBtn.setTooltip ("Show the global effects (chorus, delay, phaser, reverb)");
    fxBtn.onClick = [this] { setFxViewVisible (fxBtn.getToggleState()); };
    content.addAndMakeVisible (fxBtn);

    selectOperator (0);
    setFxViewVisible (false);
    processor.addChangeListener (this);

   #if JUCE_ANDROID
    // The standalone wrapper sizes us to the display. `content` is fixed at
    // its design resolution and letterboxed/pillarboxed to fit whatever
    // aspect ratio the device screen turns out to be (see resized()), so
    // nothing gets clipped on unusual aspect ratios. Wide limits so a large
    // phone/tablet screen is never clamped.
    setResizable (true, false);
    setResizeLimits (480, 320, 8192, 8192);
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        setSize (d->userArea.getWidth(), d->userArea.getHeight());
    else
        setSize (1180, 594);
   #else
    setResizable (true, true);
    setResizeLimits (960, 480, 2400, 1600);
    setSize (1180, 594);
   #endif
}

VDX7AudioProcessorEditor::~VDX7AudioProcessorEditor()
{
    processor.removeChangeListener (this);
    for (auto& b : opSelect) b.setLookAndFeel (nullptr);
    for (auto& k : opLevel)  k.setLookAndFeel (nullptr);
    fnWatchers.clear();
    for (auto& l : resetListeners) { monoBtn.removeMouseListener (l.get()); portaModeBtn.removeMouseListener (l.get()); }
    for (juce::Component* c : { (juce::Component*) &tuneBar, (juce::Component*) &portaTimeBar,
                                (juce::Component*) &pbBar, (juce::Component*) &monoBtn,
                                (juce::Component*) &portaModeBtn, (juce::Component*) &moreBtn,
                                (juce::Component*) &dcKnob })
        c->setLookAndFeel (nullptr);
    for (auto& k : ctlRange)  k.setLookAndFeel (nullptr);
    for (auto& b : ctlAssign) b.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

// The factory side of the bank selector. With a voice ROM loaded that is the
// eight cartridge banks followed by the bundled starter bank; without one there
// is only the starter bank, so offering eight identical copies of it would be
// nothing but noise.
void VDX7AudioProcessorEditor::rebuildBankList()
{
    const int keepUser = bankBox.indexOfItemId (kUserBankId);
    const juce::String userLabel = keepUser >= 0 ? bankBox.getItemText (keepUser)
                                                 : juce::String();
    const int previous = bankBox.getSelectedId();

    bankBox.clear (juce::dontSendNotification);

    const int numBanks = vdx7::usingStarterBank (processor.roms()) ? 1 : vdx7::kNumFactoryBanks;
    for (int b = 0; b < numBanks; ++b)
        bankBox.addItem (vdx7::factoryBankName (processor.roms(), b), b + 1);
    if (numBanks > 1)   // a voice ROM is loaded: keep the starter bank reachable too
        bankBox.addItem (vdx7::factoryBankName (processor.roms(), vdx7::kStarterBankIndex),
                         vdx7::kStarterBankIndex + 1);

    if (userLabel.isNotEmpty())
        bankBox.addItem (userLabel, kUserBankId);

    const int wanted = (bankBox.indexOfItemId (previous) >= 0) ? previous : 1;
    bankBox.setSelectedId (wanted, juce::dontSendNotification);
}

void VDX7AudioProcessorEditor::rebuildProgramList (bool sendSelect)
{
    const bool user = (bankBox.getSelectedId() == kUserBankId) && processor.hasUserBank();
    int bank = user ? 0 : juce::jmax (0, bankBox.getSelectedId() - 1);
    auto b = user ? processor.userBank() : vdx7::factoryBank (processor.roms(), bank);
    progBox.clear (juce::dontSendNotification);
    for (int i = 0; i < 32; ++i) {
        juce::String nm = juce::String (i + 1).paddedLeft ('0', 2) + " "
                        + juce::String (b.voiceName (i)).trim();
        progBox.addItem (nm, i + 1);
    }
    if (sendSelect) {
        progBox.setSelectedId (1, juce::dontSendNotification);
        if (user) processor.selectUserProgram (0);
        else      processor.selectFactory (bank, 0);
    }
}

void VDX7AudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshAll();
}

// Points the plugin at a firmware ROM or a factory voice set. Both go through
// the same picker; only the size checks and the destination differ, and those
// live in RomStore so that the same validation applies however a file arrives.
void VDX7AudioProcessorEditor::loadRomFile (bool firmware)
{
    chooser = std::make_unique<juce::FileChooser> (
        firmware ? "Select the DX7 firmware ROM (16384 bytes)"
                 : "Select the DX7 factory voices (32768 bytes)",
        juce::File{}, "*.bin;*.BIN;*.rom;*.ROM;*");

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, firmware](const juce::FileChooser& fc)
        {
            auto target = pickedFrom (fc);
            if (! target.valid()) return;

            juce::MemoryBlock mb;
            if (! target.read (mb))
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Could not read file",
                    "That file could not be opened.");
                return;
            }

            juce::String err;
            const bool ok = firmware
                ? processor.loadFirmwareRom   (mb.getData(), mb.getSize(), target.source(), err)
                : processor.loadFactoryVoices (mb.getData(), mb.getSize(), target.source(), err);

            if (! ok)
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    firmware ? "Not a firmware ROM" : "Not a voice bank", err);
                return;
            }

            // A voice ROM turns one starter bank into eight cartridge banks,
            // so the selector itself has to be rebuilt, not just relabelled.
            rebuildBankList();
            rebuildProgramList (false);
            refreshAll();
        });
}

void VDX7AudioProcessorEditor::refreshEngineStatus()
{
    // The chevron is amber like the combo boxes' arrows, and turns green
    // while a firmware ROM (the bit-accurate engine) is in charge.
    const bool emu = processor.usingEmulator();
    romBtn.setColour (juce::TextButton::textColourOffId,
                      emu ? juce::Colour (0xff7fd39b) : col::accent);
    romBtn.repaint();
    romBtn.setTooltip (emu ? "Firmware ROM loaded - running the bit-accurate engine"
                           : "No firmware ROM - running the native FM engine. "
                             "Click to load dx7.bin.");

    // The DC blocker belongs to the native engine; with a firmware ROM playing
    // it stays set but has nothing to act on, so it is shown dimmed.
    dcKnob.setAlpha (emu ? 0.4f : 1.0f);
}

// ---------------------------------------------------------------------------
//  Header, second row: the FUNCTION page
// ---------------------------------------------------------------------------
// The DX7 keeps these on its FUNCTION page, apart from the voice: they belong
// to the instrument, so loading a patch never changes them. Every control is
// bound to its own APVTS parameter (automatable, saved with the project) and
// both engines follow it - the emulator through the firmware's own
// function-parameter SysEx, the native engine directly.
void VDX7AudioProcessorEditor::setupFunctionRow()
{
    auto& apvts = processor.apvts;

    auto bar = [this, &apvts] (juce::Slider& s, const juce::String& id, const juce::String& caption,
                               const juce::String& tip, bool bipolar = false) {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setSliderSnapsToMousePosition (false);
        s.setWantsKeyboardFocus (true);
        s.getProperties().set (vdx7ui::HeaderLookAndFeel::captionProperty(), caption);
        if (bipolar) s.getProperties().set (vdx7ui::HeaderLookAndFeel::bipolarProperty(), true);
        s.setLookAndFeel (&headerLnf);
        if (auto* param = apvts.getParameter (id)) {
            attachments.push_back (std::make_unique<juce::SliderParameterAttachment> (*param, s));
            s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            s.setTitle (param->getName (64));
        }
        s.setTooltip (tip);
        content.addAndMakeVisible (s);
    };
    auto toggle = [this, &apvts] (juce::TextButton& b, const juce::String& id,
                                  const juce::String& text, const juce::String& tip) {
        vdx7ui::setupToggleButton (b, text, tip, headerLnf);
        if (auto* param = apvts.getParameter (id)) {
            buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (*param, b));
            b.setTitle (param->getName (64));
            resetListeners.push_back (std::make_unique<vdx7ui::ResetOnDoubleClick> (*param));
            b.addMouseListener (resetListeners.back().get(), false);
        }
        b.setTooltip (tip);
        content.addAndMakeVisible (b);
    };

    bar (tuneBar, vdx7::fnid::masterTune, "TUNE",
         "Master Tune, in cents: the whole instrument, up to 75 cents either way of A440. "
         "Drag sideways, click twice for A440.", true);

    toggle (monoBtn, vdx7::fnid::polyMono, "POLY",
            "POLY / MONO: play chords, or one note at a time (legato lines glide and "
            "do not restart the envelopes)");
    toggle (portaModeBtn, vdx7::fnid::portaMode, "RETAIN",
            "Portamento Mode. POLY: RETAIN keeps notes held by the sustain pedal at their "
            "pitch, FOLLOW glides them to each new key. MONO: FINGERED glides only between "
            "keys played legato, FULL glides every note.");
    bar (portaTimeBar, vdx7::fnid::portaTime, "PORTA",
         "Portamento Time: how long a note takes to glide to the next one (0 = no glide)");
    bar (pbBar, vdx7::fnid::pbRange, "PitchB",
         "Pitch Bend Range: how far the bend wheel reaches, 0 to 12 semitones");

    // The bars are short, so they print bare numbers; the tooltips give the units.
    tuneBar.textFromValueFunction = [] (double v) {
        const double c = v * (double) vdx7::kMasterTuneCentsPerStep;
        return (c > 0.05 ? "+" : "") + juce::String (c, 1);
    };
    pbBar.textFromValueFunction = [] (double v) { return juce::String ((int) v); };

    // Each controller: its range bar, and a small "v" beside it opening the
    // PITCH / AMPLITUDE / EG BIAS switches (headed with the controller's full
    // name, so the two-letter captions never need a tooltip to be understood).
    static const char* caps[4] = { "ModWhl", "FCtrl", "Breath", "AfterTouch" };
    for (int c = 0; c < 4; ++c) {
        const juce::String n = vdx7::fnid::controllerNames[c];
        bar (ctlRange[(size_t) c], vdx7::fnid::range (c), caps[c],
             n + " Range: how much the controller moves at full travel (0-99)");
        auto& m = ctlAssign[(size_t) c];
        m.setLookAndFeel (&headerLnf);
        m.setTitle (n + " assignment");
        m.onMenu  = [this, c] { showAssignMenu (c); };
        m.onReset = [this, c] {
            resetParam (vdx7::fnid::pitch (c));
            resetParam (vdx7::fnid::amp (c));
            resetParam (vdx7::fnid::egBias (c));
        };
        content.addAndMakeVisible (m);
    }

    // MORE: the MIDI receive channel and memory protect.
    moreBtn.setButtonText ("MORE");
    moreBtn.setLookAndFeel (&headerLnf);
    moreBtn.setTitle ("More function settings");
    moreBtn.onMenu  = [this] { showMoreMenu(); };
    moreBtn.onReset = [this] {
        resetParam (vdx7::fnid::midiChannel);
        resetParam (vdx7::fnid::memProtect);
    };
    content.addAndMakeVisible (moreBtn);

    // The menus show the parameters' state, which host automation, undo or a
    // restored project can change too.
    std::vector<juce::String> watched { vdx7::fnid::midiChannel, vdx7::fnid::memProtect };
    for (int c = 0; c < 4; ++c) {
        watched.push_back (vdx7::fnid::pitch (c));
        watched.push_back (vdx7::fnid::amp (c));
        watched.push_back (vdx7::fnid::egBias (c));
    }
    for (const auto& id : watched)
        if (auto* param = apvts.getParameter (id))
            fnWatchers.push_back (std::make_unique<juce::ParameterAttachment> (
                *param, [this] (float) { refreshFunctionMenus(); }));

    // DC blocker, native engine only: a leaky integrator whose corner goes from
    // off (0) up to 5 Hz. The knob shows "DC" while off, the corner in Hz once on.
    vdx7ui::setupCompactKnob (dcKnob, "DC Block", 0, 5, headerLnf);
    dcKnob.setRange (0.0, 5.0, 0.01);
    dcKnob.getProperties().set (vdx7ui::CompactLookAndFeel::litAtZeroProperty(), true);
    if (auto* param = apvts.getParameter (vdx7::fnid::dcBlock)) {
        attachments.push_back (std::make_unique<juce::SliderParameterAttachment> (*param, dcKnob));
        dcKnob.setDoubleClickReturnValue (true, 0.0);
    }
    dcKnob.textFromValueFunction = [] (double v) {
        return v <= 0.0 ? juce::String ("DC") : juce::String (v, 1);
    };
    dcKnob.setTooltip ("DC Block (native engine): removes DC offset with a leaky integrator. "
                       "Off at 0, up to a 5 Hz corner at full. No effect while a firmware ROM plays.");
    content.addAndMakeVisible (dcKnob);

    monoBtn.onStateChange      = [this] { updateFunctionLabels(); };
    portaModeBtn.onStateChange = [this] { updateFunctionLabels(); };
    updateFunctionLabels();
    refreshFunctionMenus();
}

void VDX7AudioProcessorEditor::setParamValue (const juce::String& id, float value)
{
    if (auto* p = processor.apvts.getParameter (id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void VDX7AudioProcessorEditor::resetParam (const juce::String& id)
{
    if (auto* p = processor.apvts.getParameter (id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

static float paramValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    auto* p = apvts.getParameter (id);
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

void VDX7AudioProcessorEditor::showAssignMenu (int c)
{
    auto& apvts = processor.apvts;
    const juce::String ids[3]   = { vdx7::fnid::pitch (c), vdx7::fnid::amp (c), vdx7::fnid::egBias (c) };
    const juce::String names[3] = { "Pitch  (vibrato depth)",
                                     "Amplitude  (tremolo depth)",
                                     "EG Bias  (level of operators with Amp Mod Sens)" };
    juce::PopupMenu m;
    m.addSectionHeader (vdx7::fnid::controllerNames[c]);
    for (int d = 0; d < 3; ++d) {
        const bool on = paramValue (apvts, ids[d]) >= 0.5f;
        m.addItem (names[d], true, on, [this, id = ids[d], on] { setParamValue (id, on ? 0.0f : 1.0f); });
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (ctlAssign[(size_t) c]));
}

void VDX7AudioProcessorEditor::showMoreMenu()
{
    auto& apvts = processor.apvts;
    const int ch = juce::roundToInt (paramValue (apvts, vdx7::fnid::midiChannel));   // 0 = omni
    const bool prot = paramValue (apvts, vdx7::fnid::memProtect) >= 0.5f;

    juce::PopupMenu channels;
    channels.addItem ("Omni  (every channel)", true, ch == 0, [this] { setParamValue (vdx7::fnid::midiChannel, 0.0f); });
    channels.addSeparator();
    for (int i = 1; i <= 16; ++i)
        channels.addItem ("Channel " + juce::String (i), true, ch == i,
                          [this, i] { setParamValue (vdx7::fnid::midiChannel, (float) i); });

    juce::PopupMenu m;
    m.addSectionHeader ("MIDI");
    m.addSubMenu ("Receive Channel:  " + (ch == 0 ? juce::String ("Omni") : juce::String (ch)), channels);
    m.addSectionHeader ("Memory");
    m.addItem ("Memory Protect  (refuse voice / bank dumps arriving over MIDI)", true, prot,
               [this, prot] { setParamValue (vdx7::fnid::memProtect, prot ? 0.0f : 1.0f); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreBtn));
}

// Each assignment chevron is amber while that controller drives something and
// dim while it is assigned to nothing; the tooltips spell out the settings.
void VDX7AudioProcessorEditor::refreshFunctionMenus()
{
    auto& apvts = processor.apvts;
    for (int c = 0; c < 4; ++c) {
        juce::StringArray on;
        if (paramValue (apvts, vdx7::fnid::pitch (c))  >= 0.5f) on.add ("Pitch");
        if (paramValue (apvts, vdx7::fnid::amp (c))    >= 0.5f) on.add ("Amplitude");
        if (paramValue (apvts, vdx7::fnid::egBias (c)) >= 0.5f) on.add ("EG Bias");
        auto& b = ctlAssign[(size_t) c];
        b.lit = ! on.isEmpty();
        b.setTooltip (juce::String (vdx7::fnid::controllerNames[c]) + " assignment: "
                      + (on.isEmpty() ? juce::String ("nothing") : on.joinIntoString (", "))
                      + ". Click to change, twice to reset.");
        b.repaint();
    }
    const int ch = juce::roundToInt (paramValue (apvts, vdx7::fnid::midiChannel));
    const bool prot = paramValue (apvts, vdx7::fnid::memProtect) >= 0.5f;
    moreBtn.setTooltip ("MIDI channel: " + (ch == 0 ? juce::String ("Omni") : juce::String (ch))
                        + ", Memory Protect: " + (prot ? "on" : "off")
                        + ". Click for the menu, twice to reset both.");
}

void VDX7AudioProcessorEditor::updateFunctionLabels()
{
    const bool mono   = monoBtn.getToggleState();
    const bool follow = portaModeBtn.getToggleState();
    monoBtn.setButtonText (mono ? "MONO" : "POLY");
    portaModeBtn.setButtonText (mono ? (follow ? "FULL" : "FINGER")
                                     : (follow ? "FOLLOW" : "RETAIN"));
}

// One strip, left to right:
//   [TUNE] [POLY] [RETAIN] [PORTA] [PB] | [MW v] [FC v] [BC v] [AT v] | [MORE v] (DC)
void VDX7AudioProcessorEditor::layoutFunctionRow (juce::Rectangle<int> row)
{
    const int gap = 4, groupGap = 9;
    tuneBar.setBounds (row.removeFromLeft (72));        row.removeFromLeft (gap);
    monoBtn.setBounds (row.removeFromLeft (44));        row.removeFromLeft (gap);
    portaModeBtn.setBounds (row.removeFromLeft (52));   row.removeFromLeft (gap);
    portaTimeBar.setBounds (row.removeFromLeft (64));   row.removeFromLeft (gap);
    pbBar.setBounds (row.removeFromLeft (48));          row.removeFromLeft (groupGap);

    dcKnob.setBounds (row.removeFromRight (row.getHeight()));   row.removeFromRight (gap);
    moreBtn.setBounds (row.removeFromRight (58));               row.removeFromRight (groupGap);

    // The four controllers share what is left.
    const int each = (row.getWidth() - 3 * gap) / 4;
    for (int c = 0; c < 4; ++c) {
        auto cell = (c < 3) ? row.removeFromLeft (each) : row;
        if (c < 3) row.removeFromLeft (gap);
        ctlAssign[(size_t) c].setBounds (cell.removeFromRight (16));
        cell.removeFromRight (2);
        ctlRange[(size_t) c].setBounds (cell);
    }
}

void VDX7AudioProcessorEditor::importBankFromFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Import a DX7 32-voice SysEx bank", juce::File{}, "*.syx;*.SYX");
    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc){
            auto target = pickedFrom (fc);
            if (! target.valid()) return;
            juce::MemoryBlock mb;
            if (! target.read (mb)) return;
            if (processor.importSysexBank (mb.getData(), mb.getSize(),
                                           target.nameNoExt()))
            {
                openBankFileName_ = target.name();   // for save-protection
                const juce::String label = "USER: " + target.nameNoExt();
                if (bankBox.indexOfItemId (kUserBankId) < 0) bankBox.addItem (label, kUserBankId);
                else                                         bankBox.changeItemText (kUserBankId, label);
                bankBox.setSelectedId (kUserBankId, juce::dontSendNotification);
                rebuildProgramList (false);
                refreshAll();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Import failed",
                    "No 32-voice DX7 bank (a 4104-byte SysEx dump) was found in that file.");
            }
        });
}

void VDX7AudioProcessorEditor::exportToFile (bool wholeBank)
{
    auto bytes = std::make_shared<std::vector<uint8_t>> (
        wholeBank ? processor.exportBankSysex() : processor.exportVoiceSysex());

    juce::String suggested;
    if (wholeBank)
        suggested = "VDX7Bank.syx";
    else
    {
        juce::String vn = juce::String (processor.getVoiceCopy().name().c_str()).trim();
        suggested = "VDX7_" + juce::File::createLegalFileName (vn.isEmpty() ? "voice" : vn) + ".syx";
    }

    chooser = std::make_unique<juce::FileChooser> (
        wholeBank ? "Export bank (.syx)" : "Export current voice (.syx)",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (suggested),
        "*.syx");
    chooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [bytes](const juce::FileChooser& fc){
            auto target = pickedFrom (fc);
            if (! target.valid()) return;
            target.write (bytes->data(), bytes->size());
        });
}

void VDX7AudioProcessorEditor::refreshAll()
{
    refreshEngineStatus();

    updatingUI = true;

    // Knob values are driven by their SliderParameterAttachments, so we don't
    // set them here.  We just keep the bank/program selectors and the routing
    // diagram in sync with the processor.
    //
    // Rebuilding the bank/program lists here - not just re-selecting an item
    // in whatever list already exists - matters because refreshAll() is the
    // one place that runs for *every* way the processor's state can change:
    // a ROM finishing loading from the menu (which also rebuilds directly,
    // belt-and-braces), a saved project restoring its own ROM on reload, or
    // undo/host automation of a bulk state change. Any of those can flip
    // between the starter bank and a loaded voice ROM, which changes both the
    // bank names (STARTER vs ROM1A..ROM4B) and every program name in the
    // selected bank - so both lists are rebuilt every time rather than only
    // from the one code path that used to remember to do it.
    rebuildBankList();

    // If a user bank exists (e.g. restored from a saved session) but the bank
    // selector has no USER entry yet, add one so it can be re-selected.
    if (processor.hasUserBank() && bankBox.indexOfItemId (kUserBankId) < 0) {
        juce::String nm = processor.userBankName();
        bankBox.addItem ("USER: " + (nm.isEmpty() ? juce::String ("bank") : nm), kUserBankId);
    }

    if (processor.userActive())
        bankBox.setSelectedId (kUserBankId, juce::dontSendNotification);
    else {
        // A project saved on the starter bank alongside a voice ROM, reopened
        // without that ROM, points at a slot this list does not have; without
        // a ROM the starter bank is simply the first (and only) entry.
        const int id = processor.currentBank() + 1;
        bankBox.setSelectedId (bankBox.indexOfItemId (id) >= 0 ? id : 1, juce::dontSendNotification);
    }

    rebuildProgramList (false);
    progBox.setSelectedId (processor.currentProg() + 1, juce::dontSendNotification);
    updatingUI = false;
    algoCard.diagram.repaint();
}

void VDX7AudioProcessorEditor::saveIntoBankFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Choose an existing DX7 bank (.syx) to save this voice into",
        juce::File{}, "*.syx;*.SYX");
    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc){
            auto target = pickedFrom (fc);
            if (! target.valid()) return;

            juce::MemoryBlock mb;
            if (! target.read (mb)) {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Save failed",
                    "That file could not be read.");
                return;
            }

            auto bank = std::make_shared<vdx7::Bank>();
            if (! vdx7::findBankInSysex (static_cast<const uint8_t*> (mb.getData()),
                                         mb.getSize(), *bank)) {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Not a bank",
                    "No 32-voice DX7 bank (a 4104-byte SysEx dump) was found in that file.");
                return;
            }

            // Let the user pick which of the 32 presets to overwrite, showing the
            // existing name in each slot.
            juce::PopupMenu m;
            for (int i = 0; i < 32; ++i) {
                juce::String nm = juce::String (bank->voiceName (i).c_str()).trim();
                m.addItem (i + 1,
                    juce::String (i + 1).paddedLeft ('0', 2) + "  "
                    + (nm.isEmpty() ? juce::String ("(unnamed)") : nm));
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileBtn),
                [this, target, bank](int choice){
                    if (choice < 1 || choice > 32) return;
                    const int slot = choice - 1;

                    // Prompt for a preset name (DX7 voice names are 10 chars).
                    auto* aw = new juce::AlertWindow (
                        "Preset name",
                        "Name for this preset (up to 10 characters):",
                        juce::MessageBoxIconType::QuestionIcon);
                    aw->addTextEditor ("nm",
                        juce::String (processor.getVoiceCopy().name().c_str()).trim());
                    if (auto* te = aw->getTextEditor ("nm")) te->setInputRestrictions (10);
                    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
                    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                    aw->setVisible (true);
                    aw->enterModalState (true, juce::ModalCallbackFunction::create (
                        [this, aw, target, bank, slot](int res){
                            std::unique_ptr<juce::AlertWindow> keep (aw);
                            if (res != 1) return;
                            juce::String nm = aw->getTextEditorContents ("nm");

                            // Write the (space-padded, printable-ASCII) name into
                            // the 10 VCED name bytes, then store the voice.
                            vdx7::Voice v = processor.getVoiceCopy();
                            std::string clean;                     // 10 printable ASCII chars
                            for (int i = 0; i < 10; ++i) {
                                int c = (i < nm.length()) ? (int) nm[i] : 32;
                                clean += (char) ((c < 32 || c > 126) ? 32 : c);
                            }
                            v.setName (clean);
                            vdx7::Bank out = *bank;
                            out.setVoice (slot, v);
                            auto bytes = out.toBankSysex (0);

                            // Write straight back to the chosen bank (factory ROM
                            // banks have no file, so they can never be chosen here).
                            const bool ok = target.write (bytes.data(), bytes.size());
                            juce::AlertWindow::showMessageBoxAsync (
                                ok ? juce::MessageBoxIconType::InfoIcon
                                   : juce::MessageBoxIconType::WarningIcon,
                                ok ? "Saved" : "Save failed",
                                ok ? ("\"" + nm.trim() + "\" written to slot "
                                      + juce::String (slot + 1) + " of\n" + target.name())
                                   : juce::String ("Could not write to that location."));
                        }), false);
                });
        });
}

void VDX7AudioProcessorEditor::paint (juce::Graphics& g)
{
    // Fills the whole editor, including whatever letterbox/pillarbox margin
    // is left around the (possibly scaled) `content` panel.
    g.fillAll (col::bg);
}

void VDX7AudioProcessorEditor::resized()
{
    auto avail = getLocalBounds();
    if (avail.getWidth() <= 0 || avail.getHeight() <= 0)
        return;

    // Scale the whole fixed-design-resolution UI to fit inside whatever size
    // the editor actually is, preserving aspect ratio, and centre it. This is
    // what prevents controls from being pushed off-screen on aspect ratios
    // (21:9, 16:9 phones, etc.) that differ from the design canvas.
    const float scale = juce::jmax (0.05f,
        juce::jmin ((float) avail.getWidth()  / (float) kDesignW,
                    (float) avail.getHeight() / (float) kDesignH));

    const float scaledW = (float) kDesignW * scale;
    const float scaledH = (float) kDesignH * scale;
    const float offsetX = ((float) avail.getWidth()  - scaledW) * 0.5f;
    const float offsetY = ((float) avail.getHeight() - scaledH) * 0.5f;

    content.setBounds (0, 0, kDesignW, kDesignH);   // native/untransformed size never changes
    content.setTransform (juce::AffineTransform::scale (scale).translated (offsetX, offsetY));

    layoutContent();
}

void VDX7AudioProcessorEditor::layoutContent()
{
    auto r = content.getLocalBounds().reduced (8);

    auto top = r.removeFromTop (78);

    // The header has two rows beside the two-line LCD at the right. The first:
    //   VirtualDX7  [FX]  [bank v] [program v] [<] [>]  [INIT] [FILE] [v]
    // (the program box takes whatever width is left over); the second holds
    // the FUNCTION page settings (layoutFunctionRow).
    lcd.setBounds (top.removeFromRight (380));
    top.removeFromRight (10);
    auto row = top.removeFromTop (32);
    top.removeFromTop (12);
    layoutFunctionRow (top.removeFromTop (30));

    // As wide as the title actually renders in this platform's font (Windows'
    // is wider than Linux's), so it is never cut down to "VirtualD...".
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (title.getFont(), title.getText(), 0.0f, 0.0f);
        const int w = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth())
                    + title.getBorderSize().getLeftAndRight() + 6;
        title.setBounds (row.removeFromLeft (juce::jlimit (90, 170, w)));
    }
    row.removeFromLeft (6);
    fxBtn.setBounds (row.removeFromLeft (44));
    row.removeFromLeft (10);
    bankBox.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (6);

    romBtn.setBounds (row.removeFromRight (34));
    row.removeFromRight (6);
    fileBtn.setBounds (row.removeFromRight (70));
    row.removeFromRight (8);
    initBtn.setBounds (row.removeFromRight (58));
    row.removeFromRight (10);
    nextBtn.setBounds (row.removeFromRight (32));
    row.removeFromRight (4);
    prevBtn.setBounds (row.removeFromRight (32));
    row.removeFromRight (6);
    progBox.setBounds (row);

    r.removeFromTop (8);

    keyboard.setBounds (r.removeFromBottom (72));
    r.removeFromBottom (8);

    // Everything below the header and above the keyboard is shared: either the
    // voice page (operator selector + operator panel + algorithm + global) or
    // the FX page occupies it, never both.
    if (fxPanel != nullptr)
        fxPanel->setBounds (r);

    // Right column: the selected operator's card over the GLOBAL card.
    // Left column, the same 330 px wide all the way down: the OPERATORS card
    // (selector buttons + level knobs), the envelope scope, and the ALGORITHM
    // card. The cards line up across the two columns: OPERATORS + scope span
    // the OPERATOR card's height, ALGORITHM matches GLOBAL.
    const int gap = 8;
    auto side = r.removeFromLeft (330);
    r.removeFromLeft (gap);

    auto bottom = r.removeFromBottom (168);
    global->setBounds (bottom);
    r.removeFromBottom (gap);

    // OPERATORS card: amber header band, then two rows of three cells, each
    // cell the "Operator n" button with that operator's level knob beside it.
    auto card = side.removeFromTop (106);
    opCard.setBounds (card);
    auto cells = card.withTrimmedTop (24).reduced (6, 4);
    const int btnH = 34, btnGap = 6, knobGap = 3;
    for (int row = 0; row < 2; ++row) {
        auto rowArea = cells.removeFromTop (btnH);
        const int bw = (rowArea.getWidth() - 2 * btnGap) / 3;
        for (int c = 0; c < 3; ++c) {
            auto cell = (c < 2) ? rowArea.removeFromLeft (bw) : rowArea;   // last one takes the remainder
            const auto idx = (size_t) (row * 3 + c);
            opLevel[idx].setBounds (cell.removeFromRight (btnH));
            cell.removeFromRight (knobGap);
            opSelect[idx].setBounds (cell);
            rowArea.removeFromLeft (btnGap);
        }
        cells.removeFromTop (btnGap);
    }
    side.removeFromTop (gap);

    // ALGORITHM card at the bottom, exactly level with the GLOBAL card beside
    // it; the envelope scope takes the space between the two cards, so its
    // bottom edge lines up with the OPERATOR card's.
    algoCard.setBounds (side.removeFromBottom (bottom.getHeight()));
    side.removeFromBottom (gap);
    envView.setBounds (side);

    for (auto& p : ops) p->setBounds (r);   // same bounds; only the selected one is visible
}

void VDX7AudioProcessorEditor::selectOperator (int op)
{
    selectedOp_ = juce::jlimit (0, 5, op);
    for (int i = 0; i < (int) ops.size(); ++i) {
        // On the FX page no operator panel is shown at all, but the selection
        // is still tracked so returning to the voice page restores it.
        ops[(size_t) i]->setVisible (! showingFx_ && i == selectedOp_);
        opSelect[(size_t) i].setToggleState (i == selectedOp_, juce::dontSendNotification);
    }
    envView.setSelectedOperator (selectedOp_);
}

void VDX7AudioProcessorEditor::setFxViewVisible (bool shouldShowFx)
{
    showingFx_ = shouldShowFx;

    // The FX page takes over the whole area the voice page uses, so the things
    // that live there go away together: the operator selector, the envelope
    // scope and its time bar, the selected operator panel, the algorithm view
    // and the global panel.
    opCard.setVisible (! showingFx_);
    for (auto& b : opSelect) b.setVisible (! showingFx_);
    for (auto& k : opLevel)  k.setVisible (! showingFx_);
    envView.setVisible (! showingFx_);
    algoCard.setVisible (! showingFx_);
    global->setVisible (! showingFx_);
    fxPanel->setVisible (showingFx_);

    selectOperator (selectedOp_);   // re-applies operator-panel visibility

    fxBtn.setToggleState (showingFx_, juce::dontSendNotification);
}