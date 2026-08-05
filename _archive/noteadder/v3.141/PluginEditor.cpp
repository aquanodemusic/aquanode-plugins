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

// ── Layout – all positions computed from the actual window size ───────────────
// Original design was 560 × 556 px (PIANO_W=50, content W=510, H=556).
// computeLayout() scales every value proportionally so the UI fills any size.

struct Layout
{
    static constexpr int PIANO_W = 50;   // piano-roll strip (fixed width)

    int w;        // content width  = totalW - PIANO_W
    int totalH;
    float sy;     // vertical scale relative to the original 556 px
    float sf;     // font scale factor (clamped to avoid tiny/huge text)

    // Scale a base font size, never below 8 pt
    float f(float base) const noexcept { return juce::jmax(8.0f, base * sf); }

    int headerH, ctrlRowH, ctrlRowY;
    int tabY, tabH, animY;
    int animLblH, animRowH;
    int animRow1Y, animRow2Y, animRow3Y, animRow4Y;
    int colHdrOffset, colHdrH, manualRowH, manualRowsY;
    int scaleRow1Y, scaleRow2Y, scaleTog1Y, scaleTog2Y;
    int scaleInvY, scalePrevY;
};

static Layout computeLayout(int width, int height)
{
    Layout L;
    L.w = width - Layout::PIANO_W;
    L.totalH = height;
    L.sy = height / 556.0f;
    // Font scale: blend of width and height scale, clamped to [0.70, 2.0]
    // to keep text legible at extremes.
    const float sxW = (width - Layout::PIANO_W) / 510.0f;
    const float sxH = height / 556.0f;
    L.sf = juce::jlimit(0.70f, 2.0f, (sxW + sxH) * 0.5f);

    auto s = [&](int v) { return juce::roundToInt(v * L.sy); };

    L.headerH = s(52);
    L.ctrlRowH = s(36);
    L.ctrlRowY = L.headerH;
    L.tabY = L.ctrlRowY + L.ctrlRowH;

    // Work backwards from the bottom so rows always pack tightly at the bottom edge.
    L.animRowH = s(34);
    L.animLblH = 22;
    L.animRow4Y = L.totalH - 10 - L.animRowH;
    L.animRow3Y = L.animRow4Y - L.animRowH;
    L.animRow2Y = L.animRow3Y - L.animRowH;
    L.animRow1Y = L.animRow2Y - L.animRowH;
    L.animY = L.animRow1Y - L.animLblH - 2;
    L.tabH = L.animY - L.tabY;

    L.colHdrOffset = 4;
    L.colHdrH = s(18);
    L.manualRowH = s(36);
    L.manualRowsY = L.tabY + L.colHdrOffset + L.colHdrH;

    L.scaleRow1Y = L.tabY + s(22);
    L.scaleRow2Y = L.scaleRow1Y + s(36);
    L.scaleTog1Y = L.scaleRow2Y + s(36);
    L.scaleTog2Y = L.scaleTog1Y + s(30);
    L.scaleInvY = L.scaleTog2Y + s(32);
    L.scalePrevY = L.scaleInvY + s(38);

    return L;
}

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

static void styleSliderWithBox(juce::Slider& s, juce::Colour accent)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
    s.setColour(juce::Slider::backgroundColourId, PANEL);
    s.setColour(juce::Slider::trackColourId, accent.withAlpha(0.55f));
    s.setColour(juce::Slider::thumbColourId, accent);
    s.setColour(juce::Slider::textBoxTextColourId, TEXT_MAIN);
    s.setColour(juce::Slider::textBoxBackgroundColourId, PANEL);
    s.setColour(juce::Slider::textBoxOutlineColourId, SEP);
}

