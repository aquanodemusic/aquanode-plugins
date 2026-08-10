/*  PluginEditor.cpp  -  see PluginEditor.h.  GPLv3.  */
#include "PluginEditor.h"

using namespace vdx7ui;

VDX7AudioProcessorEditor::VDX7AudioProcessorEditor (VDX7AudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);

    // The emulator only produces sound across C1..C6, so constrain the
    // on-screen keyboard to that range (MIDI 24..84 with C1 = note 24).
    keyboard.setTitle ("On-screen keyboard");
    addAndMakeVisible (keyboard);

    title.setText ("VirtualDX7", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, col::accent);
    addAndMakeVisible (title);

    // bank / program browser
    bankBox.setTitle ("Bank");
    progBox.setTitle ("Program");
    for (int b = 0; b < vdx7::kNumFactoryBanks; ++b)
        bankBox.addItem (vdx7::factoryBankName (b), b + 1);
    bankBox.setSelectedId (processor.currentBank() + 1, juce::dontSendNotification);
    bankBox.onChange = [this]{
        if (updatingUI) return;
        rebuildProgramList (true);
    };
    addAndMakeVisible (bankBox);

    rebuildProgramList (false);
    progBox.setSelectedId (processor.currentProg() + 1, juce::dontSendNotification);
    progBox.onChange = [this]{
        if (updatingUI) return;
        const int prog = progBox.getSelectedId() - 1;
        if (bankBox.getSelectedId() == kUserBankId) processor.selectUserProgram (prog);
        else processor.selectFactory (bankBox.getSelectedId() - 1, prog);
    };
    addAndMakeVisible (progBox);

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
    addAndMakeVisible (prevBtn);
    addAndMakeVisible (nextBtn);
    addAndMakeVisible (initBtn);
    addAndMakeVisible (fileBtn);


    // LCD wiring
    lcd.lcdProvider       = [this](char l1[17], char l2[17]){ processor.getLcd (l1, l2); };
    lcd.ledNumberProvider = [this]{ return processor.currentProg() + 1; };
    lcd.readyProvider     = [this]{ return processor.engineReady(); };
    addAndMakeVisible (lcd);

    // algorithm view
    algo.algoProvider     = [this]{ return processor.getVoiceCopy().get (vdx7::G_ALG); };
    algo.feedbackProvider = [this]{ return processor.getVoiceCopy().get (vdx7::G_FB); };
    algo.opLevelProvider  = [this]{
        auto v = processor.getVoiceCopy();
        std::array<int,6> a{};
        for (int i = 0; i < 6; ++i) a[(size_t) i] = v.getOp (5 - i, vdx7::OP_OL); // OP1..OP6
        return a;
    };
    addAndMakeVisible (algo);

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
        addAndMakeVisible (*panel);
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
        addAndMakeVisible (b);
    }

    global = std::make_unique<GlobalPanel> (make);
    addAndMakeVisible (*global);

    selectOperator (0);
    processor.addChangeListener (this);
    setResizable (true, true);
    setResizeLimits (960, 480, 2400, 1600);
    setSize (1180, 594);
}

VDX7AudioProcessorEditor::~VDX7AudioProcessorEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void VDX7AudioProcessorEditor::rebuildProgramList (bool sendSelect)
{
    const bool user = (bankBox.getSelectedId() == kUserBankId) && processor.hasUserBank();
    int bank = user ? 0 : juce::jmax (0, bankBox.getSelectedId() - 1);
    auto b = user ? processor.userBank() : vdx7::factoryBank (bank);
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

void VDX7AudioProcessorEditor::importBankFromFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Import a DX7 32-voice SysEx bank", juce::File{}, "*.syx;*.SYX");
    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc){
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            juce::MemoryBlock mb;
            if (! file.loadFileAsData (mb)) return;
            if (processor.importSysexBank (mb.getData(), mb.getSize(),
                                           file.getFileNameWithoutExtension()))
            {
                openBankFileName_ = file.getFileName();   // for save-protection
                const juce::String label = "USER: " + file.getFileNameWithoutExtension();
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
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            if (! file.hasFileExtension ("syx")) file = file.withFileExtension ("syx");
            file.replaceWithData (bytes->data(), bytes->size());
        });
}

void VDX7AudioProcessorEditor::refreshAll()
{
    updatingUI = true;

    // Knob values are driven by their SliderParameterAttachments, so we don't
    // set them here.  We just keep the bank/program selectors and the routing
    // diagram in sync with the processor.

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
            auto file = fc.getResult();
            if (file == juce::File{}) return;

            juce::MemoryBlock mb;
            if (! file.loadFileAsData (mb)) {
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
                [this, file, bank](int choice){
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
                        [this, aw, file, bank, slot](int res){
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

                            // Write straight to the chosen bank file (factory ROM
                            // banks have no file, so they can never be chosen here).
                            const bool ok = file.replaceWithData (bytes.data(), bytes.size());
                            juce::AlertWindow::showMessageBoxAsync (
                                ok ? juce::MessageBoxIconType::InfoIcon
                                   : juce::MessageBoxIconType::WarningIcon,
                                ok ? "Saved" : "Save failed",
                                ok ? ("\"" + nm.trim() + "\" written to slot "
                                      + juce::String (slot + 1) + " of\n" + file.getFileName())
                                   : juce::String ("Could not write to that location."));
                        }), false);
                });
        });
}

void VDX7AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::accentDim);
    g.fillRect (0, 86, getWidth(), 2);
}

void VDX7AudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (8);

    auto top = r.removeFromTop (78);
    title.setBounds (top.removeFromLeft (150));
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


    r.removeFromTop (8);

    keyboard.setBounds (r.removeFromBottom (72));
    r.removeFromBottom (8);

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
        ops[(size_t) i]->setVisible (i == selectedOp_);
        opSelect[(size_t) i].setToggleState (i == selectedOp_, juce::dontSendNotification);
    }
}
