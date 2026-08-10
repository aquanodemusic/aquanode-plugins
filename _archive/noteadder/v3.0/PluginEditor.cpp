#include "PluginEditor.h"

// ── Colour palette ────────────────────────────────────────────
static const juce::Colour BG{ 0xff1a1b26 };
static const juce::Colour PANEL{ 0xff24283a };
static const juce::Colour PANEL_ALT{ 0xff1f2235 };
static const juce::Colour ACCENT{ 0xff7aa2f7 };
static const juce::Colour GREEN{ 0xff9ece6a };
static const juce::Colour ORANGE{ 0xffff9e64 };
static const juce::Colour RED{ 0xfff7768e };
static const juce::Colour PURPLE{ 0xffbb9af7 };
static const juce::Colour YELLOW{ 0xffe0af68 };
static const juce::Colour TEAL{ 0xff2ac3de };
static const juce::Colour TEXT_MAIN{ 0xffc0caf5 };
static const juce::Colour TEXT_DIM{ 0xff565f89 };
static const juce::Colour SEP{ 0xff414868 };
static const juce::Colour ANIM_COL{ 0xff73daca };

// ── Layout constants ──────────────────────────────────────────
static constexpr int W = 510;
static constexpr int HEADER_H = 52;

// Tab content pane (shared height for both manual and scale tabs)
static constexpr int TAB_Y = HEADER_H;              // 52
static constexpr int TAB_H = 298;                   // fixed pane height

// Animation section below the tab pane
static constexpr int ANIM_Y = TAB_Y + TAB_H;        // 350
static constexpr int ANIM_LBL_H = 22;
static constexpr int ANIM_ROW_H = 34;
static constexpr int ANIM_ROW1_Y = ANIM_Y + ANIM_LBL_H + 2;   // 374
static constexpr int ANIM_ROW2_Y = ANIM_ROW1_Y + ANIM_ROW_H;  // 408
static constexpr int ANIM_ROW3_Y = ANIM_ROW2_Y + ANIM_ROW_H;  // 442
static constexpr int ANIM_ROW4_Y = ANIM_ROW3_Y + ANIM_ROW_H;  // 476
static constexpr int TOTAL_H = ANIM_ROW4_Y + ANIM_ROW_H + 10; // 520

// Manual tab internal layout (relative to TAB_Y)
static constexpr int COL_HDR_OFFSET = 4;
static constexpr int COL_HDR_H = 18;
static constexpr int MANUAL_ROW_H = 36;
static constexpr int MANUAL_ROWS_Y = TAB_Y + COL_HDR_OFFSET + COL_HDR_H; // 74

// Scale tab internal layout (relative to TAB_Y)
static constexpr int SCALE_ROW1_Y = TAB_Y + 22;                          // row 1: root/scale/count
static constexpr int SCALE_ROW2_Y = SCALE_ROW1_Y + 36;                   // row 2: harmonic mode
static constexpr int SCALE_TOG1_Y = SCALE_ROW2_Y + 36;                   // toggle row 1
static constexpr int SCALE_TOG2_Y = SCALE_TOG1_Y + 30;                   // toggle row 2
static constexpr int SCALE_INV_Y = SCALE_TOG2_Y + 32;                   // inversion row
static constexpr int SCALE_PREV_Y = SCALE_INV_Y + 38;                   // chord preview

