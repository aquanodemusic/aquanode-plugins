#include "PluginEditor.h"

// ── Palette ───────────────────────────────────────────────────
static const juce::Colour BG       { 0xff1a1b26 };
static const juce::Colour PANEL    { 0xff24283a };
static const juce::Colour PANEL_ALT{ 0xff1f2235 };
static const juce::Colour ACCENT   { 0xff7aa2f7 };
static const juce::Colour GREEN    { 0xff9ece6a };
static const juce::Colour ORANGE   { 0xffff9e64 };
static const juce::Colour RED      { 0xfff7768e };
static const juce::Colour PURPLE   { 0xffbb9af7 };
static const juce::Colour YELLOW   { 0xffe0af68 };
static const juce::Colour TEAL     { 0xff2ac3de };
static const juce::Colour TEXT_MAIN{ 0xffc0caf5 };
static const juce::Colour TEXT_DIM { 0xff565f89 };
static const juce::Colour SEP      { 0xff414868 };
static const juce::Colour ANIM_COL { 0xff73daca }; // mint green

// ── Layout ────────────────────────────────────────────────────
static constexpr int W          = 510;
static constexpr int HEADER_H   = 52;
static constexpr int COL_HDR_H  = 20;
static constexpr int ROW_H      = 42;
static constexpr int MANUAL_Y   = HEADER_H + COL_HDR_H;
static constexpr int MANUAL_H   = NoteAdderProcessor::NUM_ROWS * ROW_H;
static constexpr int SEP_Y      = MANUAL_Y + MANUAL_H;
static constexpr int SCALELBL_Y = SEP_Y + 8;
static constexpr int SCALE_Y    = SCALELBL_Y + 20;
static constexpr int SCALE_H    = 38;
static constexpr int TOGGLE1_Y  = SCALE_Y + SCALE_H + 4;
static constexpr int TOGGLE_H   = 30;
static constexpr int TOGGLE2_Y  = TOGGLE1_Y + TOGGLE_H + 2;
static constexpr int INV_Y      = TOGGLE2_Y + TOGGLE_H + 6;
static constexpr int INV_H      = 38;
static constexpr int PREV_Y     = INV_Y + INV_H + 4;
static constexpr int PREV_H     = 26;
static constexpr int TOTAL_H    = PREV_Y + PREV_H + 14;

// ── Animation section ─────────────────────────────────────────
static constexpr int ANIM_SEP_Y  = TOTAL_H;
static constexpr int ANIM_LBL_Y  = ANIM_SEP_Y + 4;
static constexpr int ANIM_ROW_H  = 34;
static constexpr int ANIM_ROW1_Y = ANIM_LBL_Y + 20;
static constexpr int ANIM_ROW2_Y = ANIM_ROW1_Y + ANIM_ROW_H;
static constexpr int ANIM_ROW3_Y = ANIM_ROW2_Y + ANIM_ROW_H;
static constexpr int ANIM_ROW4_Y = ANIM_ROW3_Y + ANIM_ROW_H;
static constexpr int NEW_TOTAL_H = ANIM_ROW4_Y + ANIM_ROW_H + 12;