//==============================================================================
NoteAdderEditor::NoteAdderEditor(NoteAdderProcessor& p)
    : AudioProcessorEditor(&p), proc(p), pianoRoll(p)
{
    setSize(560, 556);
    setResizable(true, true);
    setResizeLimits(420, 415, 1680, 1670);

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

    // ── REC button ────────────────────────────────────────────
    recBtn.setClickingTogglesState(true);
    recBtn.setColour(juce::TextButton::buttonColourId, RED.withAlpha(0.15f));
    recBtn.setColour(juce::TextButton::buttonOnColourId, RED.withAlpha(0.80f));
    recBtn.setColour(juce::TextButton::textColourOffId, RED);
    recBtn.setColour(juce::TextButton::textColourOnId, BG);
    recBtn.onClick = [this]
        {
            if (recBtn.getToggleState())
                startRecording();
            else
                stopRecordingAndSave();
        };
    recBlinker = std::make_unique<RecBlinkTimer>(*this);
    addAndMakeVisible(recBtn);

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

    // ── Global control row: Humanize Time + Humanize Vel ──────
    humanizeTimeLabel.setText("Humanize Time", juce::dontSendNotification);
    humanizeTimeLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    humanizeTimeLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    humanizeTimeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(humanizeTimeLabel);

    humanizeTimeSlider.setRange(0, 200, 1);
    humanizeTimeSlider.setTextValueSuffix(" ms");
    styleSliderWithBox(humanizeTimeSlider, PURPLE);
    addAndMakeVisible(humanizeTimeSlider);

    humanizeVelLabel.setText("Humanize Vel", juce::dontSendNotification);
    humanizeVelLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    humanizeVelLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    humanizeVelLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(humanizeVelLabel);

    humanizeVelSlider.setRange(0, 50, 1);
    styleSliderWithBox(humanizeVelSlider, GREEN);
    addAndMakeVisible(humanizeVelSlider);

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

NoteAdderEditor::~NoteAdderEditor()
{
    if (recBlinker) recBlinker->stopTimer();
}

// ── REC: start ────────────────────────────────────────────────────────────────
void NoteAdderEditor::startRecording()
{
    recordedEvents.clear();
    proc.recStartTime = 0.0;
    proc.isRecording.store(true, std::memory_order_release);
    recBlinker->startTimerHz(4);   // 4 Hz blink
}

// ── REC: stop + save MIDI file ────────────────────────────────────────────────
void NoteAdderEditor::stopRecordingAndSave()
{
    proc.isRecording.store(false, std::memory_order_release);
    recBlinker->stopTimer();

    // Drain any remaining events
    auto drained = proc.drainRecordedEvents();
    recordedEvents.insert(recordedEvents.end(), drained.begin(), drained.end());

    recBpm = proc.currentBpm.load(std::memory_order_relaxed);

    if (recordedEvents.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "NoteAdder Recorder",
            "Nothing was recorded.",
            "OK");
        return;
    }

    // Build the MIDI file
    const double bpm = recBpm > 0.0 ? recBpm : 120.0;
    const int    ppq = 480;
    const double secsPerBeat = 60.0 / bpm;
    const double ticksPerSec = ppq / secsPerBeat;

    juce::MidiMessageSequence seq;
    for (const auto& ev : recordedEvents)
    {
        auto msg = ev.message;
        msg.setTimeStamp(ev.timestampSeconds * ticksPerSec);
        seq.addEvent(msg);
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(ppq);

    // Tempo track
    juce::MidiMessageSequence tempoTrack;
    tempoTrack.addEvent(juce::MidiMessage::tempoMetaEvent(
        (int)(60'000'000.0 / bpm)));
    mf.addTrack(tempoTrack);
    mf.addTrack(seq);

    // Ask user where to save
    fileChooser = std::make_shared<juce::FileChooser>(
        "Save recorded MIDI",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
        .getChildFile("NoteAdder_recording.mid"),
        "*.mid");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this, mf](const juce::FileChooser& fc) mutable
        {
            auto result = fc.getResult();
            if (result == juce::File{}) return;   // user cancelled

            auto outStream = result.createOutputStream();
            if (outStream && mf.writeTo(*outStream))
            {
                //    juce::AlertWindow::showMessageBoxAsync(
                //        juce::MessageBoxIconType::InfoIcon,
                //        "NoteAdder Recorder",
                //        "MIDI saved to:\n" + result.getFullPathName(),
                //        "OK");
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "NoteAdder Recorder",
                    "Could not write file.",
                    "OK");
            }
        });
}



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
    g.setFont(juce::Font(juce::jmax(7.0f, 8.5f * getHeight() / 556.0f), juce::Font::bold));
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

    // ── B/W note colours button (white keys → white, black keys → near-black) ──
    bwColorsBtn.setColour(juce::TextButton::buttonColourId, TEXT_DIM.withAlpha(0.2f));
    bwColorsBtn.setColour(juce::TextButton::textColourOffId, TEXT_MAIN);
    bwColorsBtn.onClick = [this]
        {
            static const bool isBlackKey[12] = {
                false, true, false, true, false,
                false, true, false, true, false, true, false
            };
            for (int i = 0; i < 12; ++i)
                *proc.noteColorParams[i] = isBlackKey[i] ? 0x282828 : 0xFFFFFF;
            repaint();
        };
    addChildComponent(bwColorsBtn);

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
            if (id > 0) *proc.animRateParam = id - 1;
        };

    scaleAnimRateSlider.setRange(15.0, 2000.0, 1.0);
    scaleAnimRateSlider.setSkewFactorFromMidPoint(200.0);
    scaleAnimRateSlider.setTextValueSuffix(" ms");
    styleSliderWithBox(scaleAnimRateSlider, ANIM_COL);
    addChildComponent(scaleAnimRateSlider);

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

    scaleCloudDecaySlider.setRange(20.0, 2000.0, 1.0);
    scaleCloudDecaySlider.setSkewFactorFromMidPoint(300.0);
    scaleCloudDecaySlider.setTextValueSuffix(" ms");
    styleSliderWithBox(scaleCloudDecaySlider, ORANGE);
    addChildComponent(scaleCloudDecaySlider);

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
            if (id > 0) *proc.manualAnimRateParam = id - 1;
        };

    manAnimRateSlider.setRange(15.0, 2000.0, 1.0);
    manAnimRateSlider.setSkewFactorFromMidPoint(200.0);
    manAnimRateSlider.setTextValueSuffix(" ms");
    styleSliderWithBox(manAnimRateSlider, ANIM_COL);
    addChildComponent(manAnimRateSlider);

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

    manCloudDecaySlider.setRange(20.0, 2000.0, 1.0);
    manCloudDecaySlider.setSkewFactorFromMidPoint(300.0);
    manCloudDecaySlider.setTextValueSuffix(" ms");
    styleSliderWithBox(manCloudDecaySlider, ORANGE);
    addChildComponent(manCloudDecaySlider);

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

    // Global controls
    humanizeTimeAtt = std::make_unique<SA>(vts, "humanizetime", humanizeTimeSlider);
    humanizeVelAtt = std::make_unique<SA>(vts, "humanizevel", humanizeVelSlider);

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
    scaleAnimSyncAtt = std::make_unique<BA>(vts, "animsync", scaleAnimSyncBtn);
    scaleAnimRateSliderAtt = std::make_unique<SA>(vts, "animratefree", scaleAnimRateSlider);
    scaleWanderProbAtt = std::make_unique<CBA>(vts, "wanderprob", scaleWanderProbCombo);
    scaleWanderMaxAtt = std::make_unique<CBA>(vts, "wandermax", scaleWanderMaxCombo);
    scaleCloudDensAtt = std::make_unique<CBA>(vts, "clouddensity", scaleCloudDensityCombo);
    scaleCloudSpreadAtt = std::make_unique<CBA>(vts, "cloudspread", scaleCloudSpreadCombo);
    scaleCloudDecayAtt = std::make_unique<SA>(vts, "clouddecay", scaleCloudDecaySlider);
    scaleCloudVelMinAtt = std::make_unique<CBA>(vts, "cloudvelmin", scaleCloudVelMinCombo);
    scaleCloudVelMaxAtt = std::make_unique<CBA>(vts, "cloudvelmax", scaleCloudVelMaxCombo);

    // ── Manual animation ──────────────────────────────────────
    manAnimSyncAtt = std::make_unique<BA>(vts, "manimsync", manAnimSyncBtn);
    manAnimRateSliderAtt = std::make_unique<SA>(vts, "manimratefree", manAnimRateSlider);
    manCloudDensAtt = std::make_unique<CBA>(vts, "mclouddensity", manCloudDensityCombo);
    manCloudOctaveAtt = std::make_unique<CBA>(vts, "mcloudoctave", manCloudOctaveCombo);
    manCloudDecayAtt = std::make_unique<SA>(vts, "mclouddecay", manCloudDecaySlider);
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
    const bool syncOn = proc.animSyncBPMParam->get();

    // BPM sync: show subdivision combo, hide slider
    scaleAnimRateCombo.setVisible(syncOn);
    scaleAnimRateSlider.setVisible(!syncOn);

    if (syncOn)
    {
        scaleAnimRateCombo.clear(juce::dontSendNotification);
        static const char* labels[NoteAdderProcessor::NUM_SUBDIVS] = {
            "1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_SUBDIVS; ++i)
            scaleAnimRateCombo.addItem(labels[i], i + 1);
        scaleAnimRateCombo.setSelectedId(proc.animRateParam->get() + 1,
            juce::dontSendNotification);
    }
    // Slider value is handled by the SA attachment
}