// ── Note / label helpers ──────────────────────────────────────
static const char* NOTE_NAMES[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static juce::String semiLabel(int st)
{
    static const char* names[] = {
        "Unison","Minor 2nd","Major 2nd","Minor 3rd","Major 3rd",
        "Perfect 4th","Tritone","Perfect 5th",
        "Minor 6th","Major 6th","Minor 7th","Major 7th","Octave"
    };
    const int a = std::abs(st);
    juce::String prefix = (st > 0) ? "+" : (st < 0 ? "-" : "");
    juce::String num = (st != 0) ? (juce::String(a) + "  ") : "";
    return prefix + num + names[a];
}

static juce::String octLabel(int o)
{
    if (o == 0) return "0 octaves";
    return ((o > 0) ? "+" : "") + juce::String(o)
        + (std::abs(o) == 1 ? " octave" : " octaves");
}

static juce::String invPickerName(int inv)
{
    if (inv == 0) return "Root position";
    static const char* suf[] = { "st","nd","rd","th","th","th","th" };
    return juce::String(inv) + suf[juce::jmin(inv - 1, 6)] + " inversion";
}

// ── Widget styling helpers ────────────────────────────────────
static void styleCombo(juce::ComboBox& cb)
{
    cb.setColour(juce::ComboBox::backgroundColourId, PANEL);
    cb.setColour(juce::ComboBox::textColourId, TEXT_MAIN);
    cb.setColour(juce::ComboBox::outlineColourId, SEP);
    cb.setColour(juce::ComboBox::arrowColourId, ACCENT);
}

static void styleToggle(juce::ToggleButton& btn, juce::Colour col)
{
    btn.setColour(juce::ToggleButton::tickColourId, col);
    btn.setColour(juce::ToggleButton::tickDisabledColourId, TEXT_DIM);
}

//==============================================================================
NoteAdderEditor::NoteAdderEditor(NoteAdderProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setSize(W, TOTAL_H);

    // ── Header ────────────────────────────────────────────────
    titleLabel.setText("NoteAdder", juce::dontSendNotification);
    titleLabel.setFont(juce::Font("Arial", 20.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addAndMakeVisible(titleLabel);

    for (auto* btn : { &manualBtn, &scaleBtn })
    {
        btn->setClickingTogglesState(false);
        addAndMakeVisible(btn);
    }
    manualBtn.onClick = [this]
        {
            *proc.modeParam = 0;
            // Only reset anim modes that require scale knowledge (Wander=2, Drift=3)
            const int a = proc.animModeParam->get();
            if (a == 2 || a == 3) *proc.animModeParam = 0;
            updateModeUI();
        };
    scaleBtn.onClick = [this] { *proc.modeParam = 1; updateModeUI(); };

    // Utility buttons (MANUAL tab only)
    randomizeBtn.setColour(juce::TextButton::buttonColourId, PURPLE.withAlpha(0.3f));
    randomizeBtn.setColour(juce::TextButton::textColourOffId, PURPLE);
    randomizeBtn.onClick = [this] { doManualRandomize(); };
    addChildComponent(randomizeBtn);

    savePresetBtn.setColour(juce::TextButton::buttonColourId, ACCENT.withAlpha(0.2f));
    savePresetBtn.setColour(juce::TextButton::textColourOffId, ACCENT);
    savePresetBtn.onClick = [this] { saveManualPreset(); };
    addChildComponent(savePresetBtn);

    loadPresetBtn.setColour(juce::TextButton::buttonColourId, TEAL.withAlpha(0.2f));
    loadPresetBtn.setColour(juce::TextButton::textColourOffId, TEAL);
    loadPresetBtn.onClick = [this] { loadManualPreset(); };
    addChildComponent(loadPresetBtn);

    // ── Build tab contents ─────────────────────────────────────
    buildManualTab();
    buildScaleTab();
    buildAnimSection();

    // ── APVTS attachments ─────────────────────────────────────
    // Created after widgets are populated so combo items exist
    createAttachments();

    // ── Dynamic combos and initial UI state ───────────────────
    repopulateRateCombo();
    repopulateAnimModeCombo();
    rebuildPickerCombo();
    updateModeUI();
    updateInversionUI();
    updateAnimUI();
    updateChordPreview();
}

NoteAdderEditor::~NoteAdderEditor() {}

//==============================================================================
void NoteAdderEditor::buildManualTab()
{
    const juce::Font dimFont(11.0f, juce::Font::bold);

    for (auto* lbl : { &colSemiHdr, &colOctHdr })
    {
        lbl->setFont(dimFont);
        lbl->setJustificationType(juce::Justification::centred);
        lbl->setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(*lbl);
    }
    colSemiHdr.setText("Semitones", juce::dontSendNotification);
    colOctHdr.setText("Octaves", juce::dontSendNotification);

    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
    {
        auto& r = rows[i];
        styleToggle(r.enableBtn, ACCENT);
        r.enableBtn.setButtonText({});
        addChildComponent(r.enableBtn);

        r.nameLabel.setText("Note " + juce::String(i + 1), juce::dontSendNotification);
        r.nameLabel.setFont(juce::Font(13.0f));
        r.nameLabel.setJustificationType(juce::Justification::centredRight);
        r.nameLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
        addChildComponent(r.nameLabel);

        populateSemiCombo(r.semiCombo); styleCombo(r.semiCombo);
        populateOctCombo(r.octCombo);  styleCombo(r.octCombo);
        addChildComponent(r.semiCombo);
        addChildComponent(r.octCombo);

        // onStateChange only triggers UI update; APVTS attachment handles param write
        r.enableBtn.onStateChange = [this, i] { updateRowEnabled(i); };
    }
}

//==============================================================================
void NoteAdderEditor::buildScaleTab()
{
    const juce::Font labelFont(12.0f, juce::Font::bold);

    // ── Row 1: Root / Scale / Count ───────────────────────────
    for (auto* lbl : { &rootLabel, &scaleLabel, &countLabel })
    {
        lbl->setFont(labelFont);
        lbl->setJustificationType(juce::Justification::centredRight);
        lbl->setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(*lbl);
    }
    rootLabel.setText("Root", juce::dontSendNotification);
    scaleLabel.setText("Scale", juce::dontSendNotification);
    countLabel.setText("Add", juce::dontSendNotification);

    populateRootCombo(rootCombo);  styleCombo(rootCombo);
    populateScaleCombo(scaleCombo); styleCombo(scaleCombo);
    populateCountCombo(countCombo); styleCombo(countCombo);
    countCombo.setColour(juce::ComboBox::arrowColourId, GREEN);
    addChildComponent(rootCombo);
    addChildComponent(scaleCombo);
    addChildComponent(countCombo);

    // onChange: UI-only (APVTS attachment handles param write)
    rootCombo.onChange = [this] { updateChordPreview(); };
    scaleCombo.onChange = [this] { updateChordPreview(); };
    countCombo.onChange = [this] { rebuildPickerCombo(); updateChordPreview(); };

    // ── Row 2: Harmonic mode ──────────────────────────────────
    harmonicModeLabel.setText("Harmony:", juce::dontSendNotification);
    harmonicModeLabel.setFont(labelFont);
    harmonicModeLabel.setJustificationType(juce::Justification::centredRight);
    harmonicModeLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(harmonicModeLabel);

    populateHarmonicCombo(harmonicModeCombo);
    styleCombo(harmonicModeCombo);
    harmonicModeCombo.setColour(juce::ComboBox::arrowColourId, YELLOW);
    addChildComponent(harmonicModeCombo);
    harmonicModeCombo.onChange = [this] { updateChordPreview(); };

    // ── Toggle row 1 ──────────────────────────────────────────
    styleToggle(discardBtn, RED);
    discardBtn.setButtonText({});
    discardLabel.setText("Discard out-of-scale", juce::dontSendNotification);
    discardLabel.setFont(juce::Font(12.0f));
    discardLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(discardBtn);
    addChildComponent(discardLabel);
    discardBtn.onStateChange = [this]
        {
            discardLabel.setColour(juce::Label::textColourId,
                discardBtn.getToggleState() ? RED : TEXT_MAIN);
        };

    styleToggle(discardInputBtn, ORANGE);
    discardInputBtn.setButtonText({});
    discardInputLabel.setText("Drop input note", juce::dontSendNotification);
    discardInputLabel.setFont(juce::Font(12.0f));
    discardInputLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(discardInputBtn);
    addChildComponent(discardInputLabel);
    discardInputBtn.onStateChange = [this]
        {
            discardInputLabel.setColour(juce::Label::textColourId,
                discardInputBtn.getToggleState() ? ORANGE : TEXT_MAIN);
        };

    // ── Toggle row 2 ──────────────────────────────────────────
    styleToggle(randomSkipBtn, YELLOW);
    randomSkipBtn.setButtonText({});
    randomSkipLabel.setText("Skip notes randomly", juce::dontSendNotification);
    randomSkipLabel.setFont(juce::Font(12.0f));
    randomSkipLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(randomSkipBtn);
    addChildComponent(randomSkipLabel);
    randomSkipBtn.onStateChange = [this]
        {
            randomSkipLabel.setColour(juce::Label::textColourId,
                randomSkipBtn.getToggleState() ? YELLOW : TEXT_MAIN);
        };

    styleToggle(lockBassBtn, TEAL);
    lockBassBtn.setButtonText({});
    lockBassLabel.setText("Lock bass note", juce::dontSendNotification);
    lockBassLabel.setFont(juce::Font(12.0f));
    lockBassLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(lockBassBtn);
    addChildComponent(lockBassLabel);
    lockBassBtn.onStateChange = [this]
        {
            lockBassLabel.setColour(juce::Label::textColourId,
                lockBassBtn.getToggleState() ? TEAL : TEXT_MAIN);
        };

    // ── Inversion row ─────────────────────────────────────────
    invLabel.setText("Inversion:", juce::dontSendNotification);
    invLabel.setFont(labelFont);
    invLabel.setJustificationType(juce::Justification::centredRight);
    invLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(invLabel);

    invModeCombo.addItem("Normal", 1);
    invModeCombo.addItem("Inversion Picker", 2);
    invModeCombo.addItem("Voice Leader", 3);
    invModeCombo.addItem("Drop 2", 4);
    invModeCombo.addItem("Cycle Up", 5);
    invModeCombo.addItem("Cycle Down", 6);
    invModeCombo.addItem("Random Cycle", 7);
    styleCombo(invModeCombo);
    invModeCombo.setColour(juce::ComboBox::arrowColourId, PURPLE);
    addChildComponent(invModeCombo);
    invModeCombo.onChange = [this] { updateInversionUI(); };

    styleCombo(invPickerCombo);
    invPickerCombo.setColour(juce::ComboBox::arrowColourId, PURPLE);
    addChildComponent(invPickerCombo);
    invPickerCombo.onChange = [this]
        {
            int id = invPickerCombo.getSelectedId();
            if (id > 0) *proc.inversionPickerParam = id - 1;
        };

    invPickerLabel.setText("Pick:", juce::dontSendNotification);
    invPickerLabel.setFont(labelFont);
    invPickerLabel.setJustificationType(juce::Justification::centredRight);
    invPickerLabel.setColour(juce::Label::textColourId, PURPLE.withAlpha(0.8f));
    addChildComponent(invPickerLabel);

    // ── Chord preview ─────────────────────────────────────────
    previewLabel.setFont(juce::Font("Arial", 12.0f, juce::Font::italic));
    previewLabel.setJustificationType(juce::Justification::centred);
    previewLabel.setColour(juce::Label::textColourId, ORANGE);
    addChildComponent(previewLabel);
}

//==============================================================================
void NoteAdderEditor::buildAnimSection()
{
    const juce::Font labelFont(12.0f, juce::Font::bold);
    const juce::Font smallFont(12.0f);

    // ── Row 1: mode + BPM sync ────────────────────────────────
    animModeLabel.setText("Animate:", juce::dontSendNotification);
    animModeLabel.setFont(labelFont);
    animModeLabel.setJustificationType(juce::Justification::centredRight);
    animModeLabel.setColour(juce::Label::textColourId, ANIM_COL.withAlpha(0.85f));
    addAndMakeVisible(animModeLabel);

    // animModeCombo is populated dynamically by repopulateAnimModeCombo()
    styleCombo(animModeCombo);
    animModeCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addAndMakeVisible(animModeCombo);
    animModeCombo.onChange = [this]
        {
            int id = animModeCombo.getSelectedId();
            if (id > 0) *proc.animModeParam = id - 1;
            updateAnimUI();
        };

    styleToggle(animSyncBtn, ANIM_COL);
    animSyncBtn.setButtonText({});
    addAndMakeVisible(animSyncBtn);
    animSyncBtn.onStateChange = [this]
        {
            repopulateRateCombo();
            animSyncLabel.setColour(juce::Label::textColourId,
                animSyncBtn.getToggleState() ? ANIM_COL : TEXT_MAIN);
        };

    animSyncLabel.setText("BPM sync", juce::dontSendNotification);
    animSyncLabel.setFont(smallFont);
    animSyncLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addAndMakeVisible(animSyncLabel);

    // ── Row 2: rate ───────────────────────────────────────────
    animRateLabel.setText("Rate:", juce::dontSendNotification);
    animRateLabel.setFont(labelFont);
    animRateLabel.setJustificationType(juce::Justification::centredRight);
    animRateLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(animRateLabel);

    styleCombo(animRateCombo);
    animRateCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addAndMakeVisible(animRateCombo);
    animRateCombo.onChange = [this]
        {
            int id = animRateCombo.getSelectedId();
            if (id <= 0) return;
            if (proc.animSyncBPMParam->get())
                *proc.animRateParam = id - 1;
            else
                *proc.animRateFreeParam = id - 1;
        };

    // ── Row 3: Wander params ──────────────────────────────────
    wanderProbLabel.setText("Prob:", juce::dontSendNotification);
    wanderProbLabel.setFont(labelFont);
    wanderProbLabel.setJustificationType(juce::Justification::centredRight);
    wanderProbLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(wanderProbLabel);

    populateWanderProbCombo(wanderProbCombo); styleCombo(wanderProbCombo);
    wanderProbCombo.setColour(juce::ComboBox::arrowColourId, GREEN);
    addAndMakeVisible(wanderProbCombo);

    wanderMaxLabel.setText("Spread:", juce::dontSendNotification);
    wanderMaxLabel.setFont(labelFont);
    wanderMaxLabel.setJustificationType(juce::Justification::centredRight);
    wanderMaxLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(wanderMaxLabel);

    populateWanderMaxCombo(wanderMaxCombo); styleCombo(wanderMaxCombo);
    wanderMaxCombo.setColour(juce::ComboBox::arrowColourId, GREEN);
    addAndMakeVisible(wanderMaxCombo);

    // ── Row 3 (alt): Cloud density / spread / decay ───────────
    cloudDensityLabel.setText("Density:", juce::dontSendNotification);
    cloudDensityLabel.setFont(labelFont);
    cloudDensityLabel.setJustificationType(juce::Justification::centredRight);
    cloudDensityLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudDensityLabel);

    populateCloudDensCombo(cloudDensityCombo); styleCombo(cloudDensityCombo);
    cloudDensityCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudDensityCombo);

    cloudSpreadLabel.setText("Spread:", juce::dontSendNotification);
    cloudSpreadLabel.setFont(labelFont);
    cloudSpreadLabel.setJustificationType(juce::Justification::centredRight);
    cloudSpreadLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudSpreadLabel);

    populateCloudSpreadCombo(cloudSpreadCombo); styleCombo(cloudSpreadCombo);
    cloudSpreadCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudSpreadCombo);

    cloudDecayLabel.setText("Decay:", juce::dontSendNotification);
    cloudDecayLabel.setFont(labelFont);
    cloudDecayLabel.setJustificationType(juce::Justification::centredRight);
    cloudDecayLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudDecayLabel);

    populateCloudDecayCombo(cloudDecayCombo); styleCombo(cloudDecayCombo);
    cloudDecayCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudDecayCombo);

    // ── Row 4: Cloud velocity range ───────────────────────────
    cloudVelLabel.setText("Velocity:", juce::dontSendNotification);
    cloudVelLabel.setFont(labelFont);
    cloudVelLabel.setJustificationType(juce::Justification::centredRight);
    cloudVelLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudVelLabel);

    cloudVelDashLabel.setText("to", juce::dontSendNotification);
    cloudVelDashLabel.setFont(juce::Font(12.0f));
    cloudVelDashLabel.setJustificationType(juce::Justification::centred);
    cloudVelDashLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudVelDashLabel);

    populateCloudVelCombo(cloudVelMinCombo); styleCombo(cloudVelMinCombo);
    cloudVelMinCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudVelMinCombo);

    populateCloudVelCombo(cloudVelMaxCombo); styleCombo(cloudVelMaxCombo);
    cloudVelMaxCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudVelMaxCombo);
}

