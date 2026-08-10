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
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
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
    title.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, col::accent);
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
        m.addItem (5, "Forget loaded ROMs", roms.hasFirmware() || roms.hasVoices());

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (romBtn),
            [this](int choice)
            {
                if      (choice == 1) loadRomFile (true);
                else if (choice == 2) loadRomFile (false);
                else if (choice == 5)
                {
                    processor.forgetRoms();
                    rebuildBankList();
                    rebuildProgramList (false);
                    refreshAll();
                }
            });
    };
    content.addAndMakeVisible (romBtn);

    engineLabel.setJustificationType (juce::Justification::centredLeft);
    engineLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    engineLabel.setColour (juce::Label::textColourId, col::textDim);
    content.addAndMakeVisible (engineLabel);
    refreshEngineStatus();


    // LCD wiring
    lcd.lcdProvider       = [this](char l1[17], char l2[17]){ processor.getLcd (l1, l2); };
    lcd.ledNumberProvider = [this]{ return processor.currentProg() + 1; };
    lcd.readyProvider     = [this]{ return processor.engineReady(); };
    content.addAndMakeVisible (lcd);

    // algorithm view
    algo.algoProvider     = [this]{ return processor.getVoiceCopy().get (vdx7::G_ALG); };
    algo.feedbackProvider = [this]{ return processor.getVoiceCopy().get (vdx7::G_FB); };
    algo.opLevelProvider  = [this]{
        auto v = processor.getVoiceCopy();
        std::array<int,6> a{};
        for (int i = 0; i < 6; ++i) a[(size_t) i] = v.getOp (5 - i, vdx7::OP_OL); // OP1..OP6
        return a;
    };
    content.addAndMakeVisible (algo);

    // slider factory shared by all panels.  Each knob is bound to its APVTS
    // parameter with a SliderParameterAttachment, so knob moves write to the
    // parameter (and thus reach the host + emulator) and incoming host
    // automation moves the knob - both directions, no manual callbacks.
    SliderFactory make =
        [this](const juce::String& cap, const juce::String& full,
               int mn, int mx, int off) -> ParamSlider*
    {
        auto* s = new ParamSlider (cap, full, mn, mx, off);
        if (auto* param = processor.paramForOffset (off))
            attachments.push_back (
                std::make_unique<juce::SliderParameterAttachment> (*param, s->getSlider()));
        allSliders.push_back (s);
        return s;
    };

    // six operator panels, displayed OP1..OP6 (vced op index 5..0)
    for (int d = 1; d <= 6; ++d) {
        int vcedOp = 6 - d;
        auto panel = std::make_unique<OperatorPanel> (d, vcedOp, make);
        content.addAndMakeVisible (*panel);
        ops.push_back (std::move (panel));
    }

    // Operator selector sidebar: six green buttons, only one operator shown.
    for (int i = 0; i < 6; ++i) {
        auto& b = opSelect[(size_t) i];
        b.setButtonText ("Operator " + juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (9001);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f8a4e));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        b.onClick = [this, i] { selectOperator (i); };
        content.addAndMakeVisible (b);
    }

    global = std::make_unique<GlobalPanel> (make);
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
    setLookAndFeel (nullptr);
}

// The factory side of the bank selector. With a voice ROM loaded that is the
// eight cartridge banks; without one there is a single bundled starter bank, so
// offering eight identical copies of it would be nothing but noise.
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
    engineLabel.setText (processor.engineDescription(), juce::dontSendNotification);

    const bool emu = processor.usingEmulator();
    romBtn.setColour (juce::TextButton::textColourOffId,
                      emu ? juce::Colour (0xff7fd39b) : col::textDim);
    romBtn.setTooltip (emu ? "Firmware ROM loaded - running the bit-accurate engine"
                           : "No firmware ROM - running the native FM engine. "
                             "Click to load dx7.bin.");
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
    else
        bankBox.setSelectedId (processor.currentBank() + 1, juce::dontSendNotification);

    rebuildProgramList (false);
    progBox.setSelectedId (processor.currentProg() + 1, juce::dontSendNotification);
    updatingUI = false;
    algo.repaint();
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

    // Left column of the header: the title, with the FX page toggle beneath it.
    auto titleCol = top.removeFromLeft (150);
    title.setBounds (titleCol.removeFromTop (44));
    titleCol.removeFromTop (2);
    fxBtn.setBounds (titleCol.removeFromTop (30).withTrimmedRight (10));

    lcd.setBounds (top.removeFromRight (380));
    top.removeFromRight (10);
    auto ctl = top.removeFromTop (34).withTrimmedTop (2);
    bankBox.setBounds (ctl.removeFromLeft (150));
    ctl.removeFromLeft (6);
    progBox.setBounds (ctl.removeFromLeft (200));
    ctl.removeFromLeft (6);
    prevBtn.setBounds (ctl.removeFromLeft (34));
    ctl.removeFromLeft (4);
    nextBtn.setBounds (ctl.removeFromLeft (34));
    ctl.removeFromLeft (10);
    initBtn.setBounds (ctl.removeFromLeft (60));
    ctl.removeFromLeft (10);
    fileBtn.setBounds (ctl.removeFromLeft (80));
    ctl.removeFromLeft (8);
    romBtn.setBounds (ctl.removeFromLeft (66));
    ctl.removeFromLeft (8);
    engineLabel.setBounds (ctl.removeFromLeft (190));


    r.removeFromTop (8);

    keyboard.setBounds (r.removeFromBottom (72));
    r.removeFromBottom (8);

    // Everything below the header and above the keyboard is shared: either the
    // voice page (operator selector + operator panel + algorithm + global) or
    // the FX page occupies it, never both.
    if (fxPanel != nullptr)
        fxPanel->setBounds (r);

    auto bottom = r.removeFromBottom (168);
    algo.setBounds (bottom.removeFromLeft (330));
    bottom.removeFromLeft (8);
    global->setBounds (bottom);

    r.removeFromBottom (8);

    // Operator selector sidebar (left) + the single selected operator (right).
    const int gap = 8;
    auto side = r.removeFromLeft (330);   // buttons as wide as the algorithm box below
    r.removeFromLeft (gap);
    const int bh = (side.getHeight() - gap * 5) / 6;
    for (int i = 0; i < 6; ++i)
        opSelect[(size_t) i].setBounds (side.getX(), side.getY() + i * (bh + gap),
                                        side.getWidth(), bh);
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
}

void VDX7AudioProcessorEditor::setFxViewVisible (bool shouldShowFx)
{
    showingFx_ = shouldShowFx;

    // The FX page takes over the whole area the voice page uses, so the four
    // things that live there go away together: the operator selector sidebar,
    // the selected operator panel, the algorithm view and the global panel.
    for (auto& b : opSelect) b.setVisible (! showingFx_);
    algo.setVisible (! showingFx_);
    global->setVisible (! showingFx_);
    fxPanel->setVisible (showingFx_);

    selectOperator (selectedOp_);   // re-applies operator-panel visibility

    fxBtn.setToggleState (showingFx_, juce::dontSendNotification);
}