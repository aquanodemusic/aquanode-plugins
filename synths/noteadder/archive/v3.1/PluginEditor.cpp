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
// PIANO_W is the piano-roll strip on the left; all existing content is
// shifted right by this amount.  W / CONTENT_W is the original content width.
static constexpr int PIANO_W = 50;
static constexpr int W = 510;   // content area width (unchanged)
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
    : AudioProcessorEditor(&p), proc(p), pianoRoll(p)
{
    setSize(W + PIANO_W, TOTAL_H);

    addAndMakeVisible(pianoRoll);

    // ── Audit button ──────────────────────────────────────────
    auditBtn.setClickingTogglesState(true);
    auditBtn.setColour(juce::TextButton::buttonColourId, ORANGE.withAlpha(0.15f));
    auditBtn.setColour(juce::TextButton::buttonOnColourId, ORANGE.withAlpha(0.85f));
    auditBtn.setColour(juce::TextButton::textColourOffId, ORANGE);
    auditBtn.setColour(juce::TextButton::textColourOnId, BG);
    auditBtn.onClick = [this]
        {
            proc.auditEnabled.store(auditBtn.getToggleState(),
                std::memory_order_relaxed);
        };
    addAndMakeVisible(auditBtn);

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
    buildScaleAnimSection();
    buildManualAnimSection();

    // ── APVTS attachments ─────────────────────────────────────
    createAttachments();

    // ── Initial combo state ───────────────────────────────────
    repopulateScaleRateCombo();
    repopulateManualRateCombo();
    rebuildPickerCombo();
    updateModeUI();
    updateInversionUI();
    updateChordPreview();
}

NoteAdderEditor::~NoteAdderEditor() {}

//==============================================================================

//==============================================================================
// ── PianoRollDisplay ─────────────────────────────────────────────────────────

void PianoRollDisplay::paint(juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();

    g.fillAll(juce::Colour(0xff141520));

    static constexpr int NOTE_LO = 36;  // C2
    static constexpr int NOTE_HI = 96;  // C7
    // 5 octaves x 7 white keys + 1 (C7) = 36 white keys total
    const float wH = (float)H / 36.0f;
    const float bH = wH * 0.62f;
    const float bW = (float)W * 0.60f;

    static const float wUnits[12] = {
        0.0f, 0.5f, 1.0f, 1.5f, 2.0f,
        3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 6.0f
    };
    static const bool isBlack[12] = {
        false, true, false, true, false,
        false, true, false, true, false, true, false
    };

    // Colours are fetched live from APVTS params so user edits apply immediately
    auto noteColor = [&](int pc) { return proc.getNoteColour(pc % 12); };

    const uint64_t lo = proc.pianoRollNotesLow.load(std::memory_order_relaxed);
    const uint64_t hi = proc.pianoRollNotesHigh.load(std::memory_order_relaxed);

    auto isNoteOn = [&](int note) -> bool
        {
            if (note < 0 || note > 127) return false;
            return note < 64 ? ((lo >> note) & 1ULL) != 0
                : ((hi >> (note - 64)) & 1ULL) != 0;
        };

    const bool scaleMode = (proc.modeParam->get() == 1);
    const int  scaleRoot = proc.rootNoteParam->get();
    const int  scaleType = proc.scaleTypeParam->get();

    auto isInScale = [&](int note) -> bool
        {
            const int sz = NoteAdderProcessor::SCALE_SIZES[scaleType];
            const int* iv = NoteAdderProcessor::SCALE_INTERVALS[scaleType];
            int pc = ((note % 12) - scaleRoot + 12) % 12;
            for (int i = 0; i < sz; ++i)
                if (iv[i] == pc) return true;
            return false;
        };

    auto getRect = [&](int note) -> juce::Rectangle<float>
        {
            if (note < NOTE_LO || note > NOTE_HI) return {};
            const int rel = note - NOTE_LO;
            const int oct = rel / 12;
            const int semi = rel % 12;
            const float wu = (float)(oct * 7) + wUnits[semi];

            if (!isBlack[semi])
                return { 0.0f, (float)H - (wu + 1.0f) * wH, (float)W, wH };

            float cy = (float)H - (wu + 0.5f) * wH;
            return { 0.0f, cy - bH * 0.5f, bW, bH };
        };

    // White keys
    for (int note = NOTE_LO; note <= NOTE_HI; ++note)
    {
        const int semi = (note - NOTE_LO) % 12;
        if (isBlack[semi]) continue;

        const auto rect = getRect(note);
        const bool active = isNoteOn(note);
        const bool colored = active && (!scaleMode || isInScale(note));

        g.setColour(colored ? noteColor(note).brighter(0.18f)
            : juce::Colour(0xffd8d8e0));
        g.fillRect(rect);
        g.setColour(juce::Colour(0xff252535));
        g.drawRect(rect, 0.7f);
    }

    // Black keys (drawn on top)
    for (int note = NOTE_LO; note <= NOTE_HI; ++note)
    {
        const int semi = (note - NOTE_LO) % 12;
        if (!isBlack[semi]) continue;

        const auto rect = getRect(note);
        const bool active = isNoteOn(note);
        const bool colored = active && (!scaleMode || isInScale(note));

        g.setColour(colored ? noteColor(note) : juce::Colour(0xff151622));
        g.fillRect(rect);
        g.setColour(juce::Colour(colored ? 0x40ffffff : 0x25ffffff));
        g.drawRect(rect, 0.6f);
    }

    // Octave labels
    g.setFont(juce::Font(8.5f, juce::Font::bold));
    for (int oct = 2; oct <= 7; ++oct)
    {
        const int cNote = 36 + (oct - 2) * 12;
        const auto rect = getRect(cNote);
        g.setColour(juce::Colour(0xff55556a));
        g.drawText("C" + juce::String(oct),
            (int)rect.getX() + 2, (int)rect.getY() + 1,
            (int)rect.getWidth() - 3, (int)rect.getHeight() - 2,
            juce::Justification::centredLeft, false);
    }

    // ── Hover highlight ───────────────────────────────────────
    {
        juce::Point<int> mp = getMouseXYRelative();
        const int hoverNote = noteAtPosition(mp.x, mp.y);
        if (hoverNote >= 0)
        {
            // Reuse the already-defined getRect lambda
            const auto hRect = getRect(hoverNote);
            g.setColour(juce::Colours::white.withAlpha(0.14f));
            g.fillRect(hRect);
        }
    }

    // Right-edge separator
    g.setColour(juce::Colour(0xff414868));
    g.fillRect((float)(W - 1), 0.0f, 1.0f, (float)H);
}


//==============================================================================
// ── PianoRollDisplay – mouse interaction ─────────────────────────────────────