static const char* NOTE_NAMES[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static juce::String semiLabel(int st)
{
    static const char* names[] = {
        "Unison",
        "Minor 2nd",   "Major 2nd",
        "Minor 3rd",   "Major 3rd",
        "Perfect 4th", "Tritone",
        "Perfect 5th",
        "Minor 6th",   "Major 6th",
        "Minor 7th",   "Major 7th",
        "Octave"
    };
    const int a = std::abs(st);
    juce::String prefix = (st > 0) ? "+" : (st < 0 ? "-" : "");
    juce::String num    = (st != 0) ? (juce::String(a) + "  ") : "";
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

static void styleCombo(juce::ComboBox& cb)
{
    cb.setColour(juce::ComboBox::backgroundColourId, PANEL);
    cb.setColour(juce::ComboBox::textColourId,       TEXT_MAIN);
    cb.setColour(juce::ComboBox::outlineColourId,    SEP);
    cb.setColour(juce::ComboBox::arrowColourId,      ACCENT);
}

static void styleToggle(juce::ToggleButton& btn, juce::Colour col)
{
    btn.setColour(juce::ToggleButton::tickColourId,         col);
    btn.setColour(juce::ToggleButton::tickDisabledColourId, TEXT_DIM);
}

//==============================================================================
NoteAdderEditor::NoteAdderEditor(NoteAdderProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setSize(W, NEW_TOTAL_H);

    titleLabel.setText("NoteAdder", juce::dontSendNotification);
    titleLabel.setFont(juce::Font("Arial", 20.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addAndMakeVisible(titleLabel);

    randomizeBtn.setColour(juce::TextButton::buttonColourId,  PURPLE.withAlpha(0.3f));
    randomizeBtn.setColour(juce::TextButton::textColourOffId, PURPLE);
    randomizeBtn.onClick = [this] { doManualRandomize(); };
    addAndMakeVisible(randomizeBtn);

    for (auto* btn : { &manualBtn, &scaleBtn })
    {
        btn->setClickingTogglesState(false);
        addAndMakeVisible(btn);
    }
    manualBtn.onClick = [this] { *proc.modeParam = 0; updateModeUI(); };
    scaleBtn.onClick  = [this] { *proc.modeParam = 1; updateModeUI(); };

    for (auto* lbl : { &colSemiHdr, &colOctHdr })
    {
        lbl->setFont(juce::Font(11.0f, juce::Font::bold));
        lbl->setJustificationType(juce::Justification::centred);
        lbl->setColour(juce::Label::textColourId, TEXT_DIM);
        addAndMakeVisible(lbl);
    }
    colSemiHdr.setText("Semitones", juce::dontSendNotification);
    colOctHdr.setText("Octaves",    juce::dontSendNotification);

    buildManualRows();
    buildScaleSection();
    buildAnimSection();
    syncAllFromParams();
    updateModeUI();
    updateAnimUI();
}

NoteAdderEditor::~NoteAdderEditor() {}

//==============================================================================
void NoteAdderEditor::buildManualRows()
{
    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
    {
        auto& r = rows[i];
        styleToggle(r.enableBtn, ACCENT);
        r.enableBtn.setButtonText({});
        addAndMakeVisible(r.enableBtn);

        r.nameLabel.setText("Note " + juce::String(i + 1), juce::dontSendNotification);
        r.nameLabel.setFont(juce::Font(13.0f));
        r.nameLabel.setJustificationType(juce::Justification::centredRight);
        r.nameLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
        addAndMakeVisible(r.nameLabel);

        populateSemiCombo(r.semiCombo); styleCombo(r.semiCombo);
        populateOctCombo (r.octCombo);  styleCombo(r.octCombo);
        addAndMakeVisible(r.semiCombo);
        addAndMakeVisible(r.octCombo);

        r.enableBtn.onStateChange = [this, i]
        {
            *proc.enabledParams[i] = rows[i].enableBtn.getToggleState();
            updateRowEnabled(i);
        };
        r.semiCombo.onChange = [this, i]
        {
            *proc.semiParams[i] = idToSemi(rows[i].semiCombo.getSelectedId());
        };
        r.octCombo.onChange = [this, i]
        {
            *proc.octaveParams[i] = idToOct(rows[i].octCombo.getSelectedId());
        };
    }
}

//==============================================================================
void NoteAdderEditor::buildScaleSection()
{
    for (auto* lbl : { &rootLabel, &scaleLabel, &countLabel })
    {
        lbl->setFont(juce::Font(12.0f, juce::Font::bold));
        lbl->setJustificationType(juce::Justification::centredRight);
        lbl->setColour(juce::Label::textColourId, TEXT_DIM);
        addAndMakeVisible(lbl);
    }
    rootLabel.setText("Root",  juce::dontSendNotification);
    scaleLabel.setText("Scale", juce::dontSendNotification);
    countLabel.setText("Add",   juce::dontSendNotification);

    populateRootCombo (rootCombo);  styleCombo(rootCombo);
    populateScaleCombo(scaleCombo); styleCombo(scaleCombo);
    populateCountCombo(countCombo); styleCombo(countCombo);
    countCombo.setColour(juce::ComboBox::arrowColourId, GREEN);
    addAndMakeVisible(rootCombo);
    addAndMakeVisible(scaleCombo);
    addAndMakeVisible(countCombo);

    rootCombo.onChange  = [this] { *proc.rootNoteParam  = rootCombo.getSelectedId()  - 1; updateChordPreview(); };
    scaleCombo.onChange = [this] { *proc.scaleTypeParam = scaleCombo.getSelectedId() - 1; updateChordPreview(); };
    countCombo.onChange = [this]
    {
        *proc.noteCountParam = countCombo.getSelectedId() - 1;
        rebuildPickerCombo();
        updateChordPreview();
    };

    // ── Toggle row 1 ──────────────────────────────────────────
    styleToggle(discardBtn, RED);
    discardBtn.setButtonText({});
    discardLabel.setText("Discard out-of-scale", juce::dontSendNotification);
    discardLabel.setFont(juce::Font(12.0f));
    discardLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addAndMakeVisible(discardBtn);
    addAndMakeVisible(discardLabel);
    discardBtn.onStateChange = [this]
    {
        *proc.discardParam = discardBtn.getToggleState();
        discardLabel.setColour(juce::Label::textColourId,
            discardBtn.getToggleState() ? RED : TEXT_MAIN);
    };

    styleToggle(discardInputBtn, ORANGE);
    discardInputBtn.setButtonText({});
    discardInputLabel.setText("Discard input note", juce::dontSendNotification);
    discardInputLabel.setFont(juce::Font(12.0f));
    discardInputLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addAndMakeVisible(discardInputBtn);
    addAndMakeVisible(discardInputLabel);
    discardInputBtn.onStateChange = [this]
    {
        *proc.discardInputParam = discardInputBtn.getToggleState();
        discardInputLabel.setColour(juce::Label::textColourId,
            discardInputBtn.getToggleState() ? ORANGE : TEXT_MAIN);
    };

    // ── Toggle row 2 ──────────────────────────────────────────
    styleToggle(randomSkipBtn, YELLOW);
    randomSkipBtn.setButtonText({});
    randomSkipLabel.setText("Skip notes randomly", juce::dontSendNotification);
    randomSkipLabel.setFont(juce::Font(12.0f));
    randomSkipLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addAndMakeVisible(randomSkipBtn);
    addAndMakeVisible(randomSkipLabel);
    randomSkipBtn.onStateChange = [this]
    {
        *proc.randomSkipParam = randomSkipBtn.getToggleState();
        randomSkipLabel.setColour(juce::Label::textColourId,
            randomSkipBtn.getToggleState() ? YELLOW : TEXT_MAIN);
    };

    styleToggle(lockBassBtn, TEAL);
    lockBassBtn.setButtonText({});
    lockBassLabel.setText("Lock bass note", juce::dontSendNotification);
    lockBassLabel.setFont(juce::Font(12.0f));
    lockBassLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addAndMakeVisible(lockBassBtn);
    addAndMakeVisible(lockBassLabel);
    lockBassBtn.onStateChange = [this]
    {
        *proc.lockBassParam = lockBassBtn.getToggleState();
        lockBassLabel.setColour(juce::Label::textColourId,
            lockBassBtn.getToggleState() ? TEAL : TEXT_MAIN);
    };

    // ── Inversion row ─────────────────────────────────────────
    invLabel.setText("Inversion:", juce::dontSendNotification);
    invLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    invLabel.setJustificationType(juce::Justification::centredRight);
    invLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(invLabel);

    invModeCombo.addItem("Normal",           1);
    invModeCombo.addItem("Inversion Picker", 2);
    invModeCombo.addItem("Voice Leader",     3);
    invModeCombo.addItem("Drop 2",           4);
    invModeCombo.addItem("Cycle Up",         5);
    invModeCombo.addItem("Cycle Down",       6);
    invModeCombo.addItem("Random Cycle",     7);
    styleCombo(invModeCombo);
    invModeCombo.setColour(juce::ComboBox::arrowColourId, PURPLE);
    addAndMakeVisible(invModeCombo);
    invModeCombo.onChange = [this]
    {
        *proc.inversionModeParam = invModeCombo.getSelectedId() - 1;
        updateInversionUI();
    };

    styleCombo(invPickerCombo);
    invPickerCombo.setColour(juce::ComboBox::arrowColourId, PURPLE);
    addAndMakeVisible(invPickerCombo);
    invPickerCombo.onChange = [this]
    {
        int id = invPickerCombo.getSelectedId();
        if (id > 0) *proc.inversionPickerParam = id - 1;
    };

    invPickerLabel.setText("Pick:", juce::dontSendNotification);
    invPickerLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    invPickerLabel.setJustificationType(juce::Justification::centredRight);
    invPickerLabel.setColour(juce::Label::textColourId, PURPLE.withAlpha(0.8f));
    addAndMakeVisible(invPickerLabel);

    // ── Chord preview ─────────────────────────────────────────
    previewLabel.setFont(juce::Font("Arial", 12.5f, juce::Font::italic));
    previewLabel.setJustificationType(juce::Justification::centred);
    previewLabel.setColour(juce::Label::textColourId, ORANGE);
    addAndMakeVisible(previewLabel);
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

    populateAnimModeCombo(animModeCombo);
    styleCombo(animModeCombo);
    animModeCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addAndMakeVisible(animModeCombo);
    animModeCombo.onChange = [this]
    {
        *proc.animModeParam = animModeCombo.getSelectedId() - 1;
        updateAnimUI();
    };

    styleToggle(animSyncBtn, ANIM_COL);
    animSyncBtn.setButtonText({});
    addAndMakeVisible(animSyncBtn);
    animSyncBtn.onStateChange = [this]
    {
        *proc.animSyncBPMParam = animSyncBtn.getToggleState();
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
            *proc.animRateParam     = id - 1;
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
    wanderProbCombo.onChange = [this]
    {
        *proc.wanderProbParam = wanderProbCombo.getSelectedId() - 1;
    };

    wanderMaxLabel.setText("Spread:", juce::dontSendNotification);
    wanderMaxLabel.setFont(labelFont);
    wanderMaxLabel.setJustificationType(juce::Justification::centredRight);
    wanderMaxLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(wanderMaxLabel);

    populateWanderMaxCombo(wanderMaxCombo); styleCombo(wanderMaxCombo);
    wanderMaxCombo.setColour(juce::ComboBox::arrowColourId, GREEN);
    addAndMakeVisible(wanderMaxCombo);
    wanderMaxCombo.onChange = [this]
    {
        *proc.wanderMaxParam = wanderMaxCombo.getSelectedId() - 1;
    };

    // ── Row 3: Cloud params (density, spread, decay) ──────────
    cloudDensityLabel.setText("Density:", juce::dontSendNotification);
    cloudDensityLabel.setFont(labelFont);
    cloudDensityLabel.setJustificationType(juce::Justification::centredRight);
    cloudDensityLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudDensityLabel);

    populateCloudDensCombo(cloudDensityCombo); styleCombo(cloudDensityCombo);
    cloudDensityCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudDensityCombo);
    cloudDensityCombo.onChange = [this]
    {
        *proc.cloudDensityParam = cloudDensityCombo.getSelectedId() - 1;
    };

    cloudSpreadLabel.setText("Spread:", juce::dontSendNotification);
    cloudSpreadLabel.setFont(labelFont);
    cloudSpreadLabel.setJustificationType(juce::Justification::centredRight);
    cloudSpreadLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudSpreadLabel);

    populateCloudSpreadCombo(cloudSpreadCombo); styleCombo(cloudSpreadCombo);
    cloudSpreadCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudSpreadCombo);
    cloudSpreadCombo.onChange = [this]
    {
        *proc.cloudSpreadParam = cloudSpreadCombo.getSelectedId() - 1;
    };

    cloudDecayLabel.setText("Decay:", juce::dontSendNotification);
    cloudDecayLabel.setFont(labelFont);
    cloudDecayLabel.setJustificationType(juce::Justification::centredRight);
    cloudDecayLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudDecayLabel);

    populateCloudDecayCombo(cloudDecayCombo); styleCombo(cloudDecayCombo);
    cloudDecayCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudDecayCombo);
    cloudDecayCombo.onChange = [this]
    {
        *proc.cloudDecayParam = cloudDecayCombo.getSelectedId() - 1;
    };

    // ── Row 4: Cloud velocity range ───────────────────────────
    cloudVelLabel.setText("Velocity:", juce::dontSendNotification);
    cloudVelLabel.setFont(labelFont);
    cloudVelLabel.setJustificationType(juce::Justification::centredRight);
    cloudVelLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudVelLabel);

    cloudVelDashLabel.setText(juce::String::fromUTF8("\xe2\x80\x94"), juce::dontSendNotification);
    cloudVelDashLabel.setFont(juce::Font(14.0f));
    cloudVelDashLabel.setJustificationType(juce::Justification::centred);
    cloudVelDashLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addAndMakeVisible(cloudVelDashLabel);

    populateCloudVelCombo(cloudVelMinCombo); styleCombo(cloudVelMinCombo);
    cloudVelMinCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudVelMinCombo);
    cloudVelMinCombo.onChange = [this]
    {
        *proc.cloudVelMinParam = cloudVelMinCombo.getSelectedId() - 1;
    };

    populateCloudVelCombo(cloudVelMaxCombo); styleCombo(cloudVelMaxCombo);
    cloudVelMaxCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addAndMakeVisible(cloudVelMaxCombo);
    cloudVelMaxCombo.onChange = [this]
    {
        *proc.cloudVelMaxParam = cloudVelMaxCombo.getSelectedId() - 1;
    };
}