//==============================================================================
void NoteAdderEditor::createAttachments()
{
    auto& vts = proc.apvts;

    // Scale combos
    rootComboAtt = std::make_unique<CBA>(vts, "root", rootCombo);
    scaleComboAtt = std::make_unique<CBA>(vts, "scale", scaleCombo);
    countComboAtt = std::make_unique<CBA>(vts, "count", countCombo);
    harmonicModeComboAtt = std::make_unique<CBA>(vts, "harmonicmode", harmonicModeCombo);

    // Scale toggles
    discardAtt = std::make_unique<BA>(vts, "discard", discardBtn);
    discardInputAtt = std::make_unique<BA>(vts, "discardInput", discardInputBtn);
    randomSkipAtt = std::make_unique<BA>(vts, "randomskip", randomSkipBtn);
    lockBassAtt = std::make_unique<BA>(vts, "lockbass", lockBassBtn);

    // Inversion mode combo (picker is dynamic – managed manually)
    invModeComboAtt = std::make_unique<CBA>(vts, "invmode", invModeCombo);

    // Animation sync toggle
    animSyncAtt = std::make_unique<BA>(vts, "animsync", animSyncBtn);

    // Wander
    wanderProbComboAtt = std::make_unique<CBA>(vts, "wanderprob", wanderProbCombo);
    wanderMaxComboAtt = std::make_unique<CBA>(vts, "wandermax", wanderMaxCombo);

    // Cloud params
    cloudDensComboAtt = std::make_unique<CBA>(vts, "clouddensity", cloudDensityCombo);
    cloudSpreadComboAtt = std::make_unique<CBA>(vts, "cloudspread", cloudSpreadCombo);
    cloudDecayComboAtt = std::make_unique<CBA>(vts, "clouddecay", cloudDecayCombo);
    cloudVelMinComboAtt = std::make_unique<CBA>(vts, "cloudvelmin", cloudVelMinCombo);
    cloudVelMaxComboAtt = std::make_unique<CBA>(vts, "cloudvelmax", cloudVelMaxCombo);

    // Manual rows
    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
    {
        juce::String s(i);
        enabledAtts[i] = std::make_unique<BA>(vts, "enabled" + s, rows[i].enableBtn);
        semiComboAtts[i] = std::make_unique<CBA>(vts, "semi" + s, rows[i].semiCombo);
        octComboAtts[i] = std::make_unique<CBA>(vts, "oct" + s, rows[i].octCombo);
    }
}