int PianoRollDisplay::noteAtPosition(int px, int py) const noexcept
{
    const int H = getHeight();
    const int W = getWidth();

    static constexpr int NOTE_LO = 36;  // C2
    static constexpr int NOTE_HI = 96;  // C7

    const float wH = (float)H / 36.0f;
    const float bH = wH * 0.62f;
    const float bW = (float)W * 0.60f;

    // White-unit distance from bottom (C2 = 0)
    const float wuFromBottom = (float)(H - py) / wH;
    if (wuFromBottom < 0.0f || wuFromBottom >= 36.0f) return -1;

    const int   totalOctave = juce::jlimit(0, 4, (int)(wuFromBottom / 7.0f));
    const float wuInOct = wuFromBottom - totalOctave * 7.0f;

    // --- Black keys (checked first – they sit on top) ---
    if ((float)px < bW)
    {
        // Black key center positions (in white-unit within octave) and semitones
        static const float bPos[5] = { 0.5f, 1.5f, 3.5f, 4.5f, 5.5f };
        static const int   bSemi[5] = { 1,    3,    6,    8,    10 };
        const float halfBH = (bH * 0.5f) / wH;

        for (int i = 0; i < 5; ++i)
        {
            if (std::abs(wuInOct - bPos[i]) <= halfBH)
            {
                int note = NOTE_LO + totalOctave * 12 + bSemi[i];
                return (note >= NOTE_LO && note <= NOTE_HI) ? note : -1;
            }
        }
    }

    // --- White keys ---
    // wu 0-6 within octave → semitone 0,2,4,5,7,9,11
    static const int wSemi[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int wuInt = juce::jlimit(0, 6, (int)wuInOct);
    int note = NOTE_LO + totalOctave * 12 + wSemi[wuInt];
    return (note >= NOTE_LO && note <= NOTE_HI) ? note : -1;
}

void PianoRollDisplay::guiNoteOn(int note) noexcept
{
    if (note < 0 || note > 127) return;
    if (note < 64)
        proc.guiNotesLow.fetch_or(uint64_t(1) << note, std::memory_order_release);
    else
        proc.guiNotesHigh.fetch_or(uint64_t(1) << (note - 64), std::memory_order_release);
}

void PianoRollDisplay::guiNoteOff(int note) noexcept
{
    if (note < 0 || note > 127) return;
    if (note < 64)
        proc.guiNotesLow.fetch_and(~(uint64_t(1) << note), std::memory_order_release);
    else
        proc.guiNotesHigh.fetch_and(~(uint64_t(1) << (note - 64)), std::memory_order_release);
}

void PianoRollDisplay::releaseAllGuiNotes() noexcept
{
    proc.guiNotesLow.store(0, std::memory_order_release);
    proc.guiNotesHigh.store(0, std::memory_order_release);
}

void PianoRollDisplay::mouseDown(const juce::MouseEvent& e)
{
    const int note = noteAtPosition(e.x, e.y);
    if (note < 0) return;

    if (currentDragNote >= 0 && currentDragNote != note)
        guiNoteOff(currentDragNote);

    currentDragNote = note;
    guiNoteOn(note);
}

void PianoRollDisplay::mouseDrag(const juce::MouseEvent& e)
{
    const int note = noteAtPosition(e.x, e.y);
    if (note == currentDragNote) return;

    if (currentDragNote >= 0)
        guiNoteOff(currentDragNote);

    currentDragNote = note;
    if (note >= 0)
        guiNoteOn(note);
}

void PianoRollDisplay::mouseUp(const juce::MouseEvent&)
{
    if (currentDragNote >= 0)
    {
        guiNoteOff(currentDragNote);
        currentDragNote = -1;
    }
}

void PianoRollDisplay::mouseExit(const juce::MouseEvent&)
{
    if (currentDragNote >= 0)
    {
        guiNoteOff(currentDragNote);
        currentDragNote = -1;
    }
}

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

    // ── Reset all manual rows button ──────────────────────────
    resetManualBtn.setColour(juce::TextButton::buttonColourId, RED.withAlpha(0.2f));
    resetManualBtn.setColour(juce::TextButton::textColourOffId, RED);
    resetManualBtn.onClick = [this]
        {
            for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
            {
                *proc.enabledParams[i] = false;
                *proc.semiParams[i] = 0;
                *proc.octaveParams[i] = 0;
                rows[i].enableBtn.setToggleState(false, juce::sendNotification);
                rows[i].semiCombo.setSelectedId(semiToId(0), juce::sendNotification);
                rows[i].octCombo.setSelectedId(octToId(0), juce::sendNotification);
            }
        };
    addChildComponent(resetManualBtn);
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

    // ── Reset note colours button ──────────────────────────────
    resetColorsBtn.setColour(juce::TextButton::buttonColourId, RED.withAlpha(0.2f));
    resetColorsBtn.setColour(juce::TextButton::textColourOffId, RED);
    resetColorsBtn.onClick = [this]
        {
            static const int NOTE_COLOR_DEFAULTS[12] = {
                16725815, 16746255, 15127040,  7921940,  2019920,
                   51375,    42495,  1660415,  6890495, 11796735,
                15073435, 16711755
            };
            for (int i = 0; i < 12; ++i)
                *proc.noteColorParams[i] = NOTE_COLOR_DEFAULTS[i];
            repaint();
        };
    addChildComponent(resetColorsBtn);

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
void NoteAdderEditor::buildScaleAnimSection()
{
    const juce::Font labelFont(12.0f, juce::Font::bold);
    const juce::Font smallFont(12.0f);

    // ── Row 1: mode ───────────────────────────────────────────
    scaleAnimModeLabel.setText("Animate:", juce::dontSendNotification);
    scaleAnimModeLabel.setFont(labelFont);
    scaleAnimModeLabel.setJustificationType(juce::Justification::centredRight);
    scaleAnimModeLabel.setColour(juce::Label::textColourId, ANIM_COL.withAlpha(0.85f));
    addChildComponent(scaleAnimModeLabel);

    scaleAnimModeCombo.addItem("Off", 1);   // param 0
    scaleAnimModeCombo.addItem("Re-Voice", 2);   // param 1
    scaleAnimModeCombo.addItem("Wander", 3);   // param 2
    scaleAnimModeCombo.addItem("Drift", 4);   // param 3
    scaleAnimModeCombo.addItem("Cloud", 5);   // param 4
    styleCombo(scaleAnimModeCombo);
    scaleAnimModeCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addChildComponent(scaleAnimModeCombo);
    scaleAnimModeCombo.onChange = [this]
        {
            int id = scaleAnimModeCombo.getSelectedId();
            if (id > 0) *proc.animModeParam = id - 1;
            updateScaleAnimUI();
        };

    styleToggle(scaleAnimSyncBtn, ANIM_COL);
    scaleAnimSyncBtn.setButtonText({});
    addChildComponent(scaleAnimSyncBtn);
    scaleAnimSyncBtn.onStateChange = [this]
        {
            repopulateScaleRateCombo();
            scaleAnimSyncLabel.setColour(juce::Label::textColourId,
                scaleAnimSyncBtn.getToggleState() ? ANIM_COL : TEXT_MAIN);
        };

    scaleAnimSyncLabel.setText("BPM sync", juce::dontSendNotification);
    scaleAnimSyncLabel.setFont(smallFont);
    scaleAnimSyncLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(scaleAnimSyncLabel);

    // ── Row 2: rate ───────────────────────────────────────────
    scaleAnimRateLabel.setText("Rate:", juce::dontSendNotification);
    scaleAnimRateLabel.setFont(labelFont);
    scaleAnimRateLabel.setJustificationType(juce::Justification::centredRight);
    scaleAnimRateLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(scaleAnimRateLabel);

    styleCombo(scaleAnimRateCombo);
    scaleAnimRateCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addChildComponent(scaleAnimRateCombo);
    scaleAnimRateCombo.onChange = [this]
        {
            int id = scaleAnimRateCombo.getSelectedId();
            if (id <= 0) return;
            if (proc.animSyncBPMParam->get())
                *proc.animRateParam = id - 1;
            else
                *proc.animRateFreeParam = id - 1;
        };

    // ── Row 3: Wander params ──────────────────────────────────
    scaleWanderProbLabel.setText("Prob:", juce::dontSendNotification);
    scaleWanderProbLabel.setFont(labelFont);
    scaleWanderProbLabel.setJustificationType(juce::Justification::centredRight);
    scaleWanderProbLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(scaleWanderProbLabel);

    populateWanderProbCombo(scaleWanderProbCombo); styleCombo(scaleWanderProbCombo);
    scaleWanderProbCombo.setColour(juce::ComboBox::arrowColourId, GREEN);
    addChildComponent(scaleWanderProbCombo);

    scaleWanderMaxLabel.setText("Spread:", juce::dontSendNotification);
    scaleWanderMaxLabel.setFont(labelFont);
    scaleWanderMaxLabel.setJustificationType(juce::Justification::centredRight);
    scaleWanderMaxLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(scaleWanderMaxLabel);

    populateWanderMaxCombo(scaleWanderMaxCombo); styleCombo(scaleWanderMaxCombo);
    scaleWanderMaxCombo.setColour(juce::ComboBox::arrowColourId, GREEN);
    addChildComponent(scaleWanderMaxCombo);

    // ── Row 3 (alt): Cloud params ─────────────────────────────
    scaleCloudDensityLabel.setText("Density:", juce::dontSendNotification);
    scaleCloudDensityLabel.setFont(labelFont);
    scaleCloudDensityLabel.setJustificationType(juce::Justification::centredRight);
    scaleCloudDensityLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(scaleCloudDensityLabel);

    populateCloudDensCombo(scaleCloudDensityCombo); styleCombo(scaleCloudDensityCombo);
    scaleCloudDensityCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(scaleCloudDensityCombo);

    scaleCloudSpreadLabel.setText("Spread:", juce::dontSendNotification);
    scaleCloudSpreadLabel.setFont(labelFont);
    scaleCloudSpreadLabel.setJustificationType(juce::Justification::centredRight);
    scaleCloudSpreadLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(scaleCloudSpreadLabel);

    populateCloudSpreadCombo(scaleCloudSpreadCombo); styleCombo(scaleCloudSpreadCombo);
    scaleCloudSpreadCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(scaleCloudSpreadCombo);

    scaleCloudDecayLabel.setText("Decay:", juce::dontSendNotification);
    scaleCloudDecayLabel.setFont(labelFont);
    scaleCloudDecayLabel.setJustificationType(juce::Justification::centredRight);
    scaleCloudDecayLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(scaleCloudDecayLabel);

    populateCloudDecayCombo(scaleCloudDecayCombo); styleCombo(scaleCloudDecayCombo);
    scaleCloudDecayCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(scaleCloudDecayCombo);

    // ── Row 4: Cloud velocity ─────────────────────────────────
    scaleCloudVelLabel.setText("Velocity:", juce::dontSendNotification);
    scaleCloudVelLabel.setFont(labelFont);
    scaleCloudVelLabel.setJustificationType(juce::Justification::centredRight);
    scaleCloudVelLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(scaleCloudVelLabel);

    scaleCloudVelDashLabel.setText("to", juce::dontSendNotification);
    scaleCloudVelDashLabel.setFont(juce::Font(12.0f));
    scaleCloudVelDashLabel.setJustificationType(juce::Justification::centred);
    scaleCloudVelDashLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(scaleCloudVelDashLabel);

    populateCloudVelCombo(scaleCloudVelMinCombo); styleCombo(scaleCloudVelMinCombo);
    scaleCloudVelMinCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(scaleCloudVelMinCombo);

    populateCloudVelCombo(scaleCloudVelMaxCombo); styleCombo(scaleCloudVelMaxCombo);
    scaleCloudVelMaxCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(scaleCloudVelMaxCombo);
}

//==============================================================================
void NoteAdderEditor::buildManualAnimSection()
{
    const juce::Font labelFont(12.0f, juce::Font::bold);
    const juce::Font smallFont(12.0f);

    // ── Row 1: mode ───────────────────────────────────────────
    manAnimModeLabel.setText("Animate:", juce::dontSendNotification);
    manAnimModeLabel.setFont(labelFont);
    manAnimModeLabel.setJustificationType(juce::Justification::centredRight);
    manAnimModeLabel.setColour(juce::Label::textColourId, ANIM_COL.withAlpha(0.85f));
    addChildComponent(manAnimModeLabel);

    manAnimModeCombo.addItem("Off", 1);   // param 0
    manAnimModeCombo.addItem("Re-Voice", 2);   // param 1
    manAnimModeCombo.addItem("Cloud", 3);   // param 2
    styleCombo(manAnimModeCombo);
    manAnimModeCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addChildComponent(manAnimModeCombo);
    manAnimModeCombo.onChange = [this]
        {
            int id = manAnimModeCombo.getSelectedId();
            if (id > 0) *proc.manualAnimModeParam = id - 1;
            updateManualAnimUI();
        };

    styleToggle(manAnimSyncBtn, ANIM_COL);
    manAnimSyncBtn.setButtonText({});
    addChildComponent(manAnimSyncBtn);
    manAnimSyncBtn.onStateChange = [this]
        {
            repopulateManualRateCombo();
            manAnimSyncLabel.setColour(juce::Label::textColourId,
                manAnimSyncBtn.getToggleState() ? ANIM_COL : TEXT_MAIN);
        };

    manAnimSyncLabel.setText("BPM sync", juce::dontSendNotification);
    manAnimSyncLabel.setFont(smallFont);
    manAnimSyncLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(manAnimSyncLabel);

    // ── Row 2: rate ───────────────────────────────────────────
    manAnimRateLabel.setText("Rate:", juce::dontSendNotification);
    manAnimRateLabel.setFont(labelFont);
    manAnimRateLabel.setJustificationType(juce::Justification::centredRight);
    manAnimRateLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(manAnimRateLabel);

    styleCombo(manAnimRateCombo);
    manAnimRateCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addChildComponent(manAnimRateCombo);
    manAnimRateCombo.onChange = [this]
        {
            int id = manAnimRateCombo.getSelectedId();
            if (id <= 0) return;
            if (proc.manualAnimSyncBPMParam->get())
                *proc.manualAnimRateParam = id - 1;
            else
                *proc.manualAnimRateFreeParam = id - 1;
        };

    // ── Row 3: Cloud params ───────────────────────────────────
    manCloudDensityLabel.setText("Density:", juce::dontSendNotification);
    manCloudDensityLabel.setFont(labelFont);
    manCloudDensityLabel.setJustificationType(juce::Justification::centredRight);
    manCloudDensityLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(manCloudDensityLabel);

    populateCloudDensCombo(manCloudDensityCombo); styleCombo(manCloudDensityCombo);
    manCloudDensityCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(manCloudDensityCombo);

    manCloudOctaveLabel.setText("Oct range:", juce::dontSendNotification);
    manCloudOctaveLabel.setFont(labelFont);
    manCloudOctaveLabel.setJustificationType(juce::Justification::centredRight);
    manCloudOctaveLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(manCloudOctaveLabel);

    populateCloudOctaveCombo(manCloudOctaveCombo); styleCombo(manCloudOctaveCombo);
    manCloudOctaveCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(manCloudOctaveCombo);

    manCloudDecayLabel.setText("Decay:", juce::dontSendNotification);
    manCloudDecayLabel.setFont(labelFont);
    manCloudDecayLabel.setJustificationType(juce::Justification::centredRight);
    manCloudDecayLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(manCloudDecayLabel);

    populateCloudDecayCombo(manCloudDecayCombo); styleCombo(manCloudDecayCombo);
    manCloudDecayCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(manCloudDecayCombo);

    // ── Row 4: Cloud velocity ─────────────────────────────────
    manCloudVelLabel.setText("Velocity:", juce::dontSendNotification);
    manCloudVelLabel.setFont(labelFont);
    manCloudVelLabel.setJustificationType(juce::Justification::centredRight);
    manCloudVelLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(manCloudVelLabel);

    manCloudVelDashLabel.setText("to", juce::dontSendNotification);
    manCloudVelDashLabel.setFont(juce::Font(12.0f));
    manCloudVelDashLabel.setJustificationType(juce::Justification::centred);
    manCloudVelDashLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(manCloudVelDashLabel);

    populateCloudVelCombo(manCloudVelMinCombo); styleCombo(manCloudVelMinCombo);
    manCloudVelMinCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(manCloudVelMinCombo);

    populateCloudVelCombo(manCloudVelMaxCombo); styleCombo(manCloudVelMaxCombo);
    manCloudVelMaxCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(manCloudVelMaxCombo);
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

    // Inversion mode combo
    invModeComboAtt = std::make_unique<CBA>(vts, "invmode", invModeCombo);

    // ── Scale animation ───────────────────────────────────────
    // animModeCombo is manually managed (onChange writes param directly)
    scaleAnimSyncAtt = std::make_unique<BA>(vts, "animsync", scaleAnimSyncBtn);
    scaleWanderProbAtt = std::make_unique<CBA>(vts, "wanderprob", scaleWanderProbCombo);
    scaleWanderMaxAtt = std::make_unique<CBA>(vts, "wandermax", scaleWanderMaxCombo);
    scaleCloudDensAtt = std::make_unique<CBA>(vts, "clouddensity", scaleCloudDensityCombo);
    scaleCloudSpreadAtt = std::make_unique<CBA>(vts, "cloudspread", scaleCloudSpreadCombo);
    scaleCloudDecayAtt = std::make_unique<CBA>(vts, "clouddecay", scaleCloudDecayCombo);
    scaleCloudVelMinAtt = std::make_unique<CBA>(vts, "cloudvelmin", scaleCloudVelMinCombo);
    scaleCloudVelMaxAtt = std::make_unique<CBA>(vts, "cloudvelmax", scaleCloudVelMaxCombo);

    // ── Manual animation ──────────────────────────────────────
    // manAnimModeCombo is manually managed (onChange writes param directly)
    manAnimSyncAtt = std::make_unique<BA>(vts, "manimsync", manAnimSyncBtn);
    manCloudDensAtt   = std::make_unique<CBA>(vts, "mclouddensity", manCloudDensityCombo);
    manCloudOctaveAtt = std::make_unique<CBA>(vts, "mcloudoctave",  manCloudOctaveCombo);
    manCloudDecayAtt = std::make_unique<CBA>(vts, "mclouddecay", manCloudDecayCombo);
    manCloudVelMinAtt = std::make_unique<CBA>(vts, "mcloudvelmin", manCloudVelMinCombo);
    manCloudVelMaxAtt = std::make_unique<CBA>(vts, "mcloudvelmax", manCloudVelMaxCombo);

    // ── Manual rows ───────────────────────────────────────────
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

//==============================================================================
// ── Rate combo populators ─────────────────────────────────────────────────────

void NoteAdderEditor::repopulateScaleRateCombo()
{
    scaleAnimRateCombo.clear(juce::dontSendNotification);

    if (proc.animSyncBPMParam->get())
    {
        static const char* labels[NoteAdderProcessor::NUM_SUBDIVS] = {
            "1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_SUBDIVS; ++i)
            scaleAnimRateCombo.addItem(labels[i], i + 1);
        scaleAnimRateCombo.setSelectedId(proc.animRateParam->get() + 1,
            juce::dontSendNotification);
    }
    else
    {
        static const char* labels[NoteAdderProcessor::NUM_FREE_RATES] = {
            "50 ms","75 ms","100 ms","150 ms","200 ms","300 ms",
            "400 ms","500 ms","750 ms","1000 ms","1500 ms","2000 ms"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_FREE_RATES; ++i)
            scaleAnimRateCombo.addItem(labels[i], i + 1);
        scaleAnimRateCombo.setSelectedId(proc.animRateFreeParam->get() + 1,
            juce::dontSendNotification);
    }
}

void NoteAdderEditor::repopulateManualRateCombo()
{
    manAnimRateCombo.clear(juce::dontSendNotification);

    if (proc.manualAnimSyncBPMParam->get())
    {
        static const char* labels[NoteAdderProcessor::NUM_SUBDIVS] = {
            "1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_SUBDIVS; ++i)
            manAnimRateCombo.addItem(labels[i], i + 1);
        manAnimRateCombo.setSelectedId(proc.manualAnimRateParam->get() + 1,
            juce::dontSendNotification);
    }
    else
    {
        static const char* labels[NoteAdderProcessor::NUM_FREE_RATES] = {
            "50 ms","75 ms","100 ms","150 ms","200 ms","300 ms",
            "400 ms","500 ms","750 ms","1000 ms","1500 ms","2000 ms"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_FREE_RATES; ++i)
            manAnimRateCombo.addItem(labels[i], i + 1);
        manAnimRateCombo.setSelectedId(proc.manualAnimRateFreeParam->get() + 1,
            juce::dontSendNotification);
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
    savePresetBtn.setVisible(true);
    loadPresetBtn.setVisible(true);
    resetManualBtn.setVisible(!isScale);

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
    resetColorsBtn.setVisible(isScale);
    updateInversionUI();

    // ── Animation sections: show the one that matches the active mode ──
    // Scale anim section – all top-level widgets always shown when isScale,
    // sub-widget visibility is handled by updateScaleAnimUI().
    scaleAnimModeLabel.setVisible(isScale);
    scaleAnimModeCombo.setVisible(isScale);
    scaleAnimSyncBtn.setVisible(isScale);
    scaleAnimSyncLabel.setVisible(isScale);
    scaleAnimRateLabel.setVisible(isScale);
    scaleAnimRateCombo.setVisible(isScale);

    // Manual anim section
    manAnimModeLabel.setVisible(!isScale);
    manAnimModeCombo.setVisible(!isScale);
    manAnimSyncBtn.setVisible(!isScale);
    manAnimSyncLabel.setVisible(!isScale);
    manAnimRateLabel.setVisible(!isScale);
    manAnimRateCombo.setVisible(!isScale);

    // ── Hide ALL sub-widgets of the section going inactive ───────────────────
    // updateAnimUI() only manages sub-widgets for the *active* section, so
    // without this the other section's cloud/wander rows remain visible after
    // a mode switch and overlap the newly shown content.
    if (isScale)
    {
        // Switching TO scale: force all manual cloud sub-widgets off
        manCloudDensityLabel.setVisible(false);
        manCloudDensityCombo.setVisible(false);
        manCloudOctaveLabel.setVisible(false);
        manCloudOctaveCombo.setVisible(false);
        manCloudDecayLabel.setVisible(false);
        manCloudDecayCombo.setVisible(false);
        manCloudVelLabel.setVisible(false);
        manCloudVelDashLabel.setVisible(false);
        manCloudVelMinCombo.setVisible(false);
        manCloudVelMaxCombo.setVisible(false);
    }
    else
    {
        // Switching TO manual: force all scale wander & cloud sub-widgets off
        scaleWanderProbLabel.setVisible(false);
        scaleWanderProbCombo.setVisible(false);
        scaleWanderMaxLabel.setVisible(false);
        scaleWanderMaxCombo.setVisible(false);
        scaleCloudDensityLabel.setVisible(false);
        scaleCloudDensityCombo.setVisible(false);
        scaleCloudSpreadLabel.setVisible(false);
        scaleCloudSpreadCombo.setVisible(false);
        scaleCloudDecayLabel.setVisible(false);
        scaleCloudDecayCombo.setVisible(false);
        scaleCloudVelLabel.setVisible(false);
        scaleCloudVelDashLabel.setVisible(false);
        scaleCloudVelMinCombo.setVisible(false);
        scaleCloudVelMaxCombo.setVisible(false);
    }

    // Sub-widgets of the active section are managed by updateAnimUI below
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
    if (proc.modeParam->get() == 1) updateScaleAnimUI();
    else                            updateManualAnimUI();
}

void NoteAdderEditor::updateScaleAnimUI()
{
    const int  animMode = proc.animModeParam->get();
    const bool animOn = (animMode != 0);

    // Sync mode combo from param (in case param was changed externally)
    scaleAnimModeCombo.setSelectedId(animMode + 1, juce::dontSendNotification);

    scaleAnimRateLabel.setEnabled(animOn);
    scaleAnimRateCombo.setEnabled(animOn);
    scaleAnimSyncBtn.setEnabled(animOn);
    scaleAnimSyncLabel.setEnabled(animOn);

    const bool isWander = animOn && (animMode == 2);
    scaleWanderProbLabel.setVisible(isWander);
    scaleWanderProbCombo.setVisible(isWander);
    scaleWanderMaxLabel.setVisible(isWander);
    scaleWanderMaxCombo.setVisible(isWander);

    const bool isCloud = animOn && (animMode == 4);
    scaleCloudDensityLabel.setVisible(isCloud);
    scaleCloudDensityCombo.setVisible(isCloud);
    scaleCloudSpreadLabel.setVisible(isCloud);
    scaleCloudSpreadCombo.setVisible(isCloud);
    scaleCloudDecayLabel.setVisible(isCloud);
    scaleCloudDecayCombo.setVisible(isCloud);
    scaleCloudVelLabel.setVisible(isCloud);
    scaleCloudVelDashLabel.setVisible(isCloud);
    scaleCloudVelMinCombo.setVisible(isCloud);
    scaleCloudVelMaxCombo.setVisible(isCloud);

    repaint();
}

void NoteAdderEditor::updateManualAnimUI()
{
    const int  animMode = proc.manualAnimModeParam->get();
    const bool animOn = (animMode != 0);

    // Sync mode combo from param (in case param was changed externally)
    manAnimModeCombo.setSelectedId(animMode + 1, juce::dontSendNotification);

    manAnimRateLabel.setEnabled(animOn);
    manAnimRateCombo.setEnabled(animOn);
    manAnimSyncBtn.setEnabled(animOn);
    manAnimSyncLabel.setEnabled(animOn);

    const bool isCloud = animOn && (animMode == 2);
    manCloudDensityLabel.setVisible(isCloud);
    manCloudDensityCombo.setVisible(isCloud);
    manCloudOctaveLabel.setVisible(isCloud);
    manCloudOctaveCombo.setVisible(isCloud);
    manCloudDecayLabel.setVisible(isCloud);
    manCloudDecayCombo.setVisible(isCloud);
    manCloudVelLabel.setVisible(isCloud);
    manCloudVelDashLabel.setVisible(isCloud);
    manCloudVelMinCombo.setVisible(isCloud);
    manCloudVelMaxCombo.setVisible(isCloud);

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
    repaint();   // refresh the scale colour key
}

//==============================================================================
// ── Preset save / load ────────────────────────────────────────────────────────

void NoteAdderEditor::saveManualPreset()
{
    fileChooser = std::make_shared<juce::FileChooser>(
        "Save Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.nastate");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            file = file.withFileExtension("nastate");

            juce::XmlElement xml("NoteAdderPreset");

            // ── Manual rows ──────────────────────────────────────
            auto* manualEl = xml.createNewChildElement("Manual");
            for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
            {
                auto* row = manualEl->createNewChildElement("Row");
                row->setAttribute("enabled", proc.enabledParams[i]->get() ? 1 : 0);
                row->setAttribute("semi", proc.semiParams[i]->get());
                row->setAttribute("oct", proc.octaveParams[i]->get());
            }

            // ── Scale settings ───────────────────────────────────
            auto* scaleEl = xml.createNewChildElement("Scale");
            scaleEl->setAttribute("root", proc.rootNoteParam->get());
            scaleEl->setAttribute("scale", proc.scaleTypeParam->get());
            scaleEl->setAttribute("count", proc.noteCountParam->get());
            scaleEl->setAttribute("harmonicMode", proc.harmonicModeParam->get());
            scaleEl->setAttribute("discard", proc.discardParam->get() ? 1 : 0);
            scaleEl->setAttribute("discardInput", proc.discardInputParam->get() ? 1 : 0);
            scaleEl->setAttribute("randomSkip", proc.randomSkipParam->get() ? 1 : 0);
            scaleEl->setAttribute("lockBass", proc.lockBassParam->get() ? 1 : 0);
            scaleEl->setAttribute("invMode", proc.inversionModeParam->get());
            scaleEl->setAttribute("invPicker", proc.inversionPickerParam->get());

            // ── Animation settings – Scale mode ──────────────────
            auto* animScaleEl = xml.createNewChildElement("AnimationScale");
            animScaleEl->setAttribute("mode", proc.animModeParam->get());
            animScaleEl->setAttribute("syncBPM", proc.animSyncBPMParam->get() ? 1 : 0);
            animScaleEl->setAttribute("rate", proc.animRateParam->get());
            animScaleEl->setAttribute("rateFree", proc.animRateFreeParam->get());
            animScaleEl->setAttribute("wanderProb", proc.wanderProbParam->get());
            animScaleEl->setAttribute("wanderMax", proc.wanderMaxParam->get());
            animScaleEl->setAttribute("cloudDensity", proc.cloudDensityParam->get());
            animScaleEl->setAttribute("cloudSpread", proc.cloudSpreadParam->get());
            animScaleEl->setAttribute("cloudVelMin", proc.cloudVelMinParam->get());
            animScaleEl->setAttribute("cloudVelMax", proc.cloudVelMaxParam->get());
            animScaleEl->setAttribute("cloudDecay", proc.cloudDecayParam->get());

            // ── Animation settings – Manual mode ─────────────────
            auto* animManualEl = xml.createNewChildElement("AnimationManual");
            animManualEl->setAttribute("mode", proc.manualAnimModeParam->get());
            animManualEl->setAttribute("syncBPM", proc.manualAnimSyncBPMParam->get() ? 1 : 0);
            animManualEl->setAttribute("rate", proc.manualAnimRateParam->get());
            animManualEl->setAttribute("rateFree", proc.manualAnimRateFreeParam->get());
            // (No wander params – Wander/Drift are scale-only)
            animManualEl->setAttribute("cloudDensity", proc.manualCloudDensityParam->get());
            animManualEl->setAttribute("cloudOctave",  proc.manualCloudOctaveParam->get());
            animManualEl->setAttribute("cloudVelMin",  proc.manualCloudVelMinParam->get());
            animManualEl->setAttribute("cloudVelMax",  proc.manualCloudVelMaxParam->get());
            animManualEl->setAttribute("cloudDecay",   proc.manualCloudDecayParam->get());

            // ── Per-pitch-class note colours (0xRRGGBB packed) ──
            auto* colorsEl = xml.createNewChildElement("NoteColors");
            for (int i = 0; i < 12; ++i)
                colorsEl->setAttribute("pc" + juce::String(i),
                    proc.noteColorParams[i]->get());

            xml.writeTo(file);
        });
}

void NoteAdderEditor::loadManualPreset()
{
    fileChooser = std::make_shared<juce::FileChooser>(
        "Load Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.nastate");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;

            auto xml = juce::XmlDocument::parse(file);
            if (!xml) return;

            // ── Manual rows ──────────────────────────────────────
            // Support both old format (Row children directly on root) and
            // new format (Row children inside a <Manual> element).
            auto* manualEl = xml->getChildByName("Manual");
            auto* rowParent = manualEl ? manualEl : xml.get();

            int rowIdx = 0;
            for (auto* row : rowParent->getChildWithTagNameIterator("Row"))
            {
                if (rowIdx >= NoteAdderProcessor::NUM_ROWS) break;
                *proc.enabledParams[rowIdx] = row->getIntAttribute("enabled", 0) != 0;
                *proc.semiParams[rowIdx] = row->getIntAttribute("semi", 0);
                *proc.octaveParams[rowIdx] = row->getIntAttribute("oct", 0);

                // Sync widgets (APVTS attachments will also pick up param changes,
                // but manual sync ensures the combo reflects the value immediately)
                rows[rowIdx].enableBtn.setToggleState(
                    proc.enabledParams[rowIdx]->get(), juce::sendNotification);
                rows[rowIdx].semiCombo.setSelectedId(
                    semiToId(proc.semiParams[rowIdx]->get()), juce::sendNotification);
                rows[rowIdx].octCombo.setSelectedId(
                    octToId(proc.octaveParams[rowIdx]->get()), juce::sendNotification);
                ++rowIdx;
            }

            // ── Scale settings ───────────────────────────────────
            if (auto* scaleEl = xml->getChildByName("Scale"))
            {
                *proc.rootNoteParam = scaleEl->getIntAttribute("root", 0);
                *proc.scaleTypeParam = scaleEl->getIntAttribute("scale", 0);
                *proc.noteCountParam = scaleEl->getIntAttribute("count", 3);
                *proc.harmonicModeParam = scaleEl->getIntAttribute("harmonicMode", 1);
                *proc.discardParam = scaleEl->getIntAttribute("discard", 0) != 0;
                *proc.discardInputParam = scaleEl->getIntAttribute("discardInput", 0) != 0;
                *proc.randomSkipParam = scaleEl->getIntAttribute("randomSkip", 0) != 0;
                *proc.lockBassParam = scaleEl->getIntAttribute("lockBass", 0) != 0;
                *proc.inversionModeParam = scaleEl->getIntAttribute("invMode", 0);
                *proc.inversionPickerParam = scaleEl->getIntAttribute("invPicker", 0);

                // APVTS ComboBoxAttachments listen to parameter changes and will
                // update the combos, but set explicitly to be safe.
                rootCombo.setSelectedId(proc.rootNoteParam->get() + 1, juce::sendNotification);
                scaleCombo.setSelectedId(proc.scaleTypeParam->get() + 1, juce::sendNotification);
                countCombo.setSelectedId(proc.noteCountParam->get() + 1, juce::sendNotification);
                harmonicModeCombo.setSelectedId(proc.harmonicModeParam->get() + 1, juce::sendNotification);
                invModeCombo.setSelectedId(proc.inversionModeParam->get() + 1, juce::sendNotification);

                discardBtn.setToggleState(proc.discardParam->get(), juce::sendNotification);
                discardInputBtn.setToggleState(proc.discardInputParam->get(), juce::sendNotification);
                randomSkipBtn.setToggleState(proc.randomSkipParam->get(), juce::sendNotification);
                lockBassBtn.setToggleState(proc.lockBassParam->get(), juce::sendNotification);
            }

            // ── Animation settings ───────────────────────────────
            // Support old single-section format ("Animation") and new split format.
            auto loadAnimInto = [&](juce::XmlElement* el,
                juce::AudioParameterInt* modeP,
                juce::AudioParameterBool* syncP,
                juce::AudioParameterInt* rateP,
                juce::AudioParameterInt* rateFreeP,
                juce::AudioParameterInt* wanderProbP,   // nullptr for manual
                juce::AudioParameterInt* wanderMaxP,    // nullptr for manual
                juce::AudioParameterInt* densP,
                juce::AudioParameterInt* spreadP,       // nullptr for manual (handled separately)
                juce::AudioParameterInt* velMinP,
                juce::AudioParameterInt* velMaxP,
                juce::AudioParameterInt* decayP)
                {
                    if (!el) return;
                    *modeP = el->getIntAttribute("mode", 0);
                    *syncP = el->getIntAttribute("syncBPM", 1) != 0;
                    *rateP = el->getIntAttribute("rate", 3);
                    *rateFreeP = el->getIntAttribute("rateFree", 4);
                    if (wanderProbP) *wanderProbP = el->getIntAttribute("wanderProb", 4);
                    if (wanderMaxP)  *wanderMaxP  = el->getIntAttribute("wanderMax",  2);
                    *densP = el->getIntAttribute("cloudDensity", 1);
                    if (spreadP) *spreadP = el->getIntAttribute("cloudSpread", 3);
                    *velMinP = el->getIntAttribute("cloudVelMin", 1);
                    *velMaxP = el->getIntAttribute("cloudVelMax", 4);
                    *decayP  = el->getIntAttribute("cloudDecay",  1);
                };

            // New split format
            loadAnimInto(xml->getChildByName("AnimationScale"),
                proc.animModeParam, proc.animSyncBPMParam,
                proc.animRateParam, proc.animRateFreeParam,
                proc.wanderProbParam, proc.wanderMaxParam,
                proc.cloudDensityParam, proc.cloudSpreadParam,
                proc.cloudVelMinParam, proc.cloudVelMaxParam, proc.cloudDecayParam);

            loadAnimInto(xml->getChildByName("AnimationManual"),
                proc.manualAnimModeParam, proc.manualAnimSyncBPMParam,
                proc.manualAnimRateParam, proc.manualAnimRateFreeParam,
                nullptr, nullptr,    // no wander in manual mode
                proc.manualCloudDensityParam, nullptr,  // octave handled below
                proc.manualCloudVelMinParam, proc.manualCloudVelMaxParam,
                proc.manualCloudDecayParam);

            // Load the manual cloud octave range (separate attr, 0-3 range)
            if (auto* manAnimEl = xml->getChildByName("AnimationManual"))
                *proc.manualCloudOctaveParam = manAnimEl->getIntAttribute("cloudOctave", 1);

            // Legacy single-section format: load into the scale anim params only
            if (!xml->getChildByName("AnimationScale") && !xml->getChildByName("AnimationManual"))
            {
                loadAnimInto(xml->getChildByName("Animation"),
                    proc.animModeParam, proc.animSyncBPMParam,
                    proc.animRateParam, proc.animRateFreeParam,
                    proc.wanderProbParam, proc.wanderMaxParam,
                    proc.cloudDensityParam, proc.cloudSpreadParam,
                    proc.cloudVelMinParam, proc.cloudVelMaxParam, proc.cloudDecayParam);
            }

            // ── Per-pitch-class note colours ─────────────────────
            if (auto* colorsEl = xml->getChildByName("NoteColors"))
            {
                for (int i = 0; i < 12; ++i)
                    *proc.noteColorParams[i] = colorsEl->getIntAttribute(
                        "pc" + juce::String(i), proc.noteColorParams[i]->get());
            }

            // Rebuild dynamic combos and refresh all UI state.
            // updateModeUI shows the correct anim section and calls updateAnimUI.
            repopulateScaleRateCombo();
            repopulateManualRateCombo();
            updateModeUI();
            rebuildPickerCombo();
            updateInversionUI();
            updateChordPreview();
            repaint();
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
    for (int i = 1; i <= 14; ++i)
        cb.addItem(juce::String(i) + " deg", i);
}

void NoteAdderEditor::populateCloudOctaveCombo(juce::ComboBox& cb)
{
    cb.addItem("0 oct (same)",  1);  // param 0
    cb.addItem("+/- 1 oct",  2);  // param 1
    cb.addItem("+/- 2 oct",  3);  // param 2
    cb.addItem("+/- 3 oct",  4);  // param 3
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
// ── Note colour picker popup ──────────────────────────────────────────────────
// Shown in a CallOutBox when the user clicks a chip in the scale colour key.

struct NoteColourPopup : public juce::Component,
    public juce::ChangeListener
{
    NoteAdderProcessor& proc;
    const int           pitchClass;
    juce::ColourSelector selector;
    NoteAdderEditor& editor;

    NoteColourPopup(NoteAdderProcessor& p, int pc, NoteAdderEditor& ed)
        : proc(p), pitchClass(pc), editor(ed),
        selector(juce::ColourSelector::showColourAtTop
            | juce::ColourSelector::showSliders
            | juce::ColourSelector::showColourspace, 4, 0)
    {
        setSize(280, 260);
        addAndMakeVisible(selector);
        selector.setCurrentColour(proc.getNoteColour(pc), juce::dontSendNotification);
        selector.addChangeListener(this);
    }

    void resized() override { selector.setBounds(getLocalBounds()); }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        const auto c = selector.getCurrentColour().withAlpha(1.0f);
        const int packed = (c.getRed() << 16) | (c.getGreen() << 8) | c.getBlue();
        *proc.noteColorParams[pitchClass] = packed;
        editor.repaint();
    }
};

//------------------------------------------------------------------------------
// Returns which chip (0-11) the point falls on, or -1.
int NoteAdderEditor::chipIndexAtPoint(int x, int y) const noexcept
{
    static constexpr int REGION_TOP = SCALE_PREV_Y + 28;
    static constexpr int REGION_BOTTOM = ANIM_Y - 4;
    static constexpr int REGION_X = PIANO_W + 12;
    static constexpr int REGION_W = W - 24;
    static constexpr int N = 12;
    const float chipW = (REGION_W - (N - 1) * 2) / (float)N;

    if (y < REGION_TOP || y >= REGION_BOTTOM) return -1;
    if (x < REGION_X || x >= REGION_X + REGION_W) return -1;

    const int pc = (int)((x - REGION_X) / (chipW + 2.0f));
    return juce::jlimit(0, 11, pc);
}

void NoteAdderEditor::openNoteColourPicker(int pc)
{
    auto popup = std::make_unique<NoteColourPopup>(proc, pc, *this);
    // compute chip screen bounds to anchor the callout
    static constexpr int REGION_TOP = SCALE_PREV_Y + 28;
    static constexpr int REGION_X = PIANO_W + 12;
    static constexpr int REGION_W = W - 24;
    static constexpr int REGION_H = ANIM_Y - 4 - REGION_TOP;
    const float chipW = (REGION_W - 11 * 2) / 12.0f;
    const int   chipX = REGION_X + (int)(pc * (chipW + 2.0f));
    juce::Rectangle<int> anchor(chipX, REGION_TOP, (int)chipW, REGION_H);

    juce::CallOutBox::launchAsynchronously(std::move(popup),
        localAreaToGlobal(anchor), nullptr);
}

void NoteAdderEditor::mouseDown(const juce::MouseEvent& e)
{
    if (proc.modeParam->get() != 1) return;
    const int pc = chipIndexAtPoint(e.x, e.y);
    if (pc >= 0)
        openNoteColourPicker(pc);
}

void NoteAdderEditor::mouseMove(const juce::MouseEvent& e)
{
    // Repaint so the hover highlight on chips updates live.
    if (proc.modeParam->get() == 1 && chipIndexAtPoint(e.x, e.y) >= 0)
        repaint();
}

juce::MouseCursor NoteAdderEditor::getMouseCursor()
{
    auto pos = getMouseXYRelative();
    if (proc.modeParam->get() == 1 && chipIndexAtPoint(pos.x, pos.y) >= 0)
        return juce::MouseCursor::PointingHandCursor;
    return juce::MouseCursor::NormalCursor;
}

//==============================================================================
// ── Scale colour key ──────────────────────────────────────────────────────────
// Drawn in the free space in the scale tab between the chord-preview label and
// the animation section.  Each of the 12 pitch classes gets a colour chip;
// in-scale notes are fully coloured, out-of-scale notes are almost-black with
// a dim border.  The selected root note carries a small filled dot.

void NoteAdderEditor::drawScaleColorKey(juce::Graphics& g) const
{
    // ── layout ────────────────────────────────────────────────
    // Free region: y = [SCALE_PREV_Y + 28,  ANIM_Y - 4]
    static constexpr int REGION_TOP = SCALE_PREV_Y + 28;
    static constexpr int REGION_BOTTOM = ANIM_Y - 4;
    static constexpr int REGION_H = REGION_BOTTOM - REGION_TOP;   // ~46 px
    static constexpr int REGION_X = PIANO_W + 12;
    static constexpr int REGION_W = W - 24;

    // 12 chips side by side with 2px gaps
    static constexpr int N = 12;
    const float chipW = (REGION_W - (N - 1) * 2) / (float)N;
    const float chipH = (float)REGION_H;

    // Colours fetched live from APVTS so edits apply immediately everywhere
    auto noteColor = [&](int pc) { return proc.getNoteColour(pc); };

    static const char* NOTE_NAMES_SHARP[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };

    // ── current scale state ───────────────────────────────────
    const int  root = proc.rootNoteParam->get();
    const int  scaleType = proc.scaleTypeParam->get();
    const int  sz = NoteAdderProcessor::SCALE_SIZES[scaleType];
    const int* ivs = NoteAdderProcessor::SCALE_INTERVALS[scaleType];

    auto isInScale = [&](int pc) -> bool
        {
            int rel = (pc - root + 12) % 12;
            for (int i = 0; i < sz; ++i)
                if (ivs[i] == rel) return true;
            return false;
        };

    // ── section label ─────────────────────────────────────────
    g.setFont(juce::Font(9.5f, juce::Font::bold));
    g.setColour(TEXT_DIM.withAlpha(0.55f));
    g.drawText("SCALE COLOURS",
        REGION_X, REGION_TOP - 14, 120, 13,
        juce::Justification::centredLeft, false);

    // ── draw each chip ────────────────────────────────────────
    const float cornerR = 4.0f;

    for (int pc = 0; pc < 12; ++pc)
    {
        const float cx = (float)REGION_X + pc * (chipW + 2.0f);
        const float cy = (float)REGION_TOP;
        juce::Rectangle<float> chip(cx, cy, chipW, chipH);

        const bool inScale = isInScale(pc);
        const bool isRoot = (pc == root);

        if (inScale)
        {
            // Gradient: bright top, slightly darker bottom
            g.setGradientFill(juce::ColourGradient(
                noteColor(pc).brighter(0.15f), cx, cy,
                noteColor(pc).darker(0.20f), cx, cy + chipH, false));
            g.fillRoundedRectangle(chip, cornerR);

            // Thin bright border
            g.setColour(noteColor(pc).brighter(0.5f).withAlpha(0.55f));
            g.drawRoundedRectangle(chip, cornerR, 0.8f);
        }
        else
        {
            // Out-of-scale: near-black with a very faint tinted border
            g.setColour(juce::Colour(0xff181825));
            g.fillRoundedRectangle(chip, cornerR);
            g.setColour(noteColor(pc).withAlpha(0.18f));
            g.drawRoundedRectangle(chip, cornerR, 0.8f);
        }

        // ── note name ─────────────────────────────────────────
        const bool isSharp = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
        const float nameAreaH = chipH * 0.52f;
        const float nameY = cy + (chipH - nameAreaH) * 0.42f;

        if (inScale)
        {
            {
                // Single-line name for both natural and sharp notes
                const float fs = isSharp ? 8.5f : 11.0f;
                g.setFont(juce::Font(fs, juce::Font::bold));
                g.setColour(juce::Colours::white.withAlpha(0.92f));
                g.drawText(NOTE_NAMES_SHARP[pc],
                    (int)(cx - 2), (int)nameY, (int)chipW + 4, (int)nameAreaH,
                    juce::Justification::centred, false);
            }
        }
        else
        {
            // Dim, small note name
            {
                const float fs = isSharp ? 7.5f : 9.5f;
                g.setFont(juce::Font(fs, juce::Font::bold));
                g.setColour(noteColor(pc).withAlpha(0.38f));
                g.drawText(NOTE_NAMES_SHARP[pc],
                    (int)(cx - 2), (int)nameY, (int)chipW + 4, (int)nameAreaH,
                    juce::Justification::centred, false);
            }
        }

        // ── root indicator: small filled dot at bottom ─────────
        if (isRoot)
        {
            const float dotR = 3.2f;
            const float dotCx = cx + chipW * 0.5f;
            const float dotCy = cy + chipH - dotR - 3.5f;
            g.setColour(inScale ? juce::Colours::white.withAlpha(0.90f)
                : noteColor(pc).withAlpha(0.60f));
            g.fillEllipse(dotCx - dotR, dotCy - dotR, dotR * 2.0f, dotR * 2.0f);
        }

        // ── hover highlight + pencil hint ─────────────────────
        {
            auto mp = getMouseXYRelative();
            if (chip.contains((float)mp.x, (float)mp.y))
            {
                g.setColour(juce::Colours::white.withAlpha(0.15f));
                g.fillRoundedRectangle(chip, cornerR);
                // Small "edit" pencil indicator top-right
                g.setFont(juce::Font(9.0f));
                g.setColour(juce::Colours::white.withAlpha(0.65f));
                g.drawText("*", (int)(cx + chipW - 11), (int)(cy + 2), 10, 10,
                    juce::Justification::centred, false);
            }
        }
    }
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
    g.fillRect(PIANO_W, 0, W, HEADER_H);

    // Subtle amber glow behind the AUD button when active
    if (proc.auditEnabled.load(std::memory_order_relaxed))
    {
        auto ab = auditBtn.getBounds().expanded(6, 4);
        g.setGradientFill(juce::ColourGradient(
            ORANGE.withAlpha(0.22f), (float)ab.getCentreX(), (float)ab.getY(),
            ORANGE.withAlpha(0.0f), (float)ab.getCentreX(), (float)ab.getBottom(),
            false));
        g.fillEllipse(ab.toFloat());
    }

    g.setColour(SEP);
    g.fillRect(PIANO_W + 10, HEADER_H - 1, W - 20, 1);

    // ── Tab pane background ───────────────────────────────────
    const bool isScale = (proc.modeParam->get() == 1);

    g.setColour(isScale ? GREEN.withAlpha(0.03f) : ACCENT.withAlpha(0.03f));
    g.fillRect(PIANO_W, TAB_Y, W, TAB_H);

    // Subtle tab active indicator on the top edge
    g.setColour(isScale ? GREEN.withAlpha(0.35f) : ACCENT.withAlpha(0.35f));
    g.fillRect(PIANO_W, TAB_Y, W, 2);

    if (!isScale)
    {
        // Manual: stripe the rows
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        {
            g.setColour((i % 2 == 0 ? PANEL : PANEL_ALT).withAlpha(0.75f));
            g.fillRect(PIANO_W, MANUAL_ROWS_Y + i * MANUAL_ROW_H, W, MANUAL_ROW_H);
        }
    }
    else
    {
        // Scale: subtle inversion row highlight
        g.setColour(PURPLE.withAlpha(0.06f));
        g.fillRect(PIANO_W, SCALE_INV_Y, W, 38);
        g.setColour(PURPLE.withAlpha(0.18f));
        g.fillRect(PIANO_W + 10, SCALE_INV_Y, W - 20, 1);
        g.fillRect(PIANO_W + 10, SCALE_INV_Y + 37, W - 20, 1);

        // Scale colour key in the free space below the chord preview
        drawScaleColorKey(g);
    }

    // ── Separator before anim section ─────────────────────────
    g.setColour(ANIM_COL.withAlpha(0.22f));
    g.fillRect(PIANO_W + 10, ANIM_Y + 1, W - 20, 1);

    g.setColour(ANIM_COL.withAlpha(0.04f));
    g.fillRect(PIANO_W, ANIM_Y, W, TOTAL_H - ANIM_Y);

    // ── Anim section label ────────────────────────────────────
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    {
        const bool isScale = (proc.modeParam->get() == 1);
        const int  curAnim = isScale ? proc.animModeParam->get()
            : proc.manualAnimModeParam->get();
        g.setColour(curAnim != 0 ? ANIM_COL.withAlpha(0.9f) : TEXT_DIM.withAlpha(0.5f));
        g.drawText("ANIMATION", PIANO_W + 10, ANIM_Y + 4, 120, 16,
            juce::Justification::centredLeft, false);

        // Hint text for modes that have no param sub-widgets on row 3
        if (isScale && (curAnim == 1 || curAnim == 3))
        {
            static const char* hints[] = {
                "",
                "Cycles through all inversions of the chord on each tick.",
                "",
                "Chord root walks up the scale one degree per tick.",
            };
            g.setFont(juce::Font("Arial", 11.0f, juce::Font::italic));
            g.setColour(ANIM_COL.withAlpha(0.5f));
            g.drawText(hints[curAnim], PIANO_W + 12, ANIM_ROW3_Y + 6, W - 24, 22,
                juce::Justification::centredLeft, false);
        }
        else if (!isScale && curAnim == 1)
        {
            g.setFont(juce::Font("Arial", 11.0f, juce::Font::italic));
            g.setColour(ANIM_COL.withAlpha(0.5f));
            g.drawText("Cycles through all inversions of the chord on each tick.",
                PIANO_W + 12, ANIM_ROW3_Y + 6, W - 24, 22,
                juce::Justification::centredLeft, false);
        }
    }

    // ── Mode labels (dimmed section titles) ───────────────────
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    if (!isScale)
    {
        g.setColour(ACCENT.withAlpha(0.5f));
        g.drawText("MANUAL MODE", PIANO_W + 10, TAB_Y + 4, 130, 14,
            juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour(GREEN.withAlpha(0.5f));
        g.drawText("SCALE MODE", PIANO_W + 10, TAB_Y + 4, 130, 14,
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
    const int ox = PIANO_W;   // horizontal offset – all controls shift right by this

    // ── Piano-roll strip ──────────────────────────────────────
    pianoRoll.setBounds(0, 0, PIANO_W, TOTAL_H);

    // ── Header ────────────────────────────────────────────────
    {
        const int btnH = 28, btnY = 12;
        const int tabW = 74, tabGap = 5;

        // Tab buttons flush right (right edge = ox + W)
        scaleBtn.setBounds(ox + W - margin - tabW, btnY, tabW, btnH);
        manualBtn.setBounds(ox + W - margin - tabW * 2 - tabGap, btnY, tabW, btnH);

        // Utility buttons left of tabs (manual-mode only)
        const int utilW = 46, utilGap = 4;
        int utilX = ox + W - margin - tabW * 2 - tabGap - utilGap - utilW;
        loadPresetBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;
        savePresetBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;
        randomizeBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;

        // Audit button: always visible, left of RND group
        const int auditW = 42;
        auditBtn.setBounds(utilX, btnY, auditW, btnH);

        // Title fills remaining space
        titleLabel.setBounds(ox + margin, 10, utilX - (ox + margin) - 8, 28);
    }

    // ── Manual tab: column headers ────────────────────────────
    {
        const int semiW = 160, octW = 130;
        const int labelW = 56;
        const int semiX = ox + margin + toggleW + gap + labelW + gap;
        const int octX = semiX + semiW + gap;
        colSemiHdr.setBounds(semiX, TAB_Y + COL_HDR_OFFSET + 2, semiW, 14);
        colOctHdr.setBounds(octX, TAB_Y + COL_HDR_OFFSET + 2, octW, 14);

        // Manual rows
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        {
            const int rowY = MANUAL_ROWS_Y + i * MANUAL_ROW_H;
            const int cy = rowY + (MANUAL_ROW_H - comboH) / 2;
            rows[i].enableBtn.setBounds(ox + margin, rowY + (MANUAL_ROW_H - 22) / 2, toggleW, 22);
            rows[i].nameLabel.setBounds(ox + margin + toggleW + gap, cy, labelW, comboH);
            rows[i].semiCombo.setBounds(semiX, cy, semiW, comboH);
            rows[i].octCombo.setBounds(octX, cy, octW, comboH);
        }
        // Reset manual notes button sits to the right of row 0's octave combo
        {
            const int row0Y = MANUAL_ROWS_Y;
            const int cy = row0Y + (MANUAL_ROW_H - comboH) / 2;
            resetManualBtn.setBounds(octX + octW + gap + 4, cy, 52, comboH);
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
            int x = ox + margin;
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
            int x = ox + margin;
            harmonicModeLabel.setBounds(x, cy, hLblW, comboH); x += hLblW + 4;
            harmonicModeCombo.setBounds(x, cy, hCmbW, comboH);
        }

        // Toggle row 1
        {
            const int ty = SCALE_TOG1_Y + (30 - 22) / 2;
            int x = ox + margin;
            discardBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            discardLabel.setBounds(x, ty, 155, 22); x += 155 + 18;
            discardInputBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            discardInputLabel.setBounds(x, ty, 155, 22);
        }

        // Toggle row 2
        {
            const int ty = SCALE_TOG2_Y + (30 - 22) / 2;
            int x = ox + margin;
            randomSkipBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            randomSkipLabel.setBounds(x, ty, 155, 22); x += 155 + 18;
            lockBassBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            lockBassLabel.setBounds(x, ty, 155, 22); x += 155 + 12;
            resetColorsBtn.setBounds(x, ty, 82, 22);
        }

        // Inversion row
        {
            const int cy = SCALE_INV_Y + (38 - comboH) / 2;
            const int iLblW = 68, iCmbW = 150;
            const int pLblW = 38, pCmbW = 136;
            int x = ox + margin;
            invLabel.setBounds(x, cy, iLblW, comboH); x += iLblW + 4;
            invModeCombo.setBounds(x, cy, iCmbW, comboH); x += iCmbW + 14;
            invPickerLabel.setBounds(x, cy, pLblW, comboH); x += pLblW + 4;
            invPickerCombo.setBounds(x, cy, pCmbW, comboH);
        }

        // Chord preview
        previewLabel.setBounds(ox + margin, SCALE_PREV_Y + 2, W - margin * 2, 24);
    }

    // ── Animation rows – both sets share identical screen positions.
    //    Only one set is ever visible (toggled by updateModeUI).
    {
        // Row 1: mode + sync
        const int cy1 = ANIM_ROW1_Y + (ANIM_ROW_H - comboH) / 2;
        const int lblW = 62, modeCmbW = 128;
        const int syncTogW = 22, syncLblW = 80;

        // Scale anim row 1
        {
            int x = ox + margin;
            scaleAnimModeLabel.setBounds(x, cy1, lblW, comboH);      x += lblW + 4;
            scaleAnimModeCombo.setBounds(x, cy1, modeCmbW, comboH);  x += modeCmbW + 20;
            scaleAnimSyncBtn.setBounds(x, cy1, syncTogW, 22);      x += syncTogW + 4;
            scaleAnimSyncLabel.setBounds(x, cy1, syncLblW, comboH);
        }
        // Manual anim row 1  (same positions)
        {
            int x = ox + margin;
            manAnimModeLabel.setBounds(x, cy1, lblW, comboH);      x += lblW + 4;
            manAnimModeCombo.setBounds(x, cy1, modeCmbW, comboH);  x += modeCmbW + 20;
            manAnimSyncBtn.setBounds(x, cy1, syncTogW, 22);      x += syncTogW + 4;
            manAnimSyncLabel.setBounds(x, cy1, syncLblW, comboH);
        }
    }

    {
        // Row 2: rate
        const int cy2 = ANIM_ROW2_Y + (ANIM_ROW_H - comboH) / 2;
        const int rateLblW = 38, rateCmbW = 120;

        scaleAnimRateLabel.setBounds(ox + margin, cy2, rateLblW, comboH);
        scaleAnimRateCombo.setBounds(ox + margin + rateLblW + 4, cy2, rateCmbW, comboH);

        manAnimRateLabel.setBounds(ox + margin, cy2, rateLblW, comboH);
        manAnimRateCombo.setBounds(ox + margin + rateLblW + 4, cy2, rateCmbW, comboH);
    }

    {
        // Row 3: Wander (scale only) / Cloud params
        const int cy3 = ANIM_ROW3_Y + (ANIM_ROW_H - comboH) / 2;

        // Scale – Wander
        {
            const int pLblW = 38, pCmbW = 80;
            const int mLblW = 52, mCmbW = 80;
            int x = ox + margin;
            scaleWanderProbLabel.setBounds(x, cy3, pLblW, comboH); x += pLblW + 4;
            scaleWanderProbCombo.setBounds(x, cy3, pCmbW, comboH); x += pCmbW + 16;
            scaleWanderMaxLabel.setBounds(x, cy3, mLblW, comboH); x += mLblW + 4;
            scaleWanderMaxCombo.setBounds(x, cy3, mCmbW, comboH);
        }

        // Scale – Cloud row 3
        {
            const int dLblW = 52, dCmbW = 80;
            const int sLblW = 52, sCmbW = 72;
            const int dcLblW = 44, dcCmbW = 130;
            int x = ox + margin;
            scaleCloudDensityLabel.setBounds(x, cy3, dLblW, comboH); x += dLblW + 4;
            scaleCloudDensityCombo.setBounds(x, cy3, dCmbW, comboH); x += dCmbW + 16;
            scaleCloudSpreadLabel.setBounds(x, cy3, sLblW, comboH); x += sLblW + 4;
            scaleCloudSpreadCombo.setBounds(x, cy3, sCmbW, comboH); x += sCmbW + 16;
            scaleCloudDecayLabel.setBounds(x, cy3, dcLblW, comboH); x += dcLblW + 4;
            scaleCloudDecayCombo.setBounds(x, cy3, dcCmbW, comboH);
        }

        // Manual – Cloud row 3  (same positions as scale cloud)
        {
            const int dLblW = 52, dCmbW = 80;
            const int sLblW = 52, sCmbW = 72;
            const int dcLblW = 44, dcCmbW = 130;
            int x = ox + margin;
            manCloudDensityLabel.setBounds(x, cy3, dLblW, comboH); x += dLblW + 4;
            manCloudDensityCombo.setBounds(x, cy3, dCmbW, comboH); x += dCmbW + 16;
            manCloudOctaveLabel.setBounds(x, cy3, sLblW, comboH); x += sLblW + 4;
            manCloudOctaveCombo.setBounds(x, cy3, sCmbW, comboH); x += sCmbW + 16;
            manCloudDecayLabel.setBounds(x, cy3, dcLblW, comboH); x += dcLblW + 4;
            manCloudDecayCombo.setBounds(x, cy3, dcCmbW, comboH);
        }
    }

    {
        // Row 4: Cloud velocity range
        const int cy4 = ANIM_ROW4_Y + (ANIM_ROW_H - comboH) / 2;
        const int vLblW = 58, vCmbW = 110, dashW = 26;

        // Scale
        {
            int x = ox + margin;
            scaleCloudVelLabel.setBounds(x, cy4, vLblW, comboH); x += vLblW + 4;
            scaleCloudVelMinCombo.setBounds(x, cy4, vCmbW, comboH); x += vCmbW + 4;
            scaleCloudVelDashLabel.setBounds(x, cy4, dashW, comboH); x += dashW + 4;
            scaleCloudVelMaxCombo.setBounds(x, cy4, vCmbW, comboH);
        }
        // Manual
        {
            int x = ox + margin;
            manCloudVelLabel.setBounds(x, cy4, vLblW, comboH); x += vLblW + 4;
            manCloudVelMinCombo.setBounds(x, cy4, vCmbW, comboH); x += vCmbW + 4;
            manCloudVelDashLabel.setBounds(x, cy4, dashW, comboH); x += dashW + 4;
            manCloudVelMaxCombo.setBounds(x, cy4, vCmbW, comboH);
        }
    }
}