//==============================================================================
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
            "50 ms", "75 ms", "100 ms", "150 ms", "200 ms", "300 ms",
            "400 ms", "500 ms", "750 ms", "1000 ms", "1500 ms", "2000 ms"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_FREE_RATES; ++i)
            animRateCombo.addItem(labels[i], i + 1);
        animRateCombo.setSelectedId(proc.animRateFreeParam->get() + 1, juce::dontSendNotification);
    }
}

void NoteAdderEditor::updateAnimUI()
{
    const int  animMode = proc.animModeParam->get();
    const bool animOn   = (animMode != 0);

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

void NoteAdderEditor::syncAnimFromParams()
{
    animModeCombo.setSelectedId(proc.animModeParam->get() + 1, juce::dontSendNotification);
    animSyncBtn.setToggleState(proc.animSyncBPMParam->get(), juce::dontSendNotification);
    animSyncLabel.setColour(juce::Label::textColourId,
        proc.animSyncBPMParam->get() ? ANIM_COL : TEXT_MAIN);

    repopulateRateCombo();

    wanderProbCombo.setSelectedId(proc.wanderProbParam->get()    + 1, juce::dontSendNotification);
    wanderMaxCombo.setSelectedId (proc.wanderMaxParam->get()     + 1, juce::dontSendNotification);
    cloudDensityCombo.setSelectedId(proc.cloudDensityParam->get()+ 1, juce::dontSendNotification);
    cloudSpreadCombo.setSelectedId (proc.cloudSpreadParam->get() + 1, juce::dontSendNotification);
    cloudDecayCombo.setSelectedId  (proc.cloudDecayParam->get()  + 1, juce::dontSendNotification);
    cloudVelMinCombo.setSelectedId (proc.cloudVelMinParam->get() + 1, juce::dontSendNotification);
    cloudVelMaxCombo.setSelectedId (proc.cloudVelMaxParam->get() + 1, juce::dontSendNotification);
}

//==============================================================================
void NoteAdderEditor::rebuildPickerCombo()
{
    const int currentVal = proc.inversionPickerParam->get();
    const int noteCount  = proc.noteCountParam->get();

    invPickerCombo.clear(juce::dontSendNotification);
    for (int inv = 0; inv <= noteCount; ++inv)
        invPickerCombo.addItem(invPickerName(inv), inv + 1);

    int clamped = juce::jmin(currentVal, noteCount);
    *proc.inversionPickerParam = clamped;
    invPickerCombo.setSelectedId(clamped + 1, juce::dontSendNotification);
}

void NoteAdderEditor::updateInversionUI()
{
    const bool isScale  = (proc.modeParam->get() == 1);
    const bool isPicker = isScale && (proc.inversionModeParam->get() == 1);
    invPickerCombo.setVisible(isPicker);
    invPickerLabel.setVisible(isPicker);
}

//==============================================================================
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
    cb.addItem("Major",          1);
    cb.addItem("Minor",          2);
    cb.addItem("Penta Major",    3);
    cb.addItem("Penta Minor",    4);
    cb.addItem("Lydian",         5);
    cb.addItem("Mixolydian",     6);
    cb.addItem("Dorian",         7);
    cb.addItem("Harmonic Minor", 8);
    cb.addItem("Melodic Minor",  9);
}