//==============================================================================
// ── Dynamic combo helpers ─────────────────────────────────────────────────────

void NoteAdderEditor::repopulateAnimModeCombo()
{
    const int curAnimMode = proc.animModeParam->get();
    const bool isScale = (proc.modeParam->get() == 1);

    animModeCombo.clear(juce::dontSendNotification);
    animModeCombo.addItem("Off", 1);
    animModeCombo.addItem("Re-Voice", 2);
    animModeCombo.addItem("Cloud", 3);   // available in both modes

    if (isScale)
    {
        animModeCombo.addItem("Wander", 4);
        animModeCombo.addItem("Drift", 5);
    }

    // Combo IDs always equal param value + 1 so setSelectedId(param+1) works
    // regardless of which items are present.
    // Off=0, ReVoice=1, Wander=2, Drift=3, Cloud=4
    animModeCombo.clear(juce::dontSendNotification);
    animModeCombo.addItem("Off", 1);   // param 0  – both modes
    animModeCombo.addItem("Re-Voice", 2);   // param 1  – both modes
    if (isScale) animModeCombo.addItem("Wander", 3);   // param 2  – scale only
    if (isScale) animModeCombo.addItem("Drift", 4);   // param 3  – scale only
    animModeCombo.addItem("Cloud", 5);   // param 4  – both modes

    // Clamp: if we're in manual mode with a scale-only anim mode, reset to Off
    int safeAnimMode = curAnimMode;
    if (!isScale && (curAnimMode == 2 || curAnimMode == 3)) safeAnimMode = 0;
    animModeCombo.setSelectedId(safeAnimMode + 1, juce::dontSendNotification);
}