void NoteAdderEditor::repopulateManualRateCombo()
{
    const bool syncOn = proc.manualAnimSyncBPMParam->get();

    manAnimRateCombo.setVisible(syncOn);
    manAnimRateSlider.setVisible(!syncOn);

    if (syncOn)
    {
        manAnimRateCombo.clear(juce::dontSendNotification);
        static const char* labels[NoteAdderProcessor::NUM_SUBDIVS] = {
            "1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_SUBDIVS; ++i)
            manAnimRateCombo.addItem(labels[i], i + 1);
        manAnimRateCombo.setSelectedId(proc.manualAnimRateParam->get() + 1,
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
    bwColorsBtn.setVisible(isScale);
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
    scaleAnimRateSlider.setVisible(isScale);

    // Manual anim section
    manAnimModeLabel.setVisible(!isScale);
    manAnimModeCombo.setVisible(!isScale);
    manAnimSyncBtn.setVisible(!isScale);
    manAnimSyncLabel.setVisible(!isScale);
    manAnimRateLabel.setVisible(!isScale);
    manAnimRateCombo.setVisible(!isScale);
    manAnimRateSlider.setVisible(!isScale);

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
        manCloudDecaySlider.setVisible(false);
        manCloudVelLabel.setVisible(false);
        manCloudVelDashLabel.setVisible(false);
        manCloudVelMinCombo.setVisible(false);
        manCloudVelMaxCombo.setVisible(false);
        manAnimRateSlider.setVisible(false);
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
        scaleCloudDecaySlider.setVisible(false);
        scaleCloudVelLabel.setVisible(false);
        scaleCloudVelDashLabel.setVisible(false);
        scaleCloudVelMinCombo.setVisible(false);
        scaleCloudVelMaxCombo.setVisible(false);
        scaleAnimRateSlider.setVisible(false);
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

    scaleAnimModeCombo.setSelectedId(animMode + 1, juce::dontSendNotification);

    scaleAnimRateLabel.setEnabled(animOn);
    scaleAnimRateCombo.setEnabled(animOn);
    scaleAnimRateSlider.setEnabled(animOn);
    scaleAnimSyncBtn.setEnabled(animOn);
    scaleAnimSyncLabel.setEnabled(animOn);

    // Sync toggle drives combo vs slider visibility
    repopulateScaleRateCombo();

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
    scaleCloudDecaySlider.setVisible(isCloud);
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

    manAnimModeCombo.setSelectedId(animMode + 1, juce::dontSendNotification);

    manAnimRateLabel.setEnabled(animOn);
    manAnimRateCombo.setEnabled(animOn);
    manAnimRateSlider.setEnabled(animOn);
    manAnimSyncBtn.setEnabled(animOn);
    manAnimSyncLabel.setEnabled(animOn);

    repopulateManualRateCombo();

    const bool isCloud = animOn && (animMode == 2);
    manCloudDensityLabel.setVisible(isCloud);
    manCloudDensityCombo.setVisible(isCloud);
    manCloudOctaveLabel.setVisible(isCloud);
    manCloudOctaveCombo.setVisible(isCloud);
    manCloudDecayLabel.setVisible(isCloud);
    manCloudDecaySlider.setVisible(isCloud);
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
            animManualEl->setAttribute("cloudOctave", proc.manualCloudOctaveParam->get());
            animManualEl->setAttribute("cloudVelMin", proc.manualCloudVelMinParam->get());
            animManualEl->setAttribute("cloudVelMax", proc.manualCloudVelMaxParam->get());
            animManualEl->setAttribute("cloudDecay", proc.manualCloudDecayParam->get());

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
                juce::AudioParameterFloat* rateFreeP,
                juce::AudioParameterInt* wanderProbP,   // nullptr for manual
                juce::AudioParameterInt* wanderMaxP,    // nullptr for manual
                juce::AudioParameterInt* densP,
                juce::AudioParameterInt* spreadP,       // nullptr for manual
                juce::AudioParameterInt* velMinP,
                juce::AudioParameterInt* velMaxP,
                juce::AudioParameterFloat* decayP)
                {
                    if (!el) return;
                    *modeP = el->getIntAttribute("mode", 0);
                    *syncP = el->getIntAttribute("syncBPM", 1) != 0;
                    *rateP = el->getIntAttribute("rate", 3);
                    *rateFreeP = (float)el->getDoubleAttribute("rateFree", 200.0);
                    if (wanderProbP) *wanderProbP = el->getIntAttribute("wanderProb", 4);
                    if (wanderMaxP)  *wanderMaxP = el->getIntAttribute("wanderMax", 2);
                    *densP = el->getIntAttribute("cloudDensity", 1);
                    if (spreadP) *spreadP = el->getIntAttribute("cloudSpread", 3);
                    *velMinP = el->getIntAttribute("cloudVelMin", 1);
                    *velMaxP = el->getIntAttribute("cloudVelMax", 4);
                    *decayP = (float)el->getDoubleAttribute("cloudDecay", 200.0);
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
    cb.addItem("0 oct (same)", 1);  // param 0
    cb.addItem("1 oct", 2);  // param 1
    cb.addItem("2 oct", 3);  // param 2
    cb.addItem("3 oct", 4);  // param 3
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
    auto L = computeLayout(getWidth(), getHeight());
    const int REGION_TOP = L.scalePrevY + 28;
    const int REGION_BOTTOM = L.animY - 4;
    const int REGION_X = Layout::PIANO_W + 12;
    const int REGION_W = L.w - 24;
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
    auto L = computeLayout(getWidth(), getHeight());
    const int REGION_TOP = L.scalePrevY + 28;
    const int REGION_X = Layout::PIANO_W + 12;
    const int REGION_W = L.w - 24;
    const int REGION_H = L.animY - 4 - REGION_TOP;
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
    auto L = computeLayout(getWidth(), getHeight());

    // ── layout ────────────────────────────────────────────────
    const int REGION_TOP = L.scalePrevY + 28;
    const int REGION_BOTTOM = L.animY - 4;
    const int REGION_H = REGION_BOTTOM - REGION_TOP;
    const int REGION_X = Layout::PIANO_W + 12;
    const int REGION_W = L.w - 24;

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
    g.setFont(juce::Font(L.f(9.5f), juce::Font::bold));
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
                const float fs = L.f(isSharp ? 8.5f : 11.0f);
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
                const float fs = L.f(isSharp ? 7.5f : 9.5f);
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
            const float dotR = 3.2f * L.sf;
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
                g.setFont(juce::Font(L.f(9.0f)));
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
    auto L = computeLayout(getWidth(), getHeight());
    const int W = L.w;

    g.fillAll(BG);

    // ── Header gradient ───────────────────────────────────────
    g.setGradientFill(juce::ColourGradient(
        PANEL.brighter(0.05f), 0.0f, 0.0f,
        BG, 0.0f, (float)L.headerH, false));
    g.fillRect(Layout::PIANO_W, 0, W, L.headerH);

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

    // REC button glow (pulsing halo when recording)
    if (proc.isRecording.load(std::memory_order_relaxed))
    {
        const bool blinkOn = ((juce::Time::getMillisecondCounter() / 250) % 2) == 0;
        if (blinkOn)
        {
            auto rb = recBtn.getBounds().expanded(8, 6);
            g.setGradientFill(juce::ColourGradient(
                RED.withAlpha(0.45f), (float)rb.getCentreX(), (float)rb.getCentreY(),
                RED.withAlpha(0.0f), (float)rb.getRight(), (float)rb.getCentreY(),
                true));
            g.fillEllipse(rb.toFloat());
        }
    }

    g.setColour(SEP);
    g.fillRect(Layout::PIANO_W + 10, L.headerH - 1, W - 20, 1);

    // ── Global control row background ─────────────────────────
    g.setColour(PANEL.withAlpha(0.6f));
    g.fillRect(Layout::PIANO_W, L.ctrlRowY, W, L.ctrlRowH);
    g.setColour(SEP.withAlpha(0.5f));
    g.fillRect(Layout::PIANO_W + 10, L.ctrlRowY + L.ctrlRowH - 1, W - 20, 1);

    // ── Tab pane background ───────────────────────────────────
    const bool isScale = (proc.modeParam->get() == 1);

    g.setColour(isScale ? GREEN.withAlpha(0.03f) : ACCENT.withAlpha(0.03f));
    g.fillRect(Layout::PIANO_W, L.tabY, W, L.tabH);

    // Subtle tab active indicator on the top edge
    g.setColour(isScale ? GREEN.withAlpha(0.35f) : ACCENT.withAlpha(0.35f));
    g.fillRect(Layout::PIANO_W, L.tabY, W, 2);

    if (!isScale)
    {
        // Manual: stripe the rows
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        {
            g.setColour((i % 2 == 0 ? PANEL : PANEL_ALT).withAlpha(0.75f));
            g.fillRect(Layout::PIANO_W, L.manualRowsY + i * L.manualRowH, W, L.manualRowH);
        }
    }
    else
    {
        // Scale: subtle inversion row highlight
        g.setColour(PURPLE.withAlpha(0.06f));
        g.fillRect(Layout::PIANO_W, L.scaleInvY, W, 38);
        g.setColour(PURPLE.withAlpha(0.18f));
        g.fillRect(Layout::PIANO_W + 10, L.scaleInvY, W - 20, 1);
        g.fillRect(Layout::PIANO_W + 10, L.scaleInvY + 37, W - 20, 1);

        // Scale colour key in the free space below the chord preview
        drawScaleColorKey(g);
    }

    // ── Separator before anim section ─────────────────────────
    g.setColour(ANIM_COL.withAlpha(0.22f));
    g.fillRect(Layout::PIANO_W + 10, L.animY + 1, W - 20, 1);

    g.setColour(ANIM_COL.withAlpha(0.04f));
    g.fillRect(Layout::PIANO_W, L.animY, W, L.totalH - L.animY);

    // ── Anim section label ────────────────────────────────────
    g.setFont(juce::Font(L.f(10.5f), juce::Font::bold));
    {
        const bool isScale2 = (proc.modeParam->get() == 1);
        const int  curAnim = isScale2 ? proc.animModeParam->get()
            : proc.manualAnimModeParam->get();
        g.setColour(curAnim != 0 ? ANIM_COL.withAlpha(0.9f) : TEXT_DIM.withAlpha(0.5f));
        g.drawText("ANIMATION", Layout::PIANO_W + 10, L.animY + 4, 120, 16,
            juce::Justification::centredLeft, false);

        // Hint text for modes that have no param sub-widgets on row 3
        if (isScale2 && (curAnim == 1 || curAnim == 3))
        {
            static const char* hints[] = {
                "",
                "Cycles through all inversions of the chord on each tick.",
                "",
                "Chord root walks up the scale one degree per tick.",
            };
            g.setFont(juce::Font("Arial", L.f(11.0f), juce::Font::italic));
            g.setColour(ANIM_COL.withAlpha(0.5f));
            g.drawText(hints[curAnim], Layout::PIANO_W + 12, L.animRow3Y + 6, W - 24, 22,
                juce::Justification::centredLeft, false);
        }
        else if (!isScale2 && curAnim == 1)
        {
            g.setFont(juce::Font("Arial", L.f(11.0f), juce::Font::italic));
            g.setColour(ANIM_COL.withAlpha(0.5f));
            g.drawText("Cycles through all inversions of the chord on each tick.",
                Layout::PIANO_W + 12, L.animRow3Y + 6, W - 24, 22,
                juce::Justification::centredLeft, false);
        }
    }

    // ── Mode labels (dimmed section titles) ───────────────────
    g.setFont(juce::Font(L.f(10.0f), juce::Font::bold));
    if (!isScale)
    {
        g.setColour(ACCENT.withAlpha(0.5f));
        g.drawText("MANUAL MODE", Layout::PIANO_W + 10, L.tabY + 4, 130, 14,
            juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour(GREEN.withAlpha(0.5f));
        g.drawText("SCALE MODE", Layout::PIANO_W + 10, L.tabY + 4, 130, 14,
            juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// ── updateFonts ───────────────────────────────────────────────────────────────
// Re-applied every resize so all labels scale with the window.

void NoteAdderEditor::updateFonts()
{
    auto L = computeLayout(getWidth(), getHeight());

    const juce::Font titleFont("Arial", L.f(20.0f), juce::Font::bold);
    const juce::Font labelFont(L.f(12.0f), juce::Font::bold);
    const juce::Font smallFont(L.f(12.0f), 0);
    const juce::Font dimFont(L.f(11.0f), juce::Font::bold);
    const juce::Font rowFont(L.f(13.0f), 0);
    const juce::Font previewFnt("Arial", L.f(12.0f), juce::Font::italic);

    // ── Header ────────────────────────────────────────────────
    titleLabel.setFont(titleFont);

    // ── Global control row ────────────────────────────────────
    humanizeTimeLabel.setFont(dimFont);
    humanizeVelLabel.setFont(dimFont);

    // ── Manual tab ────────────────────────────────────────────
    colSemiHdr.setFont(dimFont);
    colOctHdr.setFont(dimFont);
    for (auto& r : rows)
        r.nameLabel.setFont(rowFont);

    // ── Scale tab ─────────────────────────────────────────────
    for (auto* lbl : { &rootLabel, &scaleLabel, &countLabel,
                       &harmonicModeLabel, &invLabel, &invPickerLabel })
        lbl->setFont(labelFont);

    discardLabel.setFont(smallFont);
    discardInputLabel.setFont(smallFont);
    randomSkipLabel.setFont(smallFont);
    lockBassLabel.setFont(smallFont);
    previewLabel.setFont(previewFnt);

    // ── Scale animation ───────────────────────────────────────
    for (auto* lbl : { &scaleAnimModeLabel, &scaleAnimRateLabel,
                       &scaleWanderProbLabel, &scaleWanderMaxLabel,
                       &scaleCloudDensityLabel, &scaleCloudSpreadLabel,
                       &scaleCloudDecayLabel, &scaleCloudVelLabel })
        lbl->setFont(labelFont);
    scaleAnimSyncLabel.setFont(smallFont);
    scaleCloudVelDashLabel.setFont(smallFont);

    // ── Manual animation ──────────────────────────────────────
    for (auto* lbl : { &manAnimModeLabel, &manAnimRateLabel,
                       &manCloudDensityLabel, &manCloudOctaveLabel,
                       &manCloudDecayLabel, &manCloudVelLabel })
        lbl->setFont(labelFont);
    manAnimSyncLabel.setFont(smallFont);
    manCloudVelDashLabel.setFont(smallFont);
}

//==============================================================================
// ── resized ───────────────────────────────────────────────────────────────────

void NoteAdderEditor::resized()
{
    updateFonts();
    auto L = computeLayout(getWidth(), getHeight());

    const int margin = juce::roundToInt(12 * L.sf);
    const int comboH = juce::roundToInt(26 * L.sf);
    const int toggleW = juce::roundToInt(22 * L.sf);
    const int gap = juce::roundToInt(6 * L.sf);
    const int ox = Layout::PIANO_W;   // horizontal offset – all controls shift right by this
    const int W = L.w;

    // ── Piano-roll strip ──────────────────────────────────────
    pianoRoll.setBounds(0, 0, Layout::PIANO_W, L.totalH);

    // ── Header ────────────────────────────────────────────────
    {
        const int btnH = juce::roundToInt(28 * L.sf);
        const int btnY = juce::roundToInt(12 * L.sf);
        const int tabW = juce::roundToInt(74 * L.sf), tabGap = juce::roundToInt(5 * L.sf);

        scaleBtn.setBounds(ox + W - margin - tabW, btnY, tabW, btnH);
        manualBtn.setBounds(ox + W - margin - tabW * 2 - tabGap, btnY, tabW, btnH);

        const int utilW = juce::roundToInt(46 * L.sf), utilGap = juce::roundToInt(4 * L.sf);
        int utilX = ox + W - margin - tabW * 2 - tabGap - utilGap - utilW;
        loadPresetBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;
        savePresetBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;
        randomizeBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;

        const int auditW = juce::roundToInt(42 * L.sf);
        auditBtn.setBounds(utilX, btnY, auditW, btnH);

        titleLabel.setBounds(ox + margin, btnY - juce::roundToInt(2 * L.sf),
            utilX - (ox + margin) - 8, btnH);
    }

    // ── Global control row: Humanize Time + Humanize Vel ─────
    {
        const int rowY = L.ctrlRowY;
        const int rowH = L.ctrlRowH;
        const int cy = rowY + (rowH - 22) / 2;
        const int lblW = juce::roundToInt(74 * L.sf);
        const int sliderW = juce::roundToInt(155 * L.sf);
        const int sliderH = juce::roundToInt(24 * L.sf);
        const int lhGap = juce::roundToInt(4 * L.sf);
        const int secGap = juce::roundToInt(20 * L.sf);
        int x = ox + margin;

        humanizeTimeLabel.setBounds(x, cy, lblW, juce::roundToInt(20 * L.sf)); x += lblW + lhGap;
        humanizeTimeSlider.setBounds(x, cy - 2, sliderW, sliderH);             x += sliderW + secGap;
        humanizeVelLabel.setBounds(x, cy, lblW, juce::roundToInt(20 * L.sf));  x += lblW + lhGap;
        humanizeVelSlider.setBounds(x, cy - 2, sliderW, sliderH);
    }

    // ── REC button: always visible in bottom-right corner ─────
    {
        const int recW = juce::roundToInt(46 * L.sf);
        const int recH = juce::roundToInt(28 * L.sf);
        const int margin2 = juce::roundToInt(10 * L.sf);
        recBtn.setBounds(ox + W - margin2 - recW,
            L.totalH - margin2 - recH, recW, recH);
    }

    // ── Manual tab: column headers ────────────────────────────
    {
        const int semiW = juce::roundToInt(160 * L.sf);
        const int octW = juce::roundToInt(130 * L.sf);
        const int labelW = juce::roundToInt(56 * L.sf);
        const int semiX = ox + margin + toggleW + gap + labelW + gap;
        const int octX = semiX + semiW + gap;
        colSemiHdr.setBounds(semiX, L.tabY + L.colHdrOffset + 2, semiW, 14);
        colOctHdr.setBounds(octX, L.tabY + L.colHdrOffset + 2, octW, 14);

        // Manual rows
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        {
            const int rowY = L.manualRowsY + i * L.manualRowH;
            const int cy = rowY + (L.manualRowH - comboH) / 2;
            rows[i].enableBtn.setBounds(ox + margin, rowY + (L.manualRowH - toggleW) / 2, toggleW, toggleW);
            rows[i].nameLabel.setBounds(ox + margin + toggleW + gap, cy, labelW, comboH);
            rows[i].semiCombo.setBounds(semiX, cy, semiW, comboH);
            rows[i].octCombo.setBounds(octX, cy, octW, comboH);
        }
        // Reset manual notes button sits to the right of row 0's octave combo
        {
            const int row0Y = L.manualRowsY;
            const int cy = row0Y + (L.manualRowH - comboH) / 2;
            const int rstW = juce::roundToInt(52 * L.sf);
            resetManualBtn.setBounds(octX + octW + gap + 4, cy, rstW, comboH);
        }
    }

    // ── Scale tab ─────────────────────────────────────────────
    {
        // Row 1: Root / Scale / Count
        {
            const int cy = L.scaleRow1Y + (36 - comboH) / 2;
            const int rLblW = juce::roundToInt(32 * L.sf), rCmbW = juce::roundToInt(58 * L.sf);
            const int sLblW = juce::roundToInt(50 * L.sf), sCmbW = juce::roundToInt(136 * L.sf);
            const int cLblW = juce::roundToInt(28 * L.sf), cCmbW = juce::roundToInt(80 * L.sf);
            const int g4 = juce::roundToInt(4 * L.sf), g10 = juce::roundToInt(10 * L.sf);
            int x = ox + margin;
            rootLabel.setBounds(x, cy, rLblW, comboH); x += rLblW + g4;
            rootCombo.setBounds(x, cy, rCmbW, comboH); x += rCmbW + g10;
            scaleLabel.setBounds(x, cy, sLblW, comboH); x += sLblW + g4;
            scaleCombo.setBounds(x, cy, sCmbW, comboH); x += sCmbW + g10;
            countLabel.setBounds(x, cy, cLblW, comboH); x += cLblW + g4;
            countCombo.setBounds(x, cy, cCmbW, comboH);
        }

        // Row 2: Harmonic mode
        {
            const int cy = L.scaleRow2Y + (36 - comboH) / 2;
            const int hLblW = juce::roundToInt(62 * L.sf), hCmbW = juce::roundToInt(250 * L.sf);
            int x = ox + margin;
            harmonicModeLabel.setBounds(x, cy, hLblW, comboH); x += hLblW + gap;
            harmonicModeCombo.setBounds(x, cy, hCmbW, comboH);
        }

        // Toggle row 1
        {
            const int ty = L.scaleTog1Y + (30 - 22) / 2;
            const int tLblW = juce::roundToInt(155 * L.sf);
            const int btnW = juce::roundToInt(82 * L.sf);
            int x = ox + margin;
            discardBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            discardLabel.setBounds(x, ty, tLblW, 22); x += tLblW + juce::roundToInt(18 * L.sf);
            discardInputBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            discardInputLabel.setBounds(x, ty, tLblW, 22); x += tLblW + juce::roundToInt(12 * L.sf);
            bwColorsBtn.setBounds(x, ty, btnW, 22);
        }

        // Toggle row 2
        {
            const int ty = L.scaleTog2Y + (30 - 22) / 2;
            const int tLblW = juce::roundToInt(155 * L.sf);
            const int btnW = juce::roundToInt(82 * L.sf);
            int x = ox + margin;
            randomSkipBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            randomSkipLabel.setBounds(x, ty, tLblW, 22); x += tLblW + juce::roundToInt(18 * L.sf);
            lockBassBtn.setBounds(x, ty, toggleW, 22); x += toggleW + 3;
            lockBassLabel.setBounds(x, ty, tLblW, 22); x += tLblW + juce::roundToInt(12 * L.sf);
            resetColorsBtn.setBounds(x, ty, btnW, 22);
        }

        // Inversion row
        {
            const int cy = L.scaleInvY + (38 - comboH) / 2;
            const int iLblW = juce::roundToInt(68 * L.sf), iCmbW = juce::roundToInt(150 * L.sf);
            const int pLblW = juce::roundToInt(38 * L.sf), pCmbW = juce::roundToInt(136 * L.sf);
            const int g4 = juce::roundToInt(4 * L.sf), g14 = juce::roundToInt(14 * L.sf);
            int x = ox + margin;
            invLabel.setBounds(x, cy, iLblW, comboH); x += iLblW + g4;
            invModeCombo.setBounds(x, cy, iCmbW, comboH); x += iCmbW + g14;
            invPickerLabel.setBounds(x, cy, pLblW, comboH); x += pLblW + g4;
            invPickerCombo.setBounds(x, cy, pCmbW, comboH);
        }

        // Chord preview
        previewLabel.setBounds(ox + margin, L.scalePrevY + 2, W - margin * 2, comboH);
    }

    // ── Animation rows – both sets share identical screen positions.
    //    Only one set is ever visible (toggled by updateModeUI).
    {
        // Row 1: mode + sync
        const int cy1 = L.animRow1Y + (L.animRowH - comboH) / 2;
        const int lblW = juce::roundToInt(62 * L.sf);
        const int modeCmbW = juce::roundToInt(128 * L.sf);
        const int syncTogW = juce::roundToInt(22 * L.sf);
        const int syncLblW = juce::roundToInt(80 * L.sf);
        const int g20 = juce::roundToInt(20 * L.sf);

        // Scale anim row 1
        {
            int x = ox + margin;
            scaleAnimModeLabel.setBounds(x, cy1, lblW, comboH);     x += lblW + gap;
            scaleAnimModeCombo.setBounds(x, cy1, modeCmbW, comboH); x += modeCmbW + g20;
            scaleAnimSyncBtn.setBounds(x, cy1, syncTogW, syncTogW); x += syncTogW + gap;
            scaleAnimSyncLabel.setBounds(x, cy1, syncLblW, comboH);
        }
        // Manual anim row 1  (same positions)
        {
            int x = ox + margin;
            manAnimModeLabel.setBounds(x, cy1, lblW, comboH);     x += lblW + gap;
            manAnimModeCombo.setBounds(x, cy1, modeCmbW, comboH); x += modeCmbW + g20;
            manAnimSyncBtn.setBounds(x, cy1, syncTogW, syncTogW); x += syncTogW + gap;
            manAnimSyncLabel.setBounds(x, cy1, syncLblW, comboH);
        }
    }

    {
        // Row 2: rate (combo for BPM-sync, slider for free)
        const int cy2 = L.animRow2Y + (L.animRowH - comboH) / 2;
        const int rateLblW = juce::roundToInt(38 * L.sf);
        const int rateCmbW = juce::roundToInt(120 * L.sf);
        const int rateSlW = juce::roundToInt(200 * L.sf);

        scaleAnimRateLabel.setBounds(ox + margin, cy2, rateLblW, comboH);
        scaleAnimRateCombo.setBounds(ox + margin + rateLblW + gap, cy2, rateCmbW, comboH);
        scaleAnimRateSlider.setBounds(ox + margin + rateLblW + gap, cy2 - 1, rateSlW, comboH + 2);

        manAnimRateLabel.setBounds(ox + margin, cy2, rateLblW, comboH);
        manAnimRateCombo.setBounds(ox + margin + rateLblW + gap, cy2, rateCmbW, comboH);
        manAnimRateSlider.setBounds(ox + margin + rateLblW + gap, cy2 - 1, rateSlW, comboH + 2);
    }

    {
        // Row 3: Wander (scale only) / Cloud params
        const int cy3 = L.animRow3Y + (L.animRowH - comboH) / 2;

        // Scale – Wander
        {
            const int pLblW = juce::roundToInt(38 * L.sf), pCmbW = juce::roundToInt(80 * L.sf);
            const int mLblW = juce::roundToInt(52 * L.sf), mCmbW = juce::roundToInt(80 * L.sf);
            const int g16 = juce::roundToInt(16 * L.sf);
            int x = ox + margin;
            scaleWanderProbLabel.setBounds(x, cy3, pLblW, comboH); x += pLblW + gap;
            scaleWanderProbCombo.setBounds(x, cy3, pCmbW, comboH); x += pCmbW + g16;
            scaleWanderMaxLabel.setBounds(x, cy3, mLblW, comboH);  x += mLblW + gap;
            scaleWanderMaxCombo.setBounds(x, cy3, mCmbW, comboH);
        }

        // Scale – Cloud row 3
        {
            const int dLblW = juce::roundToInt(52 * L.sf), dCmbW = juce::roundToInt(80 * L.sf);
            const int sLblW = juce::roundToInt(52 * L.sf), sCmbW = juce::roundToInt(72 * L.sf);
            const int dcLblW = juce::roundToInt(44 * L.sf), dcSlW = juce::roundToInt(140 * L.sf);
            const int g16 = juce::roundToInt(16 * L.sf);
            int x = ox + margin;
            scaleCloudDensityLabel.setBounds(x, cy3, dLblW, comboH);  x += dLblW + gap;
            scaleCloudDensityCombo.setBounds(x, cy3, dCmbW, comboH);  x += dCmbW + g16;
            scaleCloudSpreadLabel.setBounds(x, cy3, sLblW, comboH);   x += sLblW + gap;
            scaleCloudSpreadCombo.setBounds(x, cy3, sCmbW, comboH);   x += sCmbW + g16;
            scaleCloudDecayLabel.setBounds(x, cy3, dcLblW, comboH);   x += dcLblW + gap;
            scaleCloudDecaySlider.setBounds(x, cy3 - 1, dcSlW, comboH + 2);
        }

        // Manual – Cloud row 3
        {
            const int dLblW = juce::roundToInt(52 * L.sf), dCmbW = juce::roundToInt(80 * L.sf);
            const int sLblW = juce::roundToInt(52 * L.sf), sCmbW = juce::roundToInt(72 * L.sf);
            const int dcLblW = juce::roundToInt(44 * L.sf), dcSlW = juce::roundToInt(140 * L.sf);
            const int g16 = juce::roundToInt(16 * L.sf);
            int x = ox + margin;
            manCloudDensityLabel.setBounds(x, cy3, dLblW, comboH); x += dLblW + gap;
            manCloudDensityCombo.setBounds(x, cy3, dCmbW, comboH); x += dCmbW + g16;
            manCloudOctaveLabel.setBounds(x, cy3, sLblW, comboH);  x += sLblW + gap;
            manCloudOctaveCombo.setBounds(x, cy3, sCmbW, comboH);  x += sCmbW + g16;
            manCloudDecayLabel.setBounds(x, cy3, dcLblW, comboH);  x += dcLblW + gap;
            manCloudDecaySlider.setBounds(x, cy3 - 1, dcSlW, comboH + 2);
        }
    }

    {
        // Row 4: Cloud velocity range
        const int cy4 = L.animRow4Y + (L.animRowH - comboH) / 2;
        const int vLblW = juce::roundToInt(58 * L.sf);
        const int vCmbW = juce::roundToInt(110 * L.sf);
        const int dashW = juce::roundToInt(26 * L.sf);

        // Scale
        {
            int x = ox + margin;
            scaleCloudVelLabel.setBounds(x, cy4, vLblW, comboH);    x += vLblW + gap;
            scaleCloudVelMinCombo.setBounds(x, cy4, vCmbW, comboH); x += vCmbW + gap;
            scaleCloudVelDashLabel.setBounds(x, cy4, dashW, comboH); x += dashW + gap;
            scaleCloudVelMaxCombo.setBounds(x, cy4, vCmbW, comboH);
        }
        // Manual
        {
            int x = ox + margin;
            manCloudVelLabel.setBounds(x, cy4, vLblW, comboH);    x += vLblW + gap;
            manCloudVelMinCombo.setBounds(x, cy4, vCmbW, comboH); x += vCmbW + gap;
            manCloudVelDashLabel.setBounds(x, cy4, dashW, comboH); x += dashW + gap;
            manCloudVelMaxCombo.setBounds(x, cy4, vCmbW, comboH);
        }
    }
}