void NoteAdderEditor::populateCountCombo(juce::ComboBox& cb)
{
    cb.addItem("0 notes", 1);
    for (int n = 1; n <= 7; ++n)
        cb.addItem(juce::String(n) + (n == 1 ? " note" : " notes"), n + 1);
}

void NoteAdderEditor::populateAnimModeCombo(juce::ComboBox& cb)
{
    cb.addItem("Off",      1);
    cb.addItem("Re-Voice", 2);
    cb.addItem("Wander",   3);
    cb.addItem("Drift",    4);
    cb.addItem("Cloud",    5);
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
    cb.addItem("Very Short  (~80ms)",  1);
    cb.addItem("Short       (~200ms)", 2);
    cb.addItem("Medium      (~400ms)", 3);
    cb.addItem("Long        (~700ms)", 4);
    cb.addItem("Very Long   (~1.4s)",  5);
}

void NoteAdderEditor::populateCloudVelCombo(juce::ComboBox& cb)
{
    static const char* names[] = {
        "ppp (8)", "pp (16)", "p (24)", "mp (32)", "mf (48)",
        "f (64)", "ff (80)", "fff (96)", "ffff (112)", "max (127)"
    };
    for (int i = 0; i < 10; ++i)
        cb.addItem(names[i], i + 1);
}

//==============================================================================
void NoteAdderEditor::syncAllFromParams()
{
    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        syncManualRow(i);

    rootCombo.setSelectedId (proc.rootNoteParam->get()  + 1, juce::dontSendNotification);
    scaleCombo.setSelectedId(proc.scaleTypeParam->get() + 1, juce::dontSendNotification);
    countCombo.setSelectedId(proc.noteCountParam->get() + 1, juce::dontSendNotification);

    discardBtn.setToggleState     (proc.discardParam->get(),      juce::dontSendNotification);
    discardInputBtn.setToggleState(proc.discardInputParam->get(), juce::dontSendNotification);
    randomSkipBtn.setToggleState  (proc.randomSkipParam->get(),   juce::dontSendNotification);
    lockBassBtn.setToggleState    (proc.lockBassParam->get(),     juce::dontSendNotification);

    discardLabel.setColour     (juce::Label::textColourId, proc.discardParam->get()      ? RED    : TEXT_MAIN);
    discardInputLabel.setColour(juce::Label::textColourId, proc.discardInputParam->get() ? ORANGE : TEXT_MAIN);
    randomSkipLabel.setColour  (juce::Label::textColourId, proc.randomSkipParam->get()   ? YELLOW : TEXT_MAIN);
    lockBassLabel.setColour    (juce::Label::textColourId, proc.lockBassParam->get()     ? TEAL   : TEXT_MAIN);

    invModeCombo.setSelectedId(proc.inversionModeParam->get() + 1, juce::dontSendNotification);
    rebuildPickerCombo();
    syncAnimFromParams();
}

void NoteAdderEditor::syncManualRow(int i)
{
    rows[i].enableBtn.setToggleState(proc.enabledParams[i]->get(),       juce::dontSendNotification);
    rows[i].semiCombo.setSelectedId (semiToId(proc.semiParams[i]->get()), juce::dontSendNotification);
    rows[i].octCombo.setSelectedId  (octToId(proc.octaveParams[i]->get()), juce::dontSendNotification);
}

//==============================================================================
void NoteAdderEditor::updateRowEnabled(int i)
{
    bool isManual = (proc.modeParam->get() == 0);
    bool rowOn    = isManual && rows[i].enableBtn.getToggleState();
    rows[i].enableBtn.setEnabled(isManual);
    rows[i].semiCombo.setEnabled(rowOn);
    rows[i].octCombo.setEnabled(rowOn);
    rows[i].nameLabel.setColour(juce::Label::textColourId, rowOn ? TEXT_MAIN : TEXT_DIM);
}

void NoteAdderEditor::updateModeUI()
{
    const bool isScale = (proc.modeParam->get() == 1);

    auto styleBtn = [](juce::TextButton& btn, bool active, juce::Colour col)
    {
        btn.setColour(juce::TextButton::buttonColourId,  active ? col : PANEL);
        btn.setColour(juce::TextButton::textColourOffId, active ? BG  : TEXT_DIM);
        btn.setColour(juce::TextButton::textColourOnId,  active ? BG  : TEXT_DIM);
    };
    styleBtn(manualBtn, !isScale, ACCENT);
    styleBtn(scaleBtn,   isScale, GREEN);

    randomizeBtn.setVisible(!isScale);

    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        updateRowEnabled(i);

    rootCombo.setEnabled(isScale);
    scaleCombo.setEnabled(isScale);
    countCombo.setEnabled(isScale);
    discardBtn.setEnabled(isScale);
    discardInputBtn.setEnabled(isScale);
    randomSkipBtn.setEnabled(isScale);
    lockBassBtn.setEnabled(isScale);
    invModeCombo.setEnabled(isScale);

    auto dimIfOff = [&](juce::Label& lbl, bool on, juce::Colour col)
    {
        lbl.setColour(juce::Label::textColourId,
            !isScale ? TEXT_DIM : (on ? col : TEXT_MAIN));
    };
    dimIfOff(discardLabel,      discardBtn.getToggleState(),      RED);
    dimIfOff(discardInputLabel, discardInputBtn.getToggleState(), ORANGE);
    dimIfOff(randomSkipLabel,   randomSkipBtn.getToggleState(),   YELLOW);
    dimIfOff(lockBassLabel,     lockBassBtn.getToggleState(),     TEAL);
    invLabel.setColour(juce::Label::textColourId, TEXT_DIM);

    updateInversionUI();
    updateChordPreview();
    repaint();
}

void NoteAdderEditor::updateChordPreview()
{
    if (proc.modeParam->get() != 1)
    {
        previewLabel.setText({}, juce::dontSendNotification);
        return;
    }

    const int  root  = proc.rootNoteParam->get();
    const int  type  = proc.scaleTypeParam->get();
    const int  count = proc.noteCountParam->get();
    const int  sz    = NoteAdderProcessor::SCALE_SIZES[type];
    const int* ivs   = NoteAdderProcessor::SCALE_INTERVALS[type];

    static const char* scaleNames[] = {
        "major", "minor", "penta major", "penta minor",
        "lydian", "mixolydian", "dorian", "harmonic minor", "melodic minor"
    };

    juce::String text = juce::String(NOTE_NAMES[root]) + " -> "
                      + juce::String(NOTE_NAMES[root]);

    for (int k = 1; k <= count; ++k)
    {
        int targetDeg  = 2 * k;
        int degInScale = targetDeg % sz;
        int noteClass  = (root + ivs[degInScale]) % 12;
        text += "  " + juce::String(NOTE_NAMES[noteClass]);
    }

    text += "   (" + juce::String(NOTE_NAMES[root])
          + " " + scaleNames[type]
          + ", " + juce::String(count + 1) + "-note chord)";

    previewLabel.setText(text, juce::dontSendNotification);
}

//==============================================================================
void NoteAdderEditor::doManualRandomize()
{
    juce::Random r;
    static const int pool[]   = { -12, -7, -5, -4, -3, 0, 3, 4, 5, 7, 8, 9, 12 };
    static const int poolSize = 13;

    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
    {
        bool enabled = (r.nextFloat() > 0.45f);
        int  semi    = pool[r.nextInt(poolSize)];
        int  oct     = r.nextInt(4) - 1;

        *proc.enabledParams[i] = enabled;
        *proc.semiParams[i]    = semi;
        *proc.octaveParams[i]  = oct;

        syncManualRow(i);
        updateRowEnabled(i);
    }
}

//==============================================================================
void NoteAdderEditor::paint(juce::Graphics& g)
{
    g.fillAll(BG);

    // Header gradient
    g.setGradientFill(juce::ColourGradient(
        PANEL.brighter(0.05f), 0.0f, 0.0f,
        BG, 0.0f, (float)HEADER_H, false));
    g.fillRect(0, 0, W, HEADER_H);

    g.setColour(ACCENT.withAlpha(0.4f));
    g.fillRect(10, HEADER_H - 2, W - 20, 1);

    // Manual rows striping
    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
    {
        g.setColour((i % 2 == 0 ? PANEL : PANEL_ALT).withAlpha(0.75f));
        g.fillRect(0, MANUAL_Y + i * ROW_H, W, ROW_H);
    }

    // Separator between manual and scale
    g.setColour(SEP);
    g.fillRect(10, SEP_Y + 2, W - 20, 1);

    // Scale section background
    g.setColour(PANEL.withAlpha(0.5f));
    g.fillRect(0, SCALELBL_Y, W, TOTAL_H - SCALELBL_Y);

    // Inversion row highlight
    g.setColour(PURPLE.withAlpha(0.07f));
    g.fillRect(0, INV_Y, W, INV_H);
    g.setColour(PURPLE.withAlpha(0.2f));
    g.fillRect(10, INV_Y,             W - 20, 1);
    g.fillRect(10, INV_Y + INV_H - 1, W - 20, 1);

    // Section text labels
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.setColour(proc.modeParam->get() == 0
        ? ACCENT.withAlpha(0.85f) : TEXT_DIM.withAlpha(0.5f));
    g.drawText("MANUAL MODE", 10, HEADER_H + 2, 140, 16,
        juce::Justification::centredLeft, false);

    g.setColour(proc.modeParam->get() == 1
        ? GREEN.withAlpha(0.85f) : TEXT_DIM.withAlpha(0.5f));
    g.drawText("SCALE MODE", 10, SCALELBL_Y, 140, 18,
        juce::Justification::centredLeft, false);

    // ── Animation section background & separator ──────────────
    g.setColour(SEP);
    g.fillRect(10, ANIM_SEP_Y + 2, W - 20, 1);

    g.setColour(ANIM_COL.withAlpha(0.04f));
    g.fillRect(0, ANIM_SEP_Y, W, NEW_TOTAL_H - ANIM_SEP_Y);

    g.setColour(ANIM_COL.withAlpha(0.25f));
    g.fillRect(10, ANIM_SEP_Y + 2, W - 20, 1);

    // Section label
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.setColour(proc.animModeParam->get() != 0
        ? ANIM_COL.withAlpha(0.9f) : TEXT_DIM.withAlpha(0.5f));
    g.drawText("ANIMATION MODE", 10, ANIM_LBL_Y, 160, 18,
        juce::Justification::centredLeft, false);

    // Hint text for modes with no extra params (ReVoice / Drift)
    const int curAnim = proc.animModeParam->get();
    if (curAnim == 1 || curAnim == 3)
    {
        static const char* hints[] = {
            "",
            "Cycles through all inversions of the chord on each tick.",
            "",
            "Chord root walks up the scale one degree per tick, drifting through diatonic chords.",
        };
        g.setFont(juce::Font("Arial", 11.5f, juce::Font::italic));
        g.setColour(ANIM_COL.withAlpha(0.55f));
        g.drawText(hints[curAnim], 12, ANIM_ROW3_Y + 6, W - 24, 22,
            juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void NoteAdderEditor::resized()
{
    const int margin  = 12;
    const int toggleW = 22;
    const int gap     = 6;
    const int labelW  = 56;
    const int comboH  = 26;
    const int semiW   = 160;
    const int octW    = 130;

    // ── Header ────────────────────────────────────────────────
    titleLabel.setBounds(margin, 10, 160, 28);

    const int btnH = 28, btnY = 12, btnW = 76, rndW = 52;
    scaleBtn.setBounds   (W - margin - btnW, btnY, btnW, btnH);
    manualBtn.setBounds  (W - margin - btnW * 2 - gap, btnY, btnW, btnH);
    randomizeBtn.setBounds(W - margin - btnW * 2 - gap - rndW - gap, btnY, rndW, btnH);

    // ── Column headers ────────────────────────────────────────
    const int semiX = margin + toggleW + gap + labelW + gap;
    const int octX  = semiX + semiW + gap;
    colSemiHdr.setBounds(semiX, HEADER_H + 2, semiW, 16);
    colOctHdr.setBounds (octX,  HEADER_H + 2, octW,  16);

    // ── Manual rows ───────────────────────────────────────────
    for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
    {
        const int rowY = MANUAL_Y + i * ROW_H;
        const int cy   = rowY + (ROW_H - comboH) / 2;
        rows[i].enableBtn.setBounds(margin, rowY + (ROW_H - 22) / 2, toggleW, 22);
        rows[i].nameLabel.setBounds(margin + toggleW + gap, cy, labelW, comboH);
        rows[i].semiCombo.setBounds(semiX, cy, semiW, comboH);
        rows[i].octCombo.setBounds (octX,  cy, octW,  comboH);
    }

    // ── Scale controls ────────────────────────────────────────
    {
        const int cy     = SCALE_Y + (SCALE_H - comboH) / 2;
        const int rLblW  = 32, rCmbW = 58;
        const int sLblW  = 40, sCmbW = 140;
        const int cLblW  = 28, cCmbW = 82;
        int x = margin;
        rootLabel.setBounds (x, cy, rLblW, comboH); x += rLblW + 4;
        rootCombo.setBounds (x, cy, rCmbW, comboH); x += rCmbW + 10;
        scaleLabel.setBounds(x, cy, sLblW, comboH); x += sLblW + 4;
        scaleCombo.setBounds(x, cy, sCmbW, comboH); x += sCmbW + 10;
        countLabel.setBounds(x, cy, cLblW, comboH); x += cLblW + 4;
        countCombo.setBounds(x, cy, cCmbW, comboH);
    }

    // ── Toggle row 1 ──────────────────────────────────────────
    {
        const int ty = TOGGLE1_Y + (TOGGLE_H - 22) / 2;
        int x = margin;
        discardBtn.setBounds   (x, ty, toggleW, 22); x += toggleW + 3;
        discardLabel.setBounds (x, ty, 160,     22); x += 160 + 20;
        discardInputBtn.setBounds  (x, ty, toggleW, 22); x += toggleW + 3;
        discardInputLabel.setBounds(x, ty, 160,     22);
    }

    // ── Toggle row 2 ──────────────────────────────────────────
    {
        const int ty = TOGGLE2_Y + (TOGGLE_H - 22) / 2;
        int x = margin;
        randomSkipBtn.setBounds  (x, ty, toggleW, 22); x += toggleW + 3;
        randomSkipLabel.setBounds(x, ty, 160,     22); x += 160 + 20;
        lockBassBtn.setBounds    (x, ty, toggleW, 22); x += toggleW + 3;
        lockBassLabel.setBounds  (x, ty, 160,     22);
    }

    // ── Inversion row ─────────────────────────────────────────
    {
        const int cy         = INV_Y + (INV_H - comboH) / 2;
        const int invLblW    = 70;
        const int modeCmbW   = 150;
        const int pickLblW   = 40;
        const int pickCmbW   = 140;
        int x = margin;
        invLabel.setBounds    (x, cy, invLblW,  comboH); x += invLblW + 4;
        invModeCombo.setBounds(x, cy, modeCmbW, comboH); x += modeCmbW + 14;
        invPickerLabel.setBounds(x, cy, pickLblW, comboH); x += pickLblW + 4;
        invPickerCombo.setBounds(x, cy, pickCmbW, comboH);
    }

    // ── Chord preview ─────────────────────────────────────────
    previewLabel.setBounds(margin, PREV_Y, W - margin * 2, PREV_H);

    // ── Animation row 1: mode + BPM sync ─────────────────────
    {
        const int cy       = ANIM_ROW1_Y + (ANIM_ROW_H - comboH) / 2;
        const int lblW     = 62;
        const int modeCmbW = 130;
        const int syncTogW = 22;
        const int syncLblW = 80;
        int x = margin;
        animModeLabel.setBounds(x, cy, lblW,     comboH); x += lblW + 4;
        animModeCombo.setBounds(x, cy, modeCmbW, comboH); x += modeCmbW + 20;
        animSyncBtn.setBounds  (x, cy, syncTogW, 22);     x += syncTogW + 4;
        animSyncLabel.setBounds(x, cy, syncLblW, comboH);
    }

    // ── Animation row 2: rate ─────────────────────────────────
    {
        const int cy      = ANIM_ROW2_Y + (ANIM_ROW_H - comboH) / 2;
        const int rateLblW = 38;
        const int rateCmbW = 120;
        int x = margin;
        animRateLabel.setBounds(x, cy, rateLblW, comboH); x += rateLblW + 4;
        animRateCombo.setBounds(x, cy, rateCmbW, comboH);
    }

    // ── Animation row 3: Wander params OR Cloud params ────────
    {
        const int cy = ANIM_ROW3_Y + (ANIM_ROW_H - comboH) / 2;

        // Wander
        {
            const int pLblW = 38, pCmbW = 80;
            const int mLblW = 52, mCmbW = 80;
            int x = margin;
            wanderProbLabel.setBounds(x, cy, pLblW, comboH); x += pLblW + 4;
            wanderProbCombo.setBounds(x, cy, pCmbW, comboH); x += pCmbW + 16;
            wanderMaxLabel.setBounds (x, cy, mLblW, comboH); x += mLblW + 4;
            wanderMaxCombo.setBounds (x, cy, mCmbW, comboH);
        }

        // Cloud row 3 (density, spread, decay)
        {
            const int dLblW = 52, dCmbW = 80;
            const int sLblW = 52, sCmbW = 74;
            const int dcLblW = 44, dcCmbW = 148;
            int x = margin;
            cloudDensityLabel.setBounds(x, cy, dLblW,  comboH); x += dLblW  + 4;
            cloudDensityCombo.setBounds(x, cy, dCmbW,  comboH); x += dCmbW  + 10;
            cloudSpreadLabel.setBounds (x, cy, sLblW,  comboH); x += sLblW  + 4;
            cloudSpreadCombo.setBounds (x, cy, sCmbW,  comboH); x += sCmbW  + 10;
            cloudDecayLabel.setBounds  (x, cy, dcLblW, comboH); x += dcLblW + 4;
            cloudDecayCombo.setBounds  (x, cy, dcCmbW, comboH);
        }
    }

    // ── Animation row 4: Cloud velocity range ─────────────────
    {
        const int cy      = ANIM_ROW4_Y + (ANIM_ROW_H - comboH) / 2;
        const int vLblW   = 58;
        const int vCmbW   = 110;
        const int dashW   = 18;
        int x = margin;
        cloudVelLabel.setBounds   (x, cy, vLblW, comboH); x += vLblW + 4;
        cloudVelMinCombo.setBounds(x, cy, vCmbW, comboH); x += vCmbW + 4;
        cloudVelDashLabel.setBounds(x, cy, dashW, comboH); x += dashW + 4;
        cloudVelMaxCombo.setBounds(x, cy, vCmbW, comboH);
    }
}