void NoteAdderEditor::repopulateRateCombo()
{
    animRateCombo.clear(juce::dontSendNotification);

    if (proc.animSyncBPMParam->get())
    {
        static const char* labels[NoteAdderProcessor::NUM_SUBDIVS] = {
            "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_SUBDIVS; ++i)
            animRateCombo.addItem(labels[i], i + 1);
        animRateCombo.setSelectedId(proc.animRateParam->get() + 1, juce::dontSendNotification);
    }
    else
    {
        static const char* labels[NoteAdderProcessor::NUM_FREE_RATES] = {
            "50 ms","75 ms","100 ms","150 ms","200 ms","300 ms",
            "400 ms","500 ms","750 ms","1000 ms","1500 ms","2000 ms"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_FREE_RATES; ++i)
            animRateCombo.addItem(labels[i], i + 1);
        animRateCombo.setSelectedId(proc.animRateFreeParam->get() + 1, juce::dontSendNotification);
    }
}

void NoteAdderEditor::rebuildPickerCombo()
{
    const int currentVal = proc.inversionPickerParam->get();
    const int noteCount = proc.noteCountParam->get();

    invPickerCombo.clear(juce::dontSendNotification);
    for (int inv = 0; inv <= noteCount; ++inv)
        invPickerCombo.addItem(invPickerName(inv), inv + 1);

    int clamped = juce::jmin(currentVal, noteCount);
    *proc.inversionPickerParam = clamped;
    invPickerCombo.setSelectedId(clamped + 1, juce::dontSendNotification);
}

//==============================================================================
// ── UI-state updaters ─────────────────────────────────────────────────────────

void NoteAdderEditor::updateModeUI()
{
    const bool isScale = (proc.modeParam->get() == 1);

    auto styleBtn = [](juce::TextButton& btn, bool active, juce::Colour col)
        {
            btn.setColour(juce::TextButton::buttonColourId, active ? col : PANEL);
            btn.setColour(juce::TextButton::textColourOffId, active ? BG : TEXT_DIM);
            btn.setColour(juce::TextButton::textColourOnId, active ? BG : TEXT_DIM);
        };
    styleBtn(manualBtn, !isScale, ACCENT);
    styleBtn(scaleBtn, isScale, GREEN);

    // Header utility buttons
    randomizeBtn.setVisible(!isScale);
    savePresetBtn.setVisible(!isScale);
    loadPresetBtn.setVisible(!isScale);

    // Manual-tab components
    colSemiHdr.setVisible(!isScale);
    colOctHdr.setVisible(!isScale);
    for (auto& r : rows)
    {
        r.enableBtn.setVisible(!isScale);
        r.nameLabel.setVisible(!isScale);
        r.semiCombo.setVisible(!isScale);
        r.octCombo.setVisible(!isScale);
    }

    // Scale-tab components
    auto setScaleVisible = [&](bool v)
        {
            rootLabel.setVisible(v);         rootCombo.setVisible(v);
            scaleLabel.setVisible(v);        scaleCombo.setVisible(v);
            countLabel.setVisible(v);        countCombo.setVisible(v);
            harmonicModeLabel.setVisible(v); harmonicModeCombo.setVisible(v);
            discardBtn.setVisible(v);        discardLabel.setVisible(v);
            discardInputBtn.setVisible(v);   discardInputLabel.setVisible(v);
            randomSkipBtn.setVisible(v);     randomSkipLabel.setVisible(v);
            lockBassBtn.setVisible(v);       lockBassLabel.setVisible(v);
            invLabel.setVisible(v);          invModeCombo.setVisible(v);
            previewLabel.setVisible(v);
        };
    setScaleVisible(isScale);

    // invPickerCombo/label visibility also handled in updateInversionUI
    updateInversionUI();

    // Animation: repopulate mode combo for the current tab
    repopulateAnimModeCombo();
    updateAnimUI();

    // Update row enabled states in manual mode
    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        updateRowEnabled(i);

    updateChordPreview();
    repaint();
}

void NoteAdderEditor::updateRowEnabled(int i)
{
    const bool rowOn = rows[i].enableBtn.getToggleState();
    rows[i].semiCombo.setEnabled(rowOn);
    rows[i].octCombo.setEnabled(rowOn);
    rows[i].nameLabel.setColour(juce::Label::textColourId, rowOn ? TEXT_MAIN : TEXT_DIM);
}

void NoteAdderEditor::updateInversionUI()
{
    const bool isScale = (proc.modeParam->get() == 1);
    const bool isPicker = isScale && (proc.inversionModeParam->get() == 1);
    invPickerCombo.setVisible(isPicker);
    invPickerLabel.setVisible(isPicker);
}

void NoteAdderEditor::updateAnimUI()
{
    const int  animMode = proc.animModeParam->get();
    const bool animOn = (animMode != 0);

    animRateLabel.setEnabled(animOn);
    animRateCombo.setEnabled(animOn);
    animSyncBtn.setEnabled(animOn);
    animSyncLabel.setEnabled(animOn);

    const bool isWander = animOn && (animMode == 2);
    wanderProbLabel.setVisible(isWander);
    wanderProbCombo.setVisible(isWander);
    wanderMaxLabel.setVisible(isWander);
    wanderMaxCombo.setVisible(isWander);

    const bool isCloud = animOn && (animMode == 4);
    cloudDensityLabel.setVisible(isCloud);
    cloudDensityCombo.setVisible(isCloud);
    cloudSpreadLabel.setVisible(isCloud);
    cloudSpreadCombo.setVisible(isCloud);
    cloudDecayLabel.setVisible(isCloud);
    cloudDecayCombo.setVisible(isCloud);
    cloudVelLabel.setVisible(isCloud);
    cloudVelDashLabel.setVisible(isCloud);
    cloudVelMinCombo.setVisible(isCloud);
    cloudVelMaxCombo.setVisible(isCloud);

    repaint();
}

void NoteAdderEditor::updateChordPreview()
{
    if (proc.modeParam->get() != 1)
    {
        previewLabel.setText({}, juce::dontSendNotification);
        return;
    }

    const int  root = proc.rootNoteParam->get();
    const int  type = proc.scaleTypeParam->get();
    const int  count = proc.noteCountParam->get();
    const int  hmode = proc.harmonicModeParam->get();
    const int  sz = NoteAdderProcessor::SCALE_SIZES[type];
    const int* ivs = NoteAdderProcessor::SCALE_INTERVALS[type];

    static const char* hmodeNames[] = { "secundal", "tertian", "quartal", "quintal", "sextal", "septimal" };
    static const int   stepSizes[] = { 1, 2, 3, 4, 5, 6 };
    const int step = stepSizes[juce::jlimit(0, 5, hmode)];

    juce::String text = juce::String(NOTE_NAMES[root]);

    for (int k = 1; k <= count; ++k)
    {
        int targetDeg = step * k;
        int degInScale = targetDeg % sz;
        int noteClass = (root + ivs[degInScale]) % 12;
        text += "  " + juce::String(NOTE_NAMES[noteClass]);
    }

    text += "   (" + juce::String(NOTE_NAMES[root])
        + " " + juce::String(NoteAdderProcessor::SCALE_NAMES[type])
        + ", " + juce::String(count + 1) + " notes, "
        + hmodeNames[hmode] + ")";

    previewLabel.setText(text, juce::dontSendNotification);
}

//==============================================================================
// ── Preset save / load ────────────────────────────────────────────────────────

void NoteAdderEditor::saveManualPreset()
{
    fileChooser = std::make_shared<juce::FileChooser>(
        "Save Manual Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.xml");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            file = file.withFileExtension("xml");

            juce::XmlElement xml("NoteAdderManualPreset");
            for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
            {
                auto* row = xml.createNewChildElement("Row");
                row->setAttribute("enabled", proc.enabledParams[i]->get() ? 1 : 0);
                row->setAttribute("semi", proc.semiParams[i]->get());
                row->setAttribute("oct", proc.octaveParams[i]->get());
            }
            xml.writeTo(file);
        });
}

void NoteAdderEditor::loadManualPreset()
{
    fileChooser = std::make_shared<juce::FileChooser>(
        "Load Manual Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.xml");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;

            auto xml = juce::XmlDocument::parse(file);
            if (!xml) return;

            int rowIdx = 0;
            for (auto* row : xml->getChildWithTagNameIterator("Row"))
            {
                if (rowIdx >= NoteAdderProcessor::NUM_ROWS) break;
                *proc.enabledParams[rowIdx] = row->getIntAttribute("enabled", 0) != 0;
                *proc.semiParams[rowIdx] = row->getIntAttribute("semi", 0);
                *proc.octaveParams[rowIdx] = row->getIntAttribute("oct", 0);

                // Sync the UI from the freshly-written param values
                rows[rowIdx].enableBtn.setToggleState(
                    proc.enabledParams[rowIdx]->get(), juce::sendNotification);
                rows[rowIdx].semiCombo.setSelectedId(
                    semiToId(proc.semiParams[rowIdx]->get()), juce::sendNotification);
                rows[rowIdx].octCombo.setSelectedId(
                    octToId(proc.octaveParams[rowIdx]->get()), juce::sendNotification);

                ++rowIdx;
            }
        });
}

//==============================================================================
// ── Randomize ─────────────────────────────────────────────────────────────────

void NoteAdderEditor::doManualRandomize()
{
    juce::Random r;
    static const int pool[] = { -12,-7,-5,-4,-3,0,3,4,5,7,8,9,12 };
    static const int poolSize = 13;

    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
    {
        bool enabled = (r.nextFloat() > 0.45f);
        int  semi = pool[r.nextInt(poolSize)];
        int  oct = r.nextInt(4) - 1;

        *proc.enabledParams[i] = enabled;
        *proc.semiParams[i] = semi;
        *proc.octaveParams[i] = oct;

        rows[i].enableBtn.setToggleState(enabled, juce::sendNotification);
        rows[i].semiCombo.setSelectedId(semiToId(semi), juce::sendNotification);
        rows[i].octCombo.setSelectedId(octToId(oct), juce::sendNotification);
    }
}

//==============================================================================
// ── Combo populators ──────────────────────────────────────────────────────────

void NoteAdderEditor::populateSemiCombo(juce::ComboBox& cb)
{
    for (int st = -12; st <= 12; ++st)
        cb.addItem(semiLabel(st), semiToId(st));
}

void NoteAdderEditor::populateOctCombo(juce::ComboBox& cb)
{
    for (int o = -7; o <= 7; ++o)
        cb.addItem(octLabel(o), octToId(o));
}

void NoteAdderEditor::populateRootCombo(juce::ComboBox& cb)
{
    for (int n = 0; n < 12; ++n)
        cb.addItem(NOTE_NAMES[n], n + 1);
}

void NoteAdderEditor::populateScaleCombo(juce::ComboBox& cb)
{
    // Must match NoteAdderProcessor::SCALE_NAMES order exactly
    cb.addItem("Major", 1);
    cb.addItem("Minor", 2);
    cb.addItem("Penta Major", 3);
    cb.addItem("Penta Minor", 4);
    cb.addItem("Lydian", 5);
    cb.addItem("Mixolydian", 6);
    cb.addItem("Dorian", 7);
    cb.addItem("Phrygian", 8);
    cb.addItem("Locrian", 9);
    cb.addItem("Harmonic Minor", 10);
    cb.addItem("Melodic Minor", 11);
    cb.addItem("Lydian Dom.", 12);
    cb.addItem("Phrygian Dom.", 13);
    cb.addItem("Altered", 14);
    cb.addItem("Whole Tone", 15);
    cb.addItem("Blues", 16);
}

void NoteAdderEditor::populateCountCombo(juce::ComboBox& cb)
{
    cb.addItem("0 notes", 1);
    for (int n = 1; n <= 7; ++n)
        cb.addItem(juce::String(n) + (n == 1 ? " note" : " notes"), n + 1);
}

void NoteAdderEditor::populateHarmonicCombo(juce::ComboBox& cb)
{
    cb.addItem("Secundal (2nds / tone clusters)", 1);
    cb.addItem("Tertian (3rds, classic)", 2);
    cb.addItem("Quartal (4ths)", 3);
    cb.addItem("Quintal (5ths)", 4);
    cb.addItem("Sextal (6ths)", 5);
    cb.addItem("Septimal (7ths)", 6);
}

void NoteAdderEditor::populateWanderProbCombo(juce::ComboBox& cb)
{
    for (int i = 0; i < 10; ++i)
        cb.addItem(juce::String((i + 1) * 10) + " %", i + 1);
}

void NoteAdderEditor::populateWanderMaxCombo(juce::ComboBox& cb)
{
    for (int i = 0; i < 7; ++i)
        cb.addItem(juce::String(i + 1) + " deg", i + 1);
}

void NoteAdderEditor::populateCloudDensCombo(juce::ComboBox& cb)
{
    for (int i = 1; i <= 8; ++i)
        cb.addItem(juce::String(i) + (i == 1 ? " note" : " notes"), i);
}

void NoteAdderEditor::populateCloudSpreadCombo(juce::ComboBox& cb)
{
    for (int i = 1; i <= 7; ++i)
        cb.addItem(juce::String(i) + " deg", i);
}

void NoteAdderEditor::populateCloudDecayCombo(juce::ComboBox& cb)
{
    cb.addItem("Very Short (~80ms)", 1);
    cb.addItem("Short (~200ms)", 2);
    cb.addItem("Medium (~400ms)", 3);
    cb.addItem("Long (~700ms)", 4);
    cb.addItem("Very Long (~1.4s)", 5);
}

void NoteAdderEditor::populateCloudVelCombo(juce::ComboBox& cb)
{
    static const char* names[] = {
        "ppp (8)","pp (16)","p (24)","mp (32)","mf (48)",
        "f (64)","ff (80)","fff (96)","ffff (112)","max (127)"
    };
    for (int i = 0; i < 10; ++i)
        cb.addItem(names[i], i + 1);
}

//==============================================================================
// ── paint ─────────────────────────────────────────────────────────────────────

void NoteAdderEditor::paint(juce::Graphics& g)
{
    g.fillAll(BG);

    // ── Header gradient ───────────────────────────────────────
    g.setGradientFill(juce::ColourGradient(
        PANEL.brighter(0.05f), 0.0f, 0.0f,
        BG, 0.0f, (float)HEADER_H, false));
    g.fillRect(0, 0, W, HEADER_H);
    g.setColour(SEP);
    g.fillRect(10, HEADER_H - 1, W - 20, 1);

    // ── Tab pane background ───────────────────────────────────
    const bool isScale = (proc.modeParam->get() == 1);

    g.setColour(isScale ? GREEN.withAlpha(0.03f) : ACCENT.withAlpha(0.03f));
    g.fillRect(0, TAB_Y, W, TAB_H);

    // Subtle tab active indicator on the top edge
    g.setColour(isScale ? GREEN.withAlpha(0.35f) : ACCENT.withAlpha(0.35f));
    g.fillRect(0, TAB_Y, W, 2);

    if (!isScale)
    {
        // Manual: stripe the rows
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        {
            g.setColour((i % 2 == 0 ? PANEL : PANEL_ALT).withAlpha(0.75f));
            g.fillRect(0, MANUAL_ROWS_Y + i * MANUAL_ROW_H, W, MANUAL_ROW_H);
        }
    }
    else
    {
        // Scale: subtle inversion row highlight
        g.setColour(PURPLE.withAlpha(0.06f));
        g.fillRect(0, SCALE_INV_Y, W, 38);
        g.setColour(PURPLE.withAlpha(0.18f));
        g.fillRect(10, SCALE_INV_Y, W - 20, 1);
        g.fillRect(10, SCALE_INV_Y + 37, W - 20, 1);
    }

    // ── Separator before anim section ─────────────────────────
    g.setColour(ANIM_COL.withAlpha(0.22f));
    g.fillRect(10, ANIM_Y + 1, W - 20, 1);

    g.setColour(ANIM_COL.withAlpha(0.04f));
    g.fillRect(0, ANIM_Y, W, TOTAL_H - ANIM_Y);

    // ── Anim section label ────────────────────────────────────
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.setColour(proc.animModeParam->get() != 0
        ? ANIM_COL.withAlpha(0.9f) : TEXT_DIM.withAlpha(0.5f));
    g.drawText("ANIMATION", 10, ANIM_Y + 4, 120, 16,
        juce::Justification::centredLeft, false);

    // ── Hint text for modes with no extra params ──────────────
    const int curAnim = proc.animModeParam->get();
    const bool isScaleForHint = (proc.modeParam->get() == 1);

    // Only draw hint text for modes that have no param widgets on row 3.
    // Cloud (4) shows its own param widgets there, so no hint needed.
    if (curAnim == 1 || curAnim == 3)
    {
        static const char* hints[] = {
            "",
            "Cycles through all inversions of the chord on each tick.",
            "",
            "Chord root walks up the scale one degree (note in the scale) per tick.",
        };
        g.setFont(juce::Font("Arial", 11.0f, juce::Font::italic));
        g.setColour(ANIM_COL.withAlpha(0.5f));
        g.drawText(hints[curAnim], 12, ANIM_ROW3_Y + 6, W - 24, 22,
            juce::Justification::centredLeft, false);
    }

    // ── Mode labels (dimmed section titles) ───────────────────
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    if (!isScale)
    {
        g.setColour(ACCENT.withAlpha(0.5f));
        g.drawText("MANUAL MODE", 10, TAB_Y + 4, 130, 14,
            juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour(GREEN.withAlpha(0.5f));
        g.drawText("SCALE MODE", 10, TAB_Y + 4, 130, 14,
            juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// ── resized ───────────────────────────────────────────────────────────────────

void NoteAdderEditor::resized()
{
    const int margin = 12;
    const int comboH = 26;
    const int toggleW = 22;
    const int gap = 6;

    // ── Header ────────────────────────────────────────────────
    {
        const int btnH = 28, btnY = 12;
        const int tabW = 74, tabGap = 5;

        // Tab buttons flush right
        scaleBtn.setBounds(W - margin - tabW, btnY, tabW, btnH);
        manualBtn.setBounds(W - margin - tabW * 2 - tabGap, btnY, tabW, btnH);

        // Utility buttons left of tabs
        const int utilW = 46, utilGap = 4;
        int utilX = W - margin - tabW * 2 - tabGap - utilGap - utilW;
        loadPresetBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;
        savePresetBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;
        randomizeBtn.setBounds(utilX, btnY, utilW, btnH);

        titleLabel.setBounds(margin, 10, utilX - margin - 8, 28);
    }

    // ── Manual tab: column headers ────────────────────────────
    {
        const int semiW = 160, octW = 130;
        const int labelW = 56;
        const int semiX = margin + toggleW + gap + labelW + gap;
        const int octX = semiX + semiW + gap;
        colSemiHdr.setBounds(semiX, TAB_Y + COL_HDR_OFFSET + 2, semiW, 14);
        colOctHdr.setBounds(octX, TAB_Y + COL_HDR_OFFSET + 2, octW, 14);

        // Manual rows
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        {
            const int rowY = MANUAL_ROWS_Y + i * MANUAL_ROW_H;
            const int cy = rowY + (MANUAL_ROW_H - comboH) / 2;
            rows[i].enableBtn.setBounds(margin, rowY + (MANUAL_ROW_H - 22) / 2, toggleW, 22);
            rows[i].nameLabel.setBounds(margin + toggleW + gap, cy, labelW, comboH);
            rows[i].semiCombo.setBounds(semiX, cy, semiW, comboH);
            rows[i].octCombo.setBounds(octX, cy, octW, comboH);
        }
    }

    // ── Scale tab ─────────────────────────────────────────────
    {
        // Row 1: Root / Scale / Count
        {
            const int cy = SCALE_ROW1_Y + (36 - comboH) / 2;
            const int rLblW = 32, rCmbW = 58;
            const int sLblW = 50, sCmbW = 136;
            const int cLblW = 28, cCmbW = 80;
            int x = margin;
            rootLabel.setBounds(x, cy, rLblW, comboH); x += rLblW + 4;
            rootCombo.setBounds(x, cy, rCmbW, comboH); x += rCmbW + 10;
            scaleLabel.setBounds(x, cy, sLblW, comboH); x += sLblW + 4;
            scaleCombo.setBounds(x, cy, sCmbW, comboH); x += sCmbW + 10;
            countLabel.setBounds(x, cy, cLblW, comboH); x += cLblW + 4;
            countCombo.setBounds(x, cy, cCmbW, comboH);
        }

        // Row 2: Harmonic mode
        {
            const int cy = SCALE_ROW2_Y + (36 - comboH) / 2;
            const int hLblW = 62, hCmbW = 250;
            int x = margin;
            harmonicModeLabel.setBounds(x, cy, hLblW, comboH); x += hLblW + 4;
            harmonicModeCombo.setBounds(x, cy, hCmbW, comboH);
        }

        // Toggle row 1
        {
            const int ty = SCALE_TOG1_Y + (30 - 22) / 2;
            int x = margin;
            discardBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            discardLabel.setBounds(x, ty, 155, 22); x += 155 + 18;
            discardInputBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            discardInputLabel.setBounds(x, ty, 155, 22);
        }

        // Toggle row 2
        {
            const int ty = SCALE_TOG2_Y + (30 - 22) / 2;
            int x = margin;
            randomSkipBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            randomSkipLabel.setBounds(x, ty, 155, 22); x += 155 + 18;
            lockBassBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            lockBassLabel.setBounds(x, ty, 155, 22);
        }

        // Inversion row
        {
            const int cy = SCALE_INV_Y + (38 - comboH) / 2;
            const int iLblW = 68, iCmbW = 150;
            const int pLblW = 38, pCmbW = 136;
            int x = margin;
            invLabel.setBounds(x, cy, iLblW, comboH); x += iLblW + 4;
            invModeCombo.setBounds(x, cy, iCmbW, comboH); x += iCmbW + 14;
            invPickerLabel.setBounds(x, cy, pLblW, comboH); x += pLblW + 4;
            invPickerCombo.setBounds(x, cy, pCmbW, comboH);
        }

        // Chord preview
        previewLabel.setBounds(margin, SCALE_PREV_Y + 2, W - margin * 2, 24);
    }

    // ── Animation row 1: mode + sync ─────────────────────────
    {
        const int cy = ANIM_ROW1_Y + (ANIM_ROW_H - comboH) / 2;
        const int lblW = 62, modeCmbW = 128;
        const int syncTogW = 22, syncLblW = 80;
        int x = margin;
        animModeLabel.setBounds(x, cy, lblW, comboH); x += lblW + 4;
        animModeCombo.setBounds(x, cy, modeCmbW, comboH); x += modeCmbW + 20;
        animSyncBtn.setBounds(x, cy, syncTogW, 22);     x += syncTogW + 4;
        animSyncLabel.setBounds(x, cy, syncLblW, comboH);
    }

    // ── Animation row 2: rate ─────────────────────────────────
    {
        const int cy = ANIM_ROW2_Y + (ANIM_ROW_H - comboH) / 2;
        animRateLabel.setBounds(margin, cy, 38, comboH);
        animRateCombo.setBounds(margin + 38 + 4, cy, 120, comboH);
    }

    // ── Animation row 3: Wander / Cloud params ────────────────
    {
        const int cy = ANIM_ROW3_Y + (ANIM_ROW_H - comboH) / 2;

        // Wander
        {
            const int pLblW = 38, pCmbW = 80;
            const int mLblW = 52, mCmbW = 80;
            int x = margin;
            wanderProbLabel.setBounds(x, cy, pLblW, comboH); x += pLblW + 4;
            wanderProbCombo.setBounds(x, cy, pCmbW, comboH); x += pCmbW + 16;
            wanderMaxLabel.setBounds(x, cy, mLblW, comboH); x += mLblW + 4;
            wanderMaxCombo.setBounds(x, cy, mCmbW, comboH);
        }

        // Cloud row 3
        {
            const int dLblW = 52, dCmbW = 80;
            const int sLblW = 52, sCmbW = 72;
            const int dcLblW = 44, dcCmbW = 130;
            int x = margin;
            cloudDensityLabel.setBounds(x, cy, dLblW, comboH); x += dLblW + 4;
            cloudDensityCombo.setBounds(x, cy, dCmbW, comboH); x += dCmbW + 16;
            cloudSpreadLabel.setBounds(x, cy, sLblW, comboH); x += sLblW + 4;
            cloudSpreadCombo.setBounds(x, cy, sCmbW, comboH); x += sCmbW + 16;
            cloudDecayLabel.setBounds(x, cy, dcLblW, comboH); x += dcLblW + 4;
            cloudDecayCombo.setBounds(x, cy, dcCmbW, comboH);
        }
    }

    // ── Animation row 4: Cloud velocity range ─────────────────
    {
        const int cy = ANIM_ROW4_Y + (ANIM_ROW_H - comboH) / 2;
        const int vCmbW = 110, dashW = 26;
        int x = margin;
        cloudVelLabel.setBounds(x, cy, 58, comboH); x += 58 + 4;
        cloudVelMinCombo.setBounds(x, cy, vCmbW, comboH); x += vCmbW + 4;
        cloudVelDashLabel.setBounds(x, cy, dashW, comboH); x += dashW + 4;
        cloudVelMaxCombo.setBounds(x, cy, vCmbW, comboH);
    }
}