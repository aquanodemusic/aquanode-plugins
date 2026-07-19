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
    int ctrlRow2H, ctrlRow2Y;   // second global-controls row (Transpose)
    int tabY, tabH, animY;
    int animLblH, animRowH;
    int animRow1Y, animRow2Y, animRow3Y, animRow4Y;
    int colHdrOffset, colHdrH, manualRowH, manualRowsY;
    int scaleRow1Y, scaleRow2Y, scaleTranspRowY, scaleTog1Y, scaleTog2Y;
    int scaleInvY, scalePrevY;
};

static Layout computeLayout(int width, int height)
{
    Layout L;
    L.w = width - Layout::PIANO_W;
    L.totalH = height;
    L.sy = height / 592.0f;   // design height is now 592 (added Transpose row)
    // Font scale: blend of width and height scale, clamped to [0.70, 2.0]
    // to keep text legible at extremes.
    const float sxW = (width - Layout::PIANO_W) / 510.0f;
    const float sxH = height / 592.0f;
    L.sf = juce::jlimit(0.70f, 2.0f, (sxW + sxH) * 0.5f);

    auto s = [&](int v) { return juce::roundToInt(v * L.sy); };

    L.headerH = s(52);
    L.ctrlRowH = s(36);
    L.ctrlRowY = L.headerH;
    L.ctrlRow2H = s(36);
    L.ctrlRow2Y = L.ctrlRowY + L.ctrlRowH;
    L.tabY = L.ctrlRow2Y + L.ctrlRow2H;

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
    L.scaleTranspRowY = L.scaleRow2Y + s(36);   // Scale Transpose slider row (scale-mode only)
    L.scaleTog1Y = L.scaleTranspRowY + s(36);   // toggle rows shifted down one slot
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
    setSize(560, 592);
    setResizable(true, true);
    setResizeLimits(420, 451, 1680, 1706);

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
    titleLabel.setInterceptsMouseClicks(false, false);  // let double-clicks fall through to editor
    addAndMakeVisible(titleLabel);

    for (auto* btn : { &manualBtn, &scaleBtn, &customBtn })
    {
        btn->setClickingTogglesState(false);
        addAndMakeVisible(btn);
    }
    manualBtn.onClick = [this] { *proc.modeParam = 0; updateModeUI(); };
    scaleBtn.onClick = [this] { *proc.modeParam = 1; updateModeUI(); };
    customBtn.onClick = [this] { *proc.modeParam = 2; updateModeUI(); };

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

    humanizeVelSlider.setRange(0, 100, 1);
    styleSliderWithBox(humanizeVelSlider, GREEN);
    addAndMakeVisible(humanizeVelSlider);

    // ── Global transpose (in anim row 1, always visible) ─────
    globalTransposeLabel.setText("Transpose", juce::dontSendNotification);
    globalTransposeLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    globalTransposeLabel.setColour(juce::Label::textColourId, YELLOW.withAlpha(0.85f));
    globalTransposeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(globalTransposeLabel);

    globalTransposeSlider.setRange(-12.0, 12.0, 1.0);
    globalTransposeSlider.setTextValueSuffix(" st");
    styleSliderWithBox(globalTransposeSlider, YELLOW);
    addAndMakeVisible(globalTransposeSlider);

    // ── Scale in-scale transpose (scale mode tab, row 3 – visible only in Scale mode) ─
    scaleTranspLabel.setText("Scale Transp.", juce::dontSendNotification);
    scaleTranspLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    scaleTranspLabel.setColour(juce::Label::textColourId, GREEN.withAlpha(0.85f));
    scaleTranspLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(scaleTranspLabel);

    scaleTranspSlider.setRange(-12.0, 12.0, 1.0);
    scaleTranspSlider.setTextValueSuffix("");
    styleSliderWithBox(scaleTranspSlider, GREEN);
    addAndMakeVisible(scaleTranspSlider);

    // ── Add Length value box (always visible, right of Global Transpose) ─────
    addLengthLabel.setText("Decay Length", juce::dontSendNotification);
    addLengthLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    addLengthLabel.setColour(juce::Label::textColourId, TEAL.withAlpha(0.85f));
    addLengthLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(addLengthLabel);

    addLengthSlider.setRange(0.0, 10.0, 0.01);
    addLengthSlider.setTextValueSuffix(" s");
    styleSliderWithBox(addLengthSlider, TEAL);
    addAndMakeVisible(addLengthSlider);
    buildManualTab();
    buildScaleTab();
    buildScaleAnimSection();
    buildManualAnimSection();
    buildPerNoteTab();
    buildPerNoteAnimSection();

    // ── APVTS attachments ─────────────────────────────────────
    createAttachments();

    // ── Initial combo state ───────────────────────────────────
    repopulateScaleRateCombo();
    repopulateManualRateCombo();
    repopulatePerNoteRateCombo();
    rebuildPickerCombo();
    updateModeUI();
    updateInversionUI();
    updateChordPreview();

    // ── Listen for mode changes from DAW state restore / automation ──
    proc.apvts.addParameterListener("mode", this);
}

NoteAdderEditor::~NoteAdderEditor()
{
    proc.apvts.removeParameterListener("mode", this);
    if (recBlinker) recBlinker->stopTimer();
}

// ── Parameter listener – handles mode changes from DAW state restore / automation ──
void NoteAdderEditor::parameterChanged(const juce::String& paramID, float /*newValue*/)
{
    if (paramID == "mode")
        juce::MessageManager::callAsync([this] { updateModeUI(); });
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

    for (auto* btn : { &rndActivateBtn, &rndSemiBtn, &rndOctBtn })
    {
        btn->setColour(juce::TextButton::buttonColourId, PURPLE.withAlpha(0.3f));
        btn->setColour(juce::TextButton::textColourOffId, PURPLE);
        addChildComponent(*btn);
    }
    rndActivateBtn.onClick = [this] {
        juce::Random r;
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i) {
            bool en = (r.nextFloat() > 0.45f);
            *proc.enabledParams[i] = en;
            rows[i].enableBtn.setToggleState(en, juce::sendNotification);
        }
        };
    rndSemiBtn.onClick = [this] {
        juce::Random r;
        static const int pool[] = { -12,-7,-5,-4,-3,0,3,4,5,7,8,9,12 };
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i) {
            int s = pool[r.nextInt(13)];
            *proc.semiParams[i] = s;
            rows[i].semiCombo.setSelectedId(semiToId(s), juce::sendNotification);
        }
        };
    rndOctBtn.onClick = [this] {
        juce::Random r;
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i) {
            int o = r.nextInt(4) - 1;
            *proc.octaveParams[i] = o;
            rows[i].octCombo.setSelectedId(octToId(o), juce::sendNotification);
        }
        };
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

    harmonicMode2AndLabel.setText("and", juce::dontSendNotification);
    harmonicMode2AndLabel.setFont(juce::Font(10.0f));
    harmonicMode2AndLabel.setJustificationType(juce::Justification::centred);
    harmonicMode2AndLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(harmonicMode2AndLabel);

    harmonicMode2Combo.addItem("None", 1);  // param 0
    harmonicMode2Combo.addItem("2nds", 2);  // param 1
    harmonicMode2Combo.addItem("3rds", 3);  // param 2
    harmonicMode2Combo.addItem("4ths", 4);  // param 3
    harmonicMode2Combo.addItem("5ths", 5);  // param 4
    harmonicMode2Combo.addItem("6ths", 6);  // param 5
    harmonicMode2Combo.addItem("7ths", 7);  // param 6
    styleCombo(harmonicMode2Combo);
    harmonicMode2Combo.setColour(juce::ComboBox::arrowColourId, YELLOW);
    addChildComponent(harmonicMode2Combo);
    harmonicMode2Combo.onChange = [this] { updateChordPreview(); };

    // ── Toggle row 1 ──────────────────────────────────────────
    discardCombo.addItem("Allow all notes (default)", 1);   // param 0
    discardCombo.addItem("Discard non-scale notes", 2);   // param 1
    discardCombo.addItem("Play nearest in-scale note", 3);  // param 2
    styleCombo(discardCombo);
    discardCombo.setColour(juce::ComboBox::arrowColourId, RED);
    addChildComponent(discardCombo);

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

    styleToggle(scaleTransposeRootBtn, juce::Colours::orchid);
    scaleTransposeRootBtn.setButtonText({});
    scaleTransposeRootLabel.setText("Scale transpose root", juce::dontSendNotification);
    scaleTransposeRootLabel.setFont(juce::Font(12.0f));
    scaleTransposeRootLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(scaleTransposeRootBtn);
    addChildComponent(scaleTransposeRootLabel);
    scaleTransposeRootBtn.onStateChange = [this]
        {
            scaleTransposeRootLabel.setColour(juce::Label::textColourId,
                scaleTransposeRootBtn.getToggleState() ? juce::Colours::orchid : TEXT_MAIN);
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
    scaleAnimModeCombo.addItem("Cluster", 6);   // param 5
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

    scaleAnimRateSlider.setRange(10.0, 2000.0, 1.0);
    scaleAnimRateSlider.setSkewFactorFromMidPoint(200.0);
    scaleAnimRateSlider.setTextValueSuffix(" ms");
    styleSliderWithBox(scaleAnimRateSlider, ANIM_COL);
    addChildComponent(scaleAnimRateSlider);

    // ── "Up to" variation toggle ──────────────────────────────
    scaleAnimUpToBtn.setButtonText("Allow for longer notes");
    scaleAnimUpToBtn.setClickingTogglesState(true);
    scaleAnimUpToBtn.setColour(juce::ToggleButton::tickColourId, ANIM_COL);
    scaleAnimUpToBtn.setColour(juce::ToggleButton::tickDisabledColourId, TEXT_DIM);
    addChildComponent(scaleAnimUpToBtn);
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

    // ── Scale Cluster widgets ─────────────────────────────────────────────────
    {
        auto initClusterLabel = [&](juce::Label& lbl, const juce::String& txt)
            {
                lbl.setText(txt, juce::dontSendNotification);
                lbl.setFont(labelFont);
                lbl.setJustificationType(juce::Justification::centredRight);
                lbl.setColour(juce::Label::textColourId, TEXT_DIM);
                addChildComponent(lbl);
            };
        auto initSepLabel = [&](juce::Label& lbl)
            {
                lbl.setText("to", juce::dontSendNotification);
                lbl.setFont(juce::Font(12.0f));
                lbl.setJustificationType(juce::Justification::centred);
                lbl.setColour(juce::Label::textColourId, TEXT_DIM);
                addChildComponent(lbl);
            };

        initClusterLabel(scaleClusterLowNotesLabel, "Low Notes:");
        initClusterLabel(scaleClusterHighNotesLabel, "High Notes:");
        initClusterLabel(scaleClusterLowOctLabel, "Oct:");
        initClusterLabel(scaleClusterHighOctLabel, "Oct:");
        initSepLabel(scaleClusterLowOctSepLabel);
        initSepLabel(scaleClusterHighOctSepLabel);

        populateClusterNotesCombo(scaleClusterLowNotesCombo);  styleCombo(scaleClusterLowNotesCombo);
        populateClusterNotesCombo(scaleClusterHighNotesCombo); styleCombo(scaleClusterHighNotesCombo);
        scaleClusterLowNotesCombo.setColour(juce::ComboBox::arrowColourId, TEAL);
        scaleClusterHighNotesCombo.setColour(juce::ComboBox::arrowColourId, TEAL);
        addChildComponent(scaleClusterLowNotesCombo);
        addChildComponent(scaleClusterHighNotesCombo);

        for (auto* cb : { &scaleClusterLowOctStartCombo,  &scaleClusterLowOctEndCombo,
                          &scaleClusterHighOctStartCombo, &scaleClusterHighOctEndCombo })
        {
            populateClusterOctaveCombo(*cb);
            styleCombo(*cb);
            cb->setColour(juce::ComboBox::arrowColourId, TEAL);
            addChildComponent(*cb);
        }

        scaleClusterVelLabel.setText("Vel from:", juce::dontSendNotification);
        scaleClusterVelLabel.setFont(labelFont);
        scaleClusterVelLabel.setJustificationType(juce::Justification::centredRight);
        scaleClusterVelLabel.setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(scaleClusterVelLabel);

        scaleClusterVelDashLabel.setText("Vel to:", juce::dontSendNotification);
        scaleClusterVelDashLabel.setFont(labelFont);
        scaleClusterVelDashLabel.setJustificationType(juce::Justification::centredRight);
        scaleClusterVelDashLabel.setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(scaleClusterVelDashLabel);

        populateCloudVelCombo(scaleClusterVelMinCombo); styleCombo(scaleClusterVelMinCombo);
        populateCloudVelCombo(scaleClusterVelMaxCombo); styleCombo(scaleClusterVelMaxCombo);
        scaleClusterVelMinCombo.setColour(juce::ComboBox::arrowColourId, TEAL);
        scaleClusterVelMaxCombo.setColour(juce::ComboBox::arrowColourId, TEAL);
        addChildComponent(scaleClusterVelMinCombo);
        addChildComponent(scaleClusterVelMaxCombo);

        scaleClusterDecayLabel.setText("Decay:", juce::dontSendNotification);
        scaleClusterDecayLabel.setFont(labelFont);
        scaleClusterDecayLabel.setJustificationType(juce::Justification::centredRight);
        scaleClusterDecayLabel.setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(scaleClusterDecayLabel);

        scaleClusterDecaySlider.setRange(20.0, 5000.0, 1.0);
        scaleClusterDecaySlider.setSkewFactorFromMidPoint(800.0);
        scaleClusterDecaySlider.setTextValueSuffix(" ms");
        styleSliderWithBox(scaleClusterDecaySlider, TEAL);
        addChildComponent(scaleClusterDecaySlider);
    }

    // ── Cloud mute toggle (single widget, shared by all three anim sections) ──
    // Positioned in row 1 next to the BPM sync label; visibility managed by
    // updateScaleAnimUI / updateManualAnimUI / updatePerNoteAnimUI.
    styleToggle(cloudMuteInputBtn, RED);
    cloudMuteInputBtn.setButtonText({});
    cloudMuteInputLabel.setText("Mute incoming", juce::dontSendNotification);
    cloudMuteInputLabel.setFont(smallFont);
    cloudMuteInputLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(cloudMuteInputBtn);
    addChildComponent(cloudMuteInputLabel);
    cloudMuteInputBtn.onStateChange = [this]
        {
            cloudMuteInputLabel.setColour(juce::Label::textColourId,
                cloudMuteInputBtn.getToggleState() ? RED : TEXT_MAIN);
        };
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

    manAnimRateSlider.setRange(10.0, 2000.0, 1.0);
    manAnimRateSlider.setSkewFactorFromMidPoint(200.0);
    manAnimRateSlider.setTextValueSuffix(" ms");
    styleSliderWithBox(manAnimRateSlider, ANIM_COL);
    addChildComponent(manAnimRateSlider);

    // ── "Up to" variation toggle ──────────────────────────────
    manAnimUpToBtn.setButtonText("Allow for longer notes");
    manAnimUpToBtn.setClickingTogglesState(true);
    manAnimUpToBtn.setColour(juce::ToggleButton::tickColourId, ANIM_COL);
    manAnimUpToBtn.setColour(juce::ToggleButton::tickDisabledColourId, TEXT_DIM);
    addChildComponent(manAnimUpToBtn);

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
void NoteAdderEditor::buildPerNoteTab()
{
    static const char* CHORD_NAMES[] = {
        "None", "Maj", "Maj7", "Maj9", "Min", "Min7", "Min9", "6/9",
        "Dom7", "Sus2", "Sus4", "Power", "Aug", "Dim", "Custom"
    };
    static const char* PC_LABELS[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };

    const juce::Font labelFont(12.0f, juce::Font::bold);  // initial only; updateFonts rescales
    const juce::Font dimFont(11.0f, 0);

    for (int pc = 0; pc < 12; ++pc)
    {
        // Label (note name)
        pnChordLabels[pc].setText(PC_LABELS[pc], juce::dontSendNotification);
        pnChordLabels[pc].setJustificationType(juce::Justification::centredRight);
        pnChordLabels[pc].setColour(juce::Label::textColourId,
            (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10) ? YELLOW : TEXT_MAIN);
        addChildComponent(pnChordLabels[pc]);

        // Chord type combo
        for (int k = 0; k < 15; ++k)
            pnChordCombos[pc].addItem(CHORD_NAMES[k], k + 1);
        styleCombo(pnChordCombos[pc]);
        pnChordCombos[pc].setColour(juce::ComboBox::arrowColourId, ACCENT);
        pnChordCombos[pc].setSelectedId(proc.pnChordTypeParams[pc]->get() + 1,
            juce::dontSendNotification);
        pnChordCombos[pc].onChange = [this, pc]
            {
                int id = pnChordCombos[pc].getSelectedId();
                if (id > 0) *proc.pnChordTypeParams[pc] = id - 1;
                updatePerNoteCustomVisibility(pc);
            };
        addChildComponent(pnChordCombos[pc]);

        // Custom offset hint label
        pnCustomLabel[pc].setText("offsets:", juce::dontSendNotification);
        pnCustomLabel[pc].setJustificationType(juce::Justification::centredRight);
        pnCustomLabel[pc].setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(pnCustomLabel[pc]);

        // Custom offset text editor
        {
            juce::ScopedReadLock lk(proc.pnCustomLock);
            pnCustomEditors[pc].setText(proc.pnCustomStrings[pc], false);
        }
        pnCustomEditors[pc].setColour(juce::TextEditor::backgroundColourId, PANEL);
        pnCustomEditors[pc].setColour(juce::TextEditor::textColourId, TEXT_MAIN);
        pnCustomEditors[pc].setColour(juce::TextEditor::outlineColourId, SEP);
        pnCustomEditors[pc].setColour(juce::TextEditor::focusedOutlineColourId, ACCENT);
        pnCustomEditors[pc].setJustification(juce::Justification::centredLeft);
        pnCustomEditors[pc].onReturnKey = [this, pc] { applyCustomString(pc); };
        pnCustomEditors[pc].onFocusLost = [this, pc] { applyCustomString(pc); };
        addChildComponent(pnCustomEditors[pc]);

        // Initialise visibility
        updatePerNoteCustomVisibility(pc);
    }

    // RND / RST buttons
    pnRandomizeBtn.setColour(juce::TextButton::buttonColourId, PURPLE.withAlpha(0.3f));
    pnRandomizeBtn.setColour(juce::TextButton::textColourOffId, PURPLE);
    pnRandomizeBtn.onClick = [this] { doPerNoteRandomize(); };
    addChildComponent(pnRandomizeBtn);

    pnResetBtn.setColour(juce::TextButton::buttonColourId, BG.withAlpha(0.3f));
    pnResetBtn.setColour(juce::TextButton::textColourOffId, TEXT_DIM);
    pnResetBtn.onClick = [this] { doPerNoteReset(); };
    addChildComponent(pnResetBtn);

    // Hint text below RND/RST
    pnHintLabel.setText(
        "The custom option lets you type in whole number semitone offsets using commas (e.g. -5, 3, 7).",
        juce::dontSendNotification);
    pnHintLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    pnHintLabel.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(pnHintLabel);
}

void NoteAdderEditor::applyCustomString(int pc)
{
    juce::String s = pnCustomEditors[pc].getText().trim();
    {
        juce::ScopedWriteLock lk(proc.pnCustomLock);
        proc.pnCustomStrings[pc] = s;
    }
}

//==============================================================================
void NoteAdderEditor::buildPerNoteAnimSection()
{
    const juce::Font labelFont(12.0f, juce::Font::bold);
    const juce::Font smallFont(12.0f);

    pnAnimModeLabel.setText("Animate:", juce::dontSendNotification);
    pnAnimModeLabel.setFont(labelFont);
    pnAnimModeLabel.setJustificationType(juce::Justification::centredRight);
    pnAnimModeLabel.setColour(juce::Label::textColourId, ANIM_COL.withAlpha(0.85f));
    addChildComponent(pnAnimModeLabel);

    pnAnimModeCombo.addItem("Off", 1);
    pnAnimModeCombo.addItem("Re-Voice", 2);
    pnAnimModeCombo.addItem("Cloud", 3);
    pnAnimModeCombo.addItem("Cluster", 4);   // param 3
    styleCombo(pnAnimModeCombo);
    pnAnimModeCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addChildComponent(pnAnimModeCombo);
    pnAnimModeCombo.onChange = [this]
        {
            int id = pnAnimModeCombo.getSelectedId();
            if (id > 0) *proc.pnAnimModeParam = id - 1;
            updatePerNoteAnimUI();
        };

    styleToggle(pnAnimSyncBtn, ANIM_COL);
    pnAnimSyncBtn.setButtonText({});
    addChildComponent(pnAnimSyncBtn);
    pnAnimSyncBtn.onStateChange = [this]
        {
            repopulatePerNoteRateCombo();
            pnAnimSyncLabel.setColour(juce::Label::textColourId,
                pnAnimSyncBtn.getToggleState() ? ANIM_COL : TEXT_MAIN);
        };

    pnAnimSyncLabel.setText("BPM sync", juce::dontSendNotification);
    pnAnimSyncLabel.setFont(smallFont);
    pnAnimSyncLabel.setColour(juce::Label::textColourId, TEXT_MAIN);
    addChildComponent(pnAnimSyncLabel);

    pnAnimRateLabel.setText("Rate:", juce::dontSendNotification);
    pnAnimRateLabel.setFont(labelFont);
    pnAnimRateLabel.setJustificationType(juce::Justification::centredRight);
    pnAnimRateLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(pnAnimRateLabel);

    styleCombo(pnAnimRateCombo);
    pnAnimRateCombo.setColour(juce::ComboBox::arrowColourId, ANIM_COL);
    addChildComponent(pnAnimRateCombo);
    pnAnimRateCombo.onChange = [this]
        {
            int id = pnAnimRateCombo.getSelectedId();
            if (id > 0) *proc.pnAnimRateParam = id - 1;
        };

    pnAnimRateSlider.setRange(10.0, 2000.0, 1.0);
    pnAnimRateSlider.setSkewFactorFromMidPoint(200.0);
    pnAnimRateSlider.setTextValueSuffix(" ms");
    styleSliderWithBox(pnAnimRateSlider, ANIM_COL);
    addChildComponent(pnAnimRateSlider);

    // ── "Up to" variation toggle ──────────────────────────────
    pnAnimUpToBtn.setButtonText("Allow for longer notes");
    pnAnimUpToBtn.setClickingTogglesState(true);
    pnAnimUpToBtn.setColour(juce::ToggleButton::tickColourId, ANIM_COL);
    pnAnimUpToBtn.setColour(juce::ToggleButton::tickDisabledColourId, TEXT_DIM);
    addChildComponent(pnAnimUpToBtn);

    // Cloud params (rows 3 & 4)
    pnCloudDensityLabel.setText("Density:", juce::dontSendNotification);
    pnCloudOctaveLabel.setText("Oct range:", juce::dontSendNotification);
    pnCloudDecayLabel.setText("Decay:", juce::dontSendNotification);
    for (auto* lbl : { &pnCloudDensityLabel, &pnCloudOctaveLabel, &pnCloudDecayLabel })
    {
        lbl->setFont(labelFont);
        lbl->setJustificationType(juce::Justification::centredRight);
        lbl->setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(*lbl);
    }

    populateCloudDensCombo(pnCloudDensityCombo);   styleCombo(pnCloudDensityCombo);
    pnCloudDensityCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(pnCloudDensityCombo);

    populateCloudOctaveCombo(pnCloudOctaveCombo);  styleCombo(pnCloudOctaveCombo);
    pnCloudOctaveCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(pnCloudOctaveCombo);

    pnCloudDecaySlider.setRange(20.0, 2000.0, 1.0);
    pnCloudDecaySlider.setSkewFactorFromMidPoint(300.0);
    pnCloudDecaySlider.setTextValueSuffix(" ms");
    styleSliderWithBox(pnCloudDecaySlider, ORANGE);
    addChildComponent(pnCloudDecaySlider);

    pnCloudVelLabel.setText("Velocity:", juce::dontSendNotification);
    pnCloudVelLabel.setFont(labelFont);
    pnCloudVelLabel.setJustificationType(juce::Justification::centredRight);
    pnCloudVelLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(pnCloudVelLabel);

    pnCloudVelDashLabel.setText("to", juce::dontSendNotification);
    pnCloudVelDashLabel.setFont(juce::Font(12.0f));
    pnCloudVelDashLabel.setJustificationType(juce::Justification::centred);
    pnCloudVelDashLabel.setColour(juce::Label::textColourId, TEXT_DIM);
    addChildComponent(pnCloudVelDashLabel);

    populateCloudVelCombo(pnCloudVelMinCombo); styleCombo(pnCloudVelMinCombo);
    pnCloudVelMinCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(pnCloudVelMinCombo);

    populateCloudVelCombo(pnCloudVelMaxCombo); styleCombo(pnCloudVelMaxCombo);
    pnCloudVelMaxCombo.setColour(juce::ComboBox::arrowColourId, ORANGE);
    addChildComponent(pnCloudVelMaxCombo);

    // ── PerNote Cluster widgets ───────────────────────────────────────────────
    {
        auto initClusterLabel = [&](juce::Label& lbl, const juce::String& txt)
            {
                lbl.setText(txt, juce::dontSendNotification);
                lbl.setFont(labelFont);
                lbl.setJustificationType(juce::Justification::centredRight);
                lbl.setColour(juce::Label::textColourId, TEXT_DIM);
                addChildComponent(lbl);
            };
        auto initSepLabel = [&](juce::Label& lbl)
            {
                lbl.setText("to", juce::dontSendNotification);
                lbl.setFont(juce::Font(12.0f));
                lbl.setJustificationType(juce::Justification::centred);
                lbl.setColour(juce::Label::textColourId, TEXT_DIM);
                addChildComponent(lbl);
            };

        initClusterLabel(pnClusterLowNotesLabel, "Low Notes:");
        initClusterLabel(pnClusterHighNotesLabel, "High Notes:");
        initClusterLabel(pnClusterLowOctLabel, "Oct:");
        initClusterLabel(pnClusterHighOctLabel, "Oct:");
        initSepLabel(pnClusterLowOctSepLabel);
        initSepLabel(pnClusterHighOctSepLabel);

        populateClusterNotesCombo(pnClusterLowNotesCombo);  styleCombo(pnClusterLowNotesCombo);
        populateClusterNotesCombo(pnClusterHighNotesCombo); styleCombo(pnClusterHighNotesCombo);
        pnClusterLowNotesCombo.setColour(juce::ComboBox::arrowColourId, TEAL);
        pnClusterHighNotesCombo.setColour(juce::ComboBox::arrowColourId, TEAL);
        addChildComponent(pnClusterLowNotesCombo);
        addChildComponent(pnClusterHighNotesCombo);

        for (auto* cb : { &pnClusterLowOctStartCombo,  &pnClusterLowOctEndCombo,
                          &pnClusterHighOctStartCombo, &pnClusterHighOctEndCombo })
        {
            populateClusterOctaveCombo(*cb);
            styleCombo(*cb);
            cb->setColour(juce::ComboBox::arrowColourId, TEAL);
            addChildComponent(*cb);
        }

        pnClusterVelLabel.setText("Vel from:", juce::dontSendNotification);
        pnClusterVelLabel.setFont(labelFont);
        pnClusterVelLabel.setJustificationType(juce::Justification::centredRight);
        pnClusterVelLabel.setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(pnClusterVelLabel);

        pnClusterVelDashLabel.setText("Vel to:", juce::dontSendNotification);
        pnClusterVelDashLabel.setFont(labelFont);
        pnClusterVelDashLabel.setJustificationType(juce::Justification::centredRight);
        pnClusterVelDashLabel.setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(pnClusterVelDashLabel);

        populateCloudVelCombo(pnClusterVelMinCombo); styleCombo(pnClusterVelMinCombo);
        populateCloudVelCombo(pnClusterVelMaxCombo); styleCombo(pnClusterVelMaxCombo);
        pnClusterVelMinCombo.setColour(juce::ComboBox::arrowColourId, TEAL);
        pnClusterVelMaxCombo.setColour(juce::ComboBox::arrowColourId, TEAL);
        addChildComponent(pnClusterVelMinCombo);
        addChildComponent(pnClusterVelMaxCombo);

        pnClusterDecayLabel.setText("Decay:", juce::dontSendNotification);
        pnClusterDecayLabel.setFont(labelFont);
        pnClusterDecayLabel.setJustificationType(juce::Justification::centredRight);
        pnClusterDecayLabel.setColour(juce::Label::textColourId, TEXT_DIM);
        addChildComponent(pnClusterDecayLabel);

        pnClusterDecaySlider.setRange(20.0, 5000.0, 1.0);
        pnClusterDecaySlider.setSkewFactorFromMidPoint(800.0);
        pnClusterDecaySlider.setTextValueSuffix(" ms");
        styleSliderWithBox(pnClusterDecaySlider, TEAL);
        addChildComponent(pnClusterDecaySlider);
    }
}


void NoteAdderEditor::createAttachments()
{
    auto& vts = proc.apvts;

    // Global controls
    humanizeTimeAtt = std::make_unique<SA>(vts, "humanizetime", humanizeTimeSlider);
    humanizeVelAtt = std::make_unique<SA>(vts, "humanizevel", humanizeVelSlider);
    globalTransposeAtt = std::make_unique<SA>(vts, "globaltranspose", globalTransposeSlider);
    scaleTranspAtt = std::make_unique<SA>(vts, "scaletranspose", scaleTranspSlider);
    addLengthAtt = std::make_unique<SA>(vts, "addlength", addLengthSlider);

    // Scale combos
    rootComboAtt = std::make_unique<CBA>(vts, "root", rootCombo);
    scaleComboAtt = std::make_unique<CBA>(vts, "scale", scaleCombo);
    countComboAtt = std::make_unique<CBA>(vts, "count", countCombo);
    harmonicModeComboAtt = std::make_unique<CBA>(vts, "harmonicmode", harmonicModeCombo);
    harmonicMode2ComboAtt = std::make_unique<CBA>(vts, "harmonicmode2", harmonicMode2Combo);

    // Scale toggles
    discardComboAtt = std::make_unique<CBA>(vts, "discard", discardCombo);
    discardInputAtt = std::make_unique<BA>(vts, "discardInput", discardInputBtn);
    randomSkipAtt = std::make_unique<BA>(vts, "randomskip", randomSkipBtn);
    lockBassAtt = std::make_unique<BA>(vts, "lockbass", lockBassBtn);
    scaleTransposeRootAtt = std::make_unique<BA>(vts, "scaletransposeroot", scaleTransposeRootBtn);

    // Inversion mode combo
    invModeComboAtt = std::make_unique<CBA>(vts, "invmode", invModeCombo);

    // ── Scale animation ───────────────────────────────────────
    scaleAnimSyncAtt = std::make_unique<BA>(vts, "animsync", scaleAnimSyncBtn);
    scaleAnimUpToAtt = std::make_unique<BA>(vts, "animuptovar", scaleAnimUpToBtn);
    scaleAnimRateSliderAtt = std::make_unique<SA>(vts, "animratefree", scaleAnimRateSlider);
    scaleWanderProbAtt = std::make_unique<CBA>(vts, "wanderprob", scaleWanderProbCombo);
    scaleWanderMaxAtt = std::make_unique<CBA>(vts, "wandermax", scaleWanderMaxCombo);
    scaleCloudDensAtt = std::make_unique<CBA>(vts, "clouddensity", scaleCloudDensityCombo);
    scaleCloudSpreadAtt = std::make_unique<CBA>(vts, "cloudspread", scaleCloudSpreadCombo);
    scaleCloudDecayAtt = std::make_unique<SA>(vts, "clouddecay", scaleCloudDecaySlider);
    scaleCloudVelMinAtt = std::make_unique<CBA>(vts, "cloudvelmin", scaleCloudVelMinCombo);
    scaleCloudVelMaxAtt = std::make_unique<CBA>(vts, "cloudvelmax", scaleCloudVelMaxCombo);

    // ── Scale Cluster attachments ─────────────────────────────────────────────
    scaleClusterLowNotesAtt = std::make_unique<CBA>(vts, "clustlownotes", scaleClusterLowNotesCombo);
    scaleClusterHighNotesAtt = std::make_unique<CBA>(vts, "clusthighnotes", scaleClusterHighNotesCombo);
    scaleClusterLowOctStartAtt = std::make_unique<CBA>(vts, "clustlowostrt", scaleClusterLowOctStartCombo);
    scaleClusterLowOctEndAtt = std::make_unique<CBA>(vts, "clustlowoend", scaleClusterLowOctEndCombo);
    scaleClusterHighOctStartAtt = std::make_unique<CBA>(vts, "clusthighostrt", scaleClusterHighOctStartCombo);
    scaleClusterHighOctEndAtt = std::make_unique<CBA>(vts, "clusthighoend", scaleClusterHighOctEndCombo);
    scaleClusterVelMinAtt = std::make_unique<CBA>(vts, "clustvelmin", scaleClusterVelMinCombo);
    scaleClusterVelMaxAtt = std::make_unique<CBA>(vts, "clustvelmax", scaleClusterVelMaxCombo);
    scaleClusterDecayAtt = std::make_unique<SA>(vts, "clustdecay", scaleClusterDecaySlider);

    // ── Manual animation ──────────────────────────────────────
    manAnimSyncAtt = std::make_unique<BA>(vts, "manimsync", manAnimSyncBtn);
    manAnimUpToAtt = std::make_unique<BA>(vts, "manimuptovar", manAnimUpToBtn);
    manAnimRateSliderAtt = std::make_unique<SA>(vts, "manimratefree", manAnimRateSlider);
    manCloudDensAtt = std::make_unique<CBA>(vts, "mclouddensity", manCloudDensityCombo);
    manCloudOctaveAtt = std::make_unique<CBA>(vts, "mcloudoctave", manCloudOctaveCombo);
    manCloudDecayAtt = std::make_unique<SA>(vts, "mclouddecay", manCloudDecaySlider);
    manCloudVelMinAtt = std::make_unique<CBA>(vts, "mcloudvelmin", manCloudVelMinCombo);
    manCloudVelMaxAtt = std::make_unique<CBA>(vts, "mcloudvelmax", manCloudVelMaxCombo);

    // ── PerNote animation ──────────────────────────────────────
    pnAnimSyncAtt = std::make_unique<BA>(vts, "pnanimsync", pnAnimSyncBtn);
    pnAnimUpToAtt = std::make_unique<BA>(vts, "pnanimuptovar", pnAnimUpToBtn);
    pnAnimRateSliderAtt = std::make_unique<SA>(vts, "pnanimratefree", pnAnimRateSlider);
    pnCloudDensAtt = std::make_unique<CBA>(vts, "pnclouddensity", pnCloudDensityCombo);
    pnCloudOctaveAtt = std::make_unique<CBA>(vts, "pncloudoctave", pnCloudOctaveCombo);
    pnCloudDecayAtt = std::make_unique<SA>(vts, "pnclouddecay", pnCloudDecaySlider);
    pnCloudVelMinAtt = std::make_unique<CBA>(vts, "pncloudvelmin", pnCloudVelMinCombo);
    pnCloudVelMaxAtt = std::make_unique<CBA>(vts, "pncloudvelmax", pnCloudVelMaxCombo);

    // ── PerNote Cluster attachments ───────────────────────────────────────────
    pnClusterLowNotesAtt = std::make_unique<CBA>(vts, "pnclustlownotes", pnClusterLowNotesCombo);
    pnClusterHighNotesAtt = std::make_unique<CBA>(vts, "pnclusthighnotes", pnClusterHighNotesCombo);
    pnClusterLowOctStartAtt = std::make_unique<CBA>(vts, "pnclustlowostrt", pnClusterLowOctStartCombo);
    pnClusterLowOctEndAtt = std::make_unique<CBA>(vts, "pnclustlowoend", pnClusterLowOctEndCombo);
    pnClusterHighOctStartAtt = std::make_unique<CBA>(vts, "pnclusthighostrt", pnClusterHighOctStartCombo);
    pnClusterHighOctEndAtt = std::make_unique<CBA>(vts, "pnclusthighoend", pnClusterHighOctEndCombo);
    pnClusterVelMinAtt = std::make_unique<CBA>(vts, "pnclustvelmin", pnClusterVelMinCombo);
    pnClusterVelMaxAtt = std::make_unique<CBA>(vts, "pnclustvelmax", pnClusterVelMaxCombo);
    pnClusterDecayAtt = std::make_unique<SA>(vts, "pnclustdecay", pnClusterDecaySlider);

    // ── Cloud mute (shared) ───────────────────────────────────────
    cloudMuteInputAtt = std::make_unique<BA>(vts, "cloudmuteinput", cloudMuteInputBtn);

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

void NoteAdderEditor::repopulatePerNoteRateCombo()
{
    const bool syncOn = proc.pnAnimSyncBPMParam->get();

    pnAnimRateCombo.setVisible(syncOn);
    pnAnimRateSlider.setVisible(!syncOn);

    if (syncOn)
    {
        pnAnimRateCombo.clear(juce::dontSendNotification);
        static const char* labels[NoteAdderProcessor::NUM_SUBDIVS] = {
            "1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"
        };
        for (int i = 0; i < NoteAdderProcessor::NUM_SUBDIVS; ++i)
            pnAnimRateCombo.addItem(labels[i], i + 1);
        pnAnimRateCombo.setSelectedId(proc.pnAnimRateParam->get() + 1,
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
    const int mode = proc.modeParam->get();
    const bool isScale = (mode == 1);
    const bool isPerNote = (mode == 2);
    const bool isManual = (mode == 0);

    auto styleBtn = [](juce::TextButton& btn, bool active, juce::Colour col)
        {
            btn.setColour(juce::TextButton::buttonColourId, active ? col : PANEL);
            btn.setColour(juce::TextButton::textColourOffId, active ? BG : TEXT_DIM);
            btn.setColour(juce::TextButton::textColourOnId, active ? BG : TEXT_DIM);
        };
    styleBtn(manualBtn, isManual, ACCENT);
    styleBtn(scaleBtn, isScale, GREEN);
    styleBtn(customBtn, isPerNote, PURPLE);

    // Header utility buttons (always visible)
    savePresetBtn.setVisible(true);
    loadPresetBtn.setVisible(true);

    // Manual-tab buttons now live inside the tab rows (not header)
    randomizeBtn.setVisible(isManual);
    resetManualBtn.setVisible(isManual);
    rndActivateBtn.setVisible(isManual);
    rndSemiBtn.setVisible(isManual);
    rndOctBtn.setVisible(isManual);

    // Manual-tab components
    colSemiHdr.setVisible(isManual);
    colOctHdr.setVisible(isManual);
    for (auto& r : rows)
    {
        r.enableBtn.setVisible(isManual);
        r.nameLabel.setVisible(isManual);
        r.semiCombo.setVisible(isManual);
        r.octCombo.setVisible(isManual);
    }

    // Scale-tab components
    auto setScaleVisible = [&](bool v)
        {
            rootLabel.setVisible(v);         rootCombo.setVisible(v);
            scaleLabel.setVisible(v);        scaleCombo.setVisible(v);
            countLabel.setVisible(v);        countCombo.setVisible(v);
            harmonicModeLabel.setVisible(v); harmonicModeCombo.setVisible(v);
            harmonicMode2AndLabel.setVisible(v); harmonicMode2Combo.setVisible(v);
            discardCombo.setVisible(v);
            discardInputBtn.setVisible(v);   discardInputLabel.setVisible(v);
            randomSkipBtn.setVisible(v);     randomSkipLabel.setVisible(v);
            lockBassBtn.setVisible(v);       lockBassLabel.setVisible(v);
            scaleTransposeRootBtn.setVisible(v); scaleTransposeRootLabel.setVisible(v);
            invLabel.setVisible(v);          invModeCombo.setVisible(v);
            previewLabel.setVisible(v);
            scaleTranspLabel.setVisible(v);  scaleTranspSlider.setVisible(v);
        };
    setScaleVisible(isScale);
    resetColorsBtn.setVisible(isScale);
    bwColorsBtn.setVisible(isScale);
    updateInversionUI();

    // PerNote tab components
    for (int pc = 0; pc < 12; ++pc)
    {
        pnChordLabels[pc].setVisible(isPerNote);
        pnChordCombos[pc].setVisible(isPerNote);
        if (isPerNote) updatePerNoteCustomVisibility(pc);
        else
        {
            pnCustomLabel[pc].setVisible(false);
            pnCustomEditors[pc].setVisible(false);
        }
    }
    pnRandomizeBtn.setVisible(isPerNote);
    pnResetBtn.setVisible(isPerNote);
    pnHintLabel.setVisible(isPerNote);

    // ── Animation sections: hide all then show the right one ──────────────────
    // Helper lambdas to hide entire anim sections quickly
    auto hideScaleAnim = [&]
        {
            scaleAnimModeLabel.setVisible(false); scaleAnimModeCombo.setVisible(false);
            scaleAnimSyncBtn.setVisible(false);   scaleAnimSyncLabel.setVisible(false);
            scaleAnimUpToBtn.setVisible(false);
            scaleAnimRateLabel.setVisible(false); scaleAnimRateCombo.setVisible(false);
            scaleAnimRateSlider.setVisible(false);
            scaleWanderProbLabel.setVisible(false); scaleWanderProbCombo.setVisible(false);
            scaleWanderMaxLabel.setVisible(false);  scaleWanderMaxCombo.setVisible(false);
            scaleCloudDensityLabel.setVisible(false); scaleCloudDensityCombo.setVisible(false);
            scaleCloudSpreadLabel.setVisible(false);  scaleCloudSpreadCombo.setVisible(false);
            scaleCloudDecayLabel.setVisible(false);   scaleCloudDecaySlider.setVisible(false);
            scaleCloudVelLabel.setVisible(false);     scaleCloudVelDashLabel.setVisible(false);
            scaleCloudVelMinCombo.setVisible(false);  scaleCloudVelMaxCombo.setVisible(false);
            cloudMuteInputBtn.setVisible(false);      cloudMuteInputLabel.setVisible(false);
            // Scale Cluster
            scaleClusterLowNotesLabel.setVisible(false);  scaleClusterLowNotesCombo.setVisible(false);
            scaleClusterHighNotesLabel.setVisible(false); scaleClusterHighNotesCombo.setVisible(false);
            scaleClusterLowOctLabel.setVisible(false);    scaleClusterLowOctStartCombo.setVisible(false);
            scaleClusterLowOctSepLabel.setVisible(false); scaleClusterLowOctEndCombo.setVisible(false);
            scaleClusterHighOctLabel.setVisible(false);   scaleClusterHighOctStartCombo.setVisible(false);
            scaleClusterHighOctSepLabel.setVisible(false); scaleClusterHighOctEndCombo.setVisible(false);
            scaleClusterVelLabel.setVisible(false);       scaleClusterVelDashLabel.setVisible(false);
            scaleClusterVelMinCombo.setVisible(false);    scaleClusterVelMaxCombo.setVisible(false);
            scaleClusterDecayLabel.setVisible(false);     scaleClusterDecaySlider.setVisible(false);
        };
    auto hideManualAnim = [&]
        {
            manAnimModeLabel.setVisible(false);   manAnimModeCombo.setVisible(false);
            manAnimSyncBtn.setVisible(false);     manAnimSyncLabel.setVisible(false);
            manAnimUpToBtn.setVisible(false);
            manAnimRateLabel.setVisible(false);   manAnimRateCombo.setVisible(false);
            manAnimRateSlider.setVisible(false);
            manCloudDensityLabel.setVisible(false); manCloudDensityCombo.setVisible(false);
            manCloudOctaveLabel.setVisible(false);  manCloudOctaveCombo.setVisible(false);
            manCloudDecayLabel.setVisible(false);   manCloudDecaySlider.setVisible(false);
            manCloudVelLabel.setVisible(false);     manCloudVelDashLabel.setVisible(false);
            manCloudVelMinCombo.setVisible(false);  manCloudVelMaxCombo.setVisible(false);
            cloudMuteInputBtn.setVisible(false);    cloudMuteInputLabel.setVisible(false);
        };
    auto hidePnAnim = [&]
        {
            pnAnimModeLabel.setVisible(false);   pnAnimModeCombo.setVisible(false);
            pnAnimSyncBtn.setVisible(false);     pnAnimSyncLabel.setVisible(false);
            pnAnimUpToBtn.setVisible(false);
            pnAnimRateLabel.setVisible(false);   pnAnimRateCombo.setVisible(false);
            pnAnimRateSlider.setVisible(false);
            pnCloudDensityLabel.setVisible(false); pnCloudDensityCombo.setVisible(false);
            pnCloudOctaveLabel.setVisible(false);  pnCloudOctaveCombo.setVisible(false);
            pnCloudDecayLabel.setVisible(false);   pnCloudDecaySlider.setVisible(false);
            pnCloudVelLabel.setVisible(false);     pnCloudVelDashLabel.setVisible(false);
            pnCloudVelMinCombo.setVisible(false);  pnCloudVelMaxCombo.setVisible(false);
            cloudMuteInputBtn.setVisible(false);   cloudMuteInputLabel.setVisible(false);
            // PN Cluster
            pnClusterLowNotesLabel.setVisible(false);  pnClusterLowNotesCombo.setVisible(false);
            pnClusterHighNotesLabel.setVisible(false); pnClusterHighNotesCombo.setVisible(false);
            pnClusterLowOctLabel.setVisible(false);    pnClusterLowOctStartCombo.setVisible(false);
            pnClusterLowOctSepLabel.setVisible(false); pnClusterLowOctEndCombo.setVisible(false);
            pnClusterHighOctLabel.setVisible(false);   pnClusterHighOctStartCombo.setVisible(false);
            pnClusterHighOctSepLabel.setVisible(false); pnClusterHighOctEndCombo.setVisible(false);
            pnClusterVelLabel.setVisible(false);       pnClusterVelDashLabel.setVisible(false);
            pnClusterVelMinCombo.setVisible(false);    pnClusterVelMaxCombo.setVisible(false);
            pnClusterDecayLabel.setVisible(false);     pnClusterDecaySlider.setVisible(false);
        };

    hideScaleAnim();
    hideManualAnim();
    hidePnAnim();

    if (isScale)
    {
        scaleAnimModeLabel.setVisible(true); scaleAnimModeCombo.setVisible(true);
        scaleAnimSyncBtn.setVisible(true);   scaleAnimSyncLabel.setVisible(true);
        scaleAnimUpToBtn.setVisible(true);
        scaleAnimRateLabel.setVisible(true);
    }
    else if (isPerNote)
    {
        pnAnimModeLabel.setVisible(true); pnAnimModeCombo.setVisible(true);
        pnAnimSyncBtn.setVisible(true);   pnAnimSyncLabel.setVisible(true);
        pnAnimUpToBtn.setVisible(true);
        pnAnimRateLabel.setVisible(true);
    }
    else // manual
    {
        manAnimModeLabel.setVisible(true); manAnimModeCombo.setVisible(true);
        manAnimSyncBtn.setVisible(true);   manAnimSyncLabel.setVisible(true);
        manAnimUpToBtn.setVisible(true);
        manAnimRateLabel.setVisible(true);
    }

    // Sub-widgets of the active section managed by updateAnimUI
    updateAnimUI();

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
    const int mode = proc.modeParam->get();
    if (mode == 1) updateScaleAnimUI();
    else if (mode == 2) updatePerNoteAnimUI();
    else                updateManualAnimUI();
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
    scaleAnimUpToBtn.setEnabled(animOn);

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

    const bool isCluster = animOn && (animMode == 5);
    scaleClusterLowNotesLabel.setVisible(isCluster);
    scaleClusterLowNotesCombo.setVisible(isCluster);
    scaleClusterHighNotesLabel.setVisible(isCluster);
    scaleClusterHighNotesCombo.setVisible(isCluster);
    scaleClusterLowOctLabel.setVisible(isCluster);
    scaleClusterLowOctStartCombo.setVisible(isCluster);
    scaleClusterLowOctSepLabel.setVisible(isCluster);
    scaleClusterLowOctEndCombo.setVisible(isCluster);
    scaleClusterHighOctLabel.setVisible(isCluster);
    scaleClusterHighOctStartCombo.setVisible(isCluster);
    scaleClusterHighOctSepLabel.setVisible(isCluster);
    scaleClusterHighOctEndCombo.setVisible(isCluster);
    scaleClusterVelLabel.setVisible(isCluster);
    scaleClusterVelDashLabel.setVisible(isCluster);
    scaleClusterVelMinCombo.setVisible(isCluster);
    scaleClusterVelMaxCombo.setVisible(isCluster);
    scaleClusterDecayLabel.setVisible(isCluster);
    scaleClusterDecaySlider.setVisible(isCluster);

    // Cloud mute is shared and shown for Cloud or Cluster
    cloudMuteInputBtn.setVisible(isCloud || isCluster);
    cloudMuteInputLabel.setVisible(isCloud || isCluster);

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
    manAnimUpToBtn.setEnabled(animOn);

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
    cloudMuteInputBtn.setVisible(isCloud);
    cloudMuteInputLabel.setVisible(isCloud);

    repaint();
}

void NoteAdderEditor::updatePerNoteAnimUI()
{
    const int  animMode = proc.pnAnimModeParam->get();
    const bool animOn = (animMode != 0);

    pnAnimModeCombo.setSelectedId(animMode + 1, juce::dontSendNotification);

    pnAnimRateLabel.setEnabled(animOn);
    pnAnimRateCombo.setEnabled(animOn);
    pnAnimRateSlider.setEnabled(animOn);
    pnAnimSyncBtn.setEnabled(animOn);
    pnAnimSyncLabel.setEnabled(animOn);
    pnAnimUpToBtn.setEnabled(animOn);

    repopulatePerNoteRateCombo();

    const bool isCloud = animOn && (animMode == 2);
    pnCloudDensityLabel.setVisible(isCloud);
    pnCloudDensityCombo.setVisible(isCloud);
    pnCloudOctaveLabel.setVisible(isCloud);
    pnCloudOctaveCombo.setVisible(isCloud);
    pnCloudDecayLabel.setVisible(isCloud);
    pnCloudDecaySlider.setVisible(isCloud);
    pnCloudVelLabel.setVisible(isCloud);
    pnCloudVelDashLabel.setVisible(isCloud);
    pnCloudVelMinCombo.setVisible(isCloud);
    pnCloudVelMaxCombo.setVisible(isCloud);

    const bool isCluster = animOn && (animMode == 3);
    pnClusterLowNotesLabel.setVisible(isCluster);
    pnClusterLowNotesCombo.setVisible(isCluster);
    pnClusterHighNotesLabel.setVisible(isCluster);
    pnClusterHighNotesCombo.setVisible(isCluster);
    pnClusterLowOctLabel.setVisible(isCluster);
    pnClusterLowOctStartCombo.setVisible(isCluster);
    pnClusterLowOctSepLabel.setVisible(isCluster);
    pnClusterLowOctEndCombo.setVisible(isCluster);
    pnClusterHighOctLabel.setVisible(isCluster);
    pnClusterHighOctStartCombo.setVisible(isCluster);
    pnClusterHighOctSepLabel.setVisible(isCluster);
    pnClusterHighOctEndCombo.setVisible(isCluster);
    pnClusterVelLabel.setVisible(isCluster);
    pnClusterVelDashLabel.setVisible(isCluster);
    pnClusterVelMinCombo.setVisible(isCluster);
    pnClusterVelMaxCombo.setVisible(isCluster);
    pnClusterDecayLabel.setVisible(isCluster);
    pnClusterDecaySlider.setVisible(isCluster);

    // Cloud mute is shared and shown for Cloud or Cluster
    cloudMuteInputBtn.setVisible(isCloud || isCluster);
    cloudMuteInputLabel.setVisible(isCloud || isCluster);

    repaint();
}

void NoteAdderEditor::updatePerNoteCustomVisibility(int pc)
{
    const bool isCustom = (proc.pnChordTypeParams[pc]->get() == 14);
    pnCustomLabel[pc].setVisible(isCustom);
    pnCustomEditors[pc].setVisible(isCustom);
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

            // ── Global controls ──────────────────────────────────
            auto* globalEl = xml.createNewChildElement("Global");
            globalEl->setAttribute("humanizeTime", proc.humanizeTimeParam->get());
            globalEl->setAttribute("humanizeVel", proc.humanizeVelParam->get());
            globalEl->setAttribute("transpose", proc.globalTransposeParam->get());

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
            scaleEl->setAttribute("discard", proc.discardParam->get());
            scaleEl->setAttribute("discardInput", proc.discardInputParam->get() ? 1 : 0);
            scaleEl->setAttribute("randomSkip", proc.randomSkipParam->get() ? 1 : 0);
            scaleEl->setAttribute("lockBass", proc.lockBassParam->get() ? 1 : 0);
            scaleEl->setAttribute("scaleTransposeRoot", proc.scaleTransposeRootParam->get() ? 1 : 0);
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

            // ── PerNote chord types + custom offset strings ───────
            auto* perNoteEl = xml.createNewChildElement("PerNote");
            for (int i = 0; i < 12; ++i)
            {
                auto* pcEl = perNoteEl->createNewChildElement("PC");
                pcEl->setAttribute("index", i);
                pcEl->setAttribute("chordType", proc.pnChordTypeParams[i]->get());
                juce::ScopedReadLock lk(proc.pnCustomLock);
                pcEl->setAttribute("custom", proc.pnCustomStrings[i]);
            }

            // ── Animation settings – PerNote mode ────────────────
            auto* animPNEl = xml.createNewChildElement("AnimationPerNote");
            animPNEl->setAttribute("mode", proc.pnAnimModeParam->get());
            animPNEl->setAttribute("syncBPM", proc.pnAnimSyncBPMParam->get() ? 1 : 0);
            animPNEl->setAttribute("rate", proc.pnAnimRateParam->get());
            animPNEl->setAttribute("rateFree", proc.pnAnimRateFreeParam->get());
            animPNEl->setAttribute("cloudDensity", proc.pnCloudDensityParam->get());
            animPNEl->setAttribute("cloudOctave", proc.pnCloudOctaveParam->get());
            animPNEl->setAttribute("cloudVelMin", proc.pnCloudVelMinParam->get());
            animPNEl->setAttribute("cloudVelMax", proc.pnCloudVelMaxParam->get());
            animPNEl->setAttribute("cloudDecay", proc.pnCloudDecayParam->get());

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

            // ── Global controls ──────────────────────────────────
            if (auto* globalEl = xml->getChildByName("Global"))
            {
                *proc.humanizeTimeParam = globalEl->getIntAttribute("humanizeTime", 0);
                *proc.humanizeVelParam = globalEl->getIntAttribute("humanizeVel", 0);
                *proc.globalTransposeParam = globalEl->getIntAttribute("transpose", 0);

                humanizeTimeSlider.setValue(proc.humanizeTimeParam->get(),
                    juce::dontSendNotification);
                humanizeVelSlider.setValue(proc.humanizeVelParam->get(),
                    juce::dontSendNotification);
                globalTransposeSlider.setValue(proc.globalTransposeParam->get(),
                    juce::dontSendNotification);
            }

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
                *proc.discardParam = scaleEl->getIntAttribute("discard", 0);
                *proc.discardInputParam = scaleEl->getIntAttribute("discardInput", 0) != 0;
                *proc.randomSkipParam = scaleEl->getIntAttribute("randomSkip", 0) != 0;
                *proc.lockBassParam = scaleEl->getIntAttribute("lockBass", 0) != 0;
                *proc.scaleTransposeRootParam = scaleEl->getIntAttribute("scaleTransposeRoot", 0) != 0;
                *proc.inversionModeParam = scaleEl->getIntAttribute("invMode", 0);
                *proc.inversionPickerParam = scaleEl->getIntAttribute("invPicker", 0);

                // APVTS ComboBoxAttachments listen to parameter changes and will
                // update the combos, but set explicitly to be safe.
                rootCombo.setSelectedId(proc.rootNoteParam->get() + 1, juce::sendNotification);
                scaleCombo.setSelectedId(proc.scaleTypeParam->get() + 1, juce::sendNotification);
                countCombo.setSelectedId(proc.noteCountParam->get() + 1, juce::sendNotification);
                harmonicModeCombo.setSelectedId(proc.harmonicModeParam->get() + 1, juce::sendNotification);
                invModeCombo.setSelectedId(proc.inversionModeParam->get() + 1, juce::sendNotification);

                discardCombo.setSelectedId(proc.discardParam->get() + 1, juce::sendNotification);
                discardInputBtn.setToggleState(proc.discardInputParam->get(), juce::sendNotification);
                randomSkipBtn.setToggleState(proc.randomSkipParam->get(), juce::sendNotification);
                lockBassBtn.setToggleState(proc.lockBassParam->get(), juce::sendNotification);
                scaleTransposeRootBtn.setToggleState(proc.scaleTransposeRootParam->get(), juce::sendNotification);
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

            // ── PerNote chord types + custom strings ─────────────
            if (auto* perNoteEl = xml->getChildByName("PerNote"))
            {
                for (auto* pcEl : perNoteEl->getChildWithTagNameIterator("PC"))
                {
                    const int i = pcEl->getIntAttribute("index", -1);
                    if (i < 0 || i >= 12) continue;
                    *proc.pnChordTypeParams[i] = pcEl->getIntAttribute("chordType", 0);
                    pnChordCombos[i].setSelectedId(
                        proc.pnChordTypeParams[i]->get() + 1, juce::sendNotification);
                    {
                        juce::ScopedWriteLock lk(proc.pnCustomLock);
                        proc.pnCustomStrings[i] = pcEl->getStringAttribute("custom", "");
                    }
                    pnCustomEditors[i].setText(proc.pnCustomStrings[i], false);
                    updatePerNoteCustomVisibility(i);
                }
            }

            // ── Animation – PerNote mode ──────────────────────────
            loadAnimInto(xml->getChildByName("AnimationPerNote"),
                proc.pnAnimModeParam, proc.pnAnimSyncBPMParam,
                proc.pnAnimRateParam, proc.pnAnimRateFreeParam,
                nullptr, nullptr,
                proc.pnCloudDensityParam, nullptr,
                proc.pnCloudVelMinParam, proc.pnCloudVelMaxParam,
                proc.pnCloudDecayParam);
            if (auto* pnAnimEl = xml->getChildByName("AnimationPerNote"))
                *proc.pnCloudOctaveParam = pnAnimEl->getIntAttribute("cloudOctave", 1);

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

void NoteAdderEditor::doPerNoteRandomize()
{
    // Pick a random chord type (1-7: Maj…6/9, skipping None=0 and Custom=8)
    juce::Random r;
    for (int pc = 0; pc < 12; ++pc)
    {
        const int chordType = 1 + r.nextInt(13);   // 1..13 (excludes None=0 and Custom=14)
        *proc.pnChordTypeParams[pc] = chordType;
        pnChordCombos[pc].setSelectedId(chordType + 1, juce::sendNotification);
        updatePerNoteCustomVisibility(pc);
    }
}

void NoteAdderEditor::doPerNoteReset()
{
    for (int pc = 0; pc < 12; ++pc)
    {
        *proc.pnChordTypeParams[pc] = 0;   // None
        pnChordCombos[pc].setSelectedId(1, juce::sendNotification);   // id 1 = None
        updatePerNoteCustomVisibility(pc);
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
    cb.addItem("Neapolitan Maj.", 17);
    cb.addItem("Neapolitan Min.", 18);
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

void NoteAdderEditor::populateClusterNotesCombo(juce::ComboBox& cb)
{
    for (int i = 1; i <= 12; ++i)
        cb.addItem(juce::String(i), i);
}

void NoteAdderEditor::populateClusterOctaveCombo(juce::ComboBox& cb)
{
    for (int i = 0; i < 10; ++i)
        cb.addItem(juce::String(i), i + 1);
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

void NoteAdderEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (titleLabel.getBounds().contains(e.getPosition()))
    {
        // Nudge by 1 px first so JUCE always sees a genuine size change,
        // even if its internal bounds already say 560x592 (which can happen
        // after FL Studio's wrapper settings panel desyncs the window).
        if (getWidth() == 560 && getHeight() == 592)
            Component::setSize(559, 592);
        setSize(560, 592);
    }
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
    const int  modeVal = proc.modeParam->get();
    const bool isScale = (modeVal == 1);
    const bool isPerNote = (modeVal == 2);

    juce::Colour tabAccent = isScale ? GREEN : (isPerNote ? PURPLE : ACCENT);

    g.setColour(tabAccent.withAlpha(0.03f));
    g.fillRect(Layout::PIANO_W, L.tabY, W, L.tabH);

    // Subtle tab active indicator on the top edge
    g.setColour(tabAccent.withAlpha(0.35f));
    g.fillRect(Layout::PIANO_W, L.tabY, W, 2);

    if (!isScale && !isPerNote)
    {
        // Manual: stripe the rows
        for (int i = 0; i < NoteAdderProcessor::NUM_ROWS; ++i)
        {
            g.setColour((i % 2 == 0 ? PANEL : PANEL_ALT).withAlpha(0.75f));
            g.fillRect(Layout::PIANO_W, L.manualRowsY + i * L.manualRowH, W, L.manualRowH);
        }
    }
    else if (isScale)
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
        const int curAnim = isScale ? proc.animModeParam->get()
            : (isPerNote ? proc.pnAnimModeParam->get()
                : proc.manualAnimModeParam->get());
        g.setColour(curAnim != 0 ? ANIM_COL.withAlpha(0.9f) : TEXT_DIM.withAlpha(0.5f));
        g.drawText("ANIMATION", Layout::PIANO_W + 10, L.animY + 4, 120, 16,
            juce::Justification::centredLeft, false);

        // Hint text for modes that have no param sub-widgets on row 3
        if (isScale && (curAnim == 1 || curAnim == 3))
        {
            static const char* hints[] = {
                "", "Cycles through all inversions of the chord on each tick.",
                "", "Chord root walks up the scale one degree per tick.",
            };
            g.setFont(juce::Font("Arial", L.f(11.0f), juce::Font::italic));
            g.setColour(ANIM_COL.withAlpha(0.5f));
            g.drawText(hints[curAnim], Layout::PIANO_W + 12, L.animRow3Y + 6, W - 24, 22,
                juce::Justification::centredLeft, false);
        }
        else if ((!isScale) && curAnim == 1)
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
    if (isScale)
    {
        g.setColour(GREEN.withAlpha(0.5f));
        g.drawText("SCALE MODE", Layout::PIANO_W + 10, L.tabY + 4, 130, 14,
            juce::Justification::centredLeft, false);
    }
    else if (isPerNote)
    {
        g.setColour(PURPLE.withAlpha(0.5f));
        g.drawText("CUSTOM MODE", Layout::PIANO_W + 10, L.tabY + 4, 140, 14,
            juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour(ACCENT.withAlpha(0.5f));
        g.drawText("MANUAL MODE", Layout::PIANO_W + 10, L.tabY + 4, 130, 14,
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
    globalTransposeLabel.setFont(dimFont);
    scaleTranspLabel.setFont(dimFont);
    addLengthLabel.setFont(dimFont);

    // ── Manual tab ────────────────────────────────────────────
    colSemiHdr.setFont(dimFont);
    colOctHdr.setFont(dimFont);
    for (auto& r : rows)
        r.nameLabel.setFont(rowFont);

    // ── Scale tab ─────────────────────────────────────────────
    for (auto* lbl : { &rootLabel, &scaleLabel, &countLabel,
                       &harmonicModeLabel, &invLabel, &invPickerLabel })
        lbl->setFont(labelFont);

    discardInputLabel.setFont(smallFont);
    randomSkipLabel.setFont(smallFont);
    lockBassLabel.setFont(smallFont);
    scaleTransposeRootLabel.setFont(smallFont);
    previewLabel.setFont(previewFnt);

    // ── Scale animation ───────────────────────────────────────
    for (auto* lbl : { &scaleAnimModeLabel, &scaleAnimRateLabel,
                       &scaleWanderProbLabel, &scaleWanderMaxLabel,
                       &scaleCloudDensityLabel, &scaleCloudSpreadLabel,
                       &scaleCloudDecayLabel, &scaleCloudVelLabel,
                       &scaleClusterLowNotesLabel, &scaleClusterHighNotesLabel,
                       &scaleClusterLowOctLabel, &scaleClusterHighOctLabel,
                       &scaleClusterVelLabel, &scaleClusterDecayLabel })
        lbl->setFont(labelFont);
    scaleAnimSyncLabel.setFont(smallFont);
    scaleCloudVelDashLabel.setFont(smallFont);
    scaleClusterVelDashLabel.setFont(labelFont);
    scaleClusterLowOctSepLabel.setFont(smallFont);
    scaleClusterHighOctSepLabel.setFont(smallFont);

    // ── Manual animation ──────────────────────────────────────
    for (auto* lbl : { &manAnimModeLabel, &manAnimRateLabel,
                       &manCloudDensityLabel, &manCloudOctaveLabel,
                       &manCloudDecayLabel, &manCloudVelLabel })
        lbl->setFont(labelFont);
    manAnimSyncLabel.setFont(smallFont);
    manCloudVelDashLabel.setFont(smallFont);

    // ── PerNote animation ─────────────────────────────────────
    for (auto* lbl : { &pnAnimModeLabel, &pnAnimRateLabel,
                       &pnCloudDensityLabel, &pnCloudOctaveLabel,
                       &pnCloudDecayLabel, &pnCloudVelLabel,
                       &pnClusterLowNotesLabel, &pnClusterHighNotesLabel,
                       &pnClusterLowOctLabel, &pnClusterHighOctLabel,
                       &pnClusterVelLabel, &pnClusterDecayLabel })
        lbl->setFont(labelFont);
    pnAnimSyncLabel.setFont(smallFont);
    pnCloudVelDashLabel.setFont(smallFont);
    pnClusterVelDashLabel.setFont(labelFont);
    pnClusterLowOctSepLabel.setFont(smallFont);
    pnClusterHighOctSepLabel.setFont(smallFont);
    cloudMuteInputLabel.setFont(smallFont);

    // ── PerNote tab ───────────────────────────────────────────
    const juce::Font pnLabelFont(L.f(12.0f), juce::Font::bold);
    const juce::Font pnDimFont(L.f(11.0f), 0);
    const juce::Font pnEditorFont(L.f(12.0f), 0);
    const juce::Font pnHintFont(L.f(10.5f), 0);

    for (int pc = 0; pc < 12; ++pc)
    {
        pnChordLabels[pc].setFont(pnLabelFont);
        pnCustomLabel[pc].setFont(pnDimFont);
        pnCustomEditors[pc].setFont(pnEditorFont);
    }
    pnHintLabel.setFont(pnHintFont);
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

        // Three tab buttons right-aligned: CUSTOM | SCALE | MANUAL
        customBtn.setBounds(ox + W - margin - tabW, btnY, tabW, btnH);
        scaleBtn.setBounds(ox + W - margin - tabW * 2 - tabGap, btnY, tabW, btnH);
        manualBtn.setBounds(ox + W - margin - tabW * 3 - tabGap * 2, btnY, tabW, btnH);

        // Utility buttons to the left of the tab buttons: LOAD | SAVE
        const int utilW = juce::roundToInt(46 * L.sf), utilGap = juce::roundToInt(4 * L.sf);
        int utilX = ox + W - margin - tabW * 3 - tabGap * 2 - utilGap - utilW;
        loadPresetBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;
        savePresetBtn.setBounds(utilX, btnY, utilW, btnH); utilX -= utilW + utilGap;

        const int auditW = juce::roundToInt(42 * L.sf);
        auditBtn.setBounds(utilX, btnY, auditW, btnH);

        titleLabel.setBounds(ox + margin, btnY - juce::roundToInt(2 * L.sf),
            utilX - (ox + margin) - 8, btnH);
    }

    // ── Global control row 1: Humanize Time + Humanize Vel ──────
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

    // ── Global control row 2: Transpose + Add Length (same grid as Humanize row) ──
    {
        const int rowY = L.ctrlRow2Y;
        const int rowH = L.ctrlRow2H;
        const int cy = rowY + (rowH - 22) / 2;
        const int lblW = juce::roundToInt(74 * L.sf);   // mirrors Humanize label width
        const int sliderW = juce::roundToInt(155 * L.sf);  // mirrors Humanize slider width
        const int sliderH = juce::roundToInt(24 * L.sf);
        const int lhGap = juce::roundToInt(4 * L.sf);    // label → slider gap
        const int secGap = juce::roundToInt(20 * L.sf);   // slider → next label gap
        int x = ox + margin;

        globalTransposeLabel.setBounds(x, cy, lblW, juce::roundToInt(20 * L.sf)); x += lblW + lhGap;
        globalTransposeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
        globalTransposeSlider.setBounds(x, cy - 2, sliderW, sliderH); x += sliderW + secGap;

        addLengthLabel.setBounds(x, cy, lblW, juce::roundToInt(20 * L.sf)); x += lblW + lhGap;
        addLengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
        addLengthSlider.setBounds(x, cy - 2, sliderW, sliderH);
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
        // RND sits right of row 0's octave combo (where RST used to be)
        {
            const int row0Y = L.manualRowsY;
            const int cy0 = row0Y + (L.manualRowH - comboH) / 2;
            const int btnW = juce::roundToInt(52 * L.sf);
            randomizeBtn.setBounds(octX + octW + gap + 4, cy0, btnW, comboH);
        }
        // RST sits right of row 1's octave combo (one row below RND)
        {
            const int row1Y = L.manualRowsY + L.manualRowH;
            const int cy1 = row1Y + (L.manualRowH - comboH) / 2;
            const int rstW = juce::roundToInt(52 * L.sf);
            resetManualBtn.setBounds(octX + octW + gap + 4, cy1, rstW, comboH);
        }

        // RND Act / RND Sem / RND Oct below RST
        {
            const int btnW = juce::roundToInt(52 * L.sf);
            const int btnX = octX + octW + gap + 4;
            for (int b = 0; b < 3; ++b)
            {
                const int rowY = L.manualRowsY + (b + 2) * L.manualRowH;
                const int cy = rowY + (L.manualRowH - comboH) / 2;
                juce::TextButton* btns[] = { &rndActivateBtn, &rndSemiBtn, &rndOctBtn };
                btns[b]->setBounds(btnX, cy, btnW, comboH);
            }
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
            rootCombo.setBounds(x, cy, rCmbW + 5, comboH); x += rCmbW + g10;
            scaleLabel.setBounds(x, cy, sLblW, comboH); x += sLblW + g4;
            scaleCombo.setBounds(x, cy, sCmbW, comboH); x += sCmbW + g10;
            countLabel.setBounds(x, cy, cLblW, comboH); x += cLblW + g4;

            const int hLblW = juce::roundToInt(62 * L.sf);
            const int andW = juce::roundToInt(26 * L.sf);
            const int availableSpace = (W - margin * 2) - hLblW - andW - gap * 3;
            const int hCmbW = juce::roundToInt(availableSpace * 0.6f);
            const int hCmb2W = availableSpace - hCmbW;

            countCombo.setBounds(x, cy, hCmb2W, comboH);
        }

        // Row 2: Harmonic mode
        {
            const int cy = L.scaleRow2Y + (36 - comboH) / 2;
            const int hLblW = juce::roundToInt(62 * L.sf);
            const int andW = juce::roundToInt(26 * L.sf);

            const int availableSpace = (W - margin * 2) - hLblW - andW - gap * 3;
            const int hCmbW = juce::roundToInt(availableSpace * 0.6f);
            const int hCmb2W = availableSpace - hCmbW;

            int x = ox + margin;
            harmonicModeLabel.setBounds(x, cy, hLblW, comboH);   x += hLblW + gap;
            harmonicModeCombo.setBounds(x, cy, hCmbW, comboH);   x += hCmbW + gap;
            harmonicMode2AndLabel.setBounds(x, cy, andW, comboH); x += andW + gap;
            harmonicMode2Combo.setBounds(x, cy, hCmb2W, comboH);
        }

        // Row 3: Scale Transpose | allow note combobox
        {
            const int cy = L.scaleTranspRowY + (36 - 22) / 2;
            const int lblW = juce::roundToInt(74 * L.sf);
            const int sliderW = juce::roundToInt(155 * L.sf);
            const int slH = juce::roundToInt(24 * L.sf);
            const int boxW = juce::roundToInt(46 * L.sf);
            const int lhGap = juce::roundToInt(4 * L.sf);
            int x = ox + margin;

            scaleTranspLabel.setBounds(x, cy, lblW, juce::roundToInt(20 * L.sf)); x += lblW + lhGap;
            scaleTranspSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, boxW, 20);
            scaleTranspSlider.setBounds(x, cy - 2, sliderW, slH);

            const int discardX = x + sliderW + gap * 3;
            const int discardW = (ox + W - margin) - discardX;
            discardCombo.setBounds(discardX, cy, discardW, 22);
        }

        // Toggle rows
        {
            const int cy1 = L.scaleTog1Y + (30 - 22) / 2;
            const int cy2 = L.scaleTog2Y + (30 - 22) / 2;
            const int colW = (W - margin * 2) / 3;
            const int tLblW = colW - toggleW - 3 - gap;
            const int btnW = colW - gap;

            int x = ox + margin;

            discardInputBtn.setBounds(x, cy1, toggleW, 22);
            discardInputLabel.setBounds(x + toggleW + 3, cy1, tLblW, 22);

            x += colW;
            randomSkipBtn.setBounds(x, cy1, toggleW, 22);
            randomSkipLabel.setBounds(x + toggleW + 3, cy1, tLblW, 22);

            x += colW;
            bwColorsBtn.setBounds(x, cy1, btnW / 2, 22);

            x = ox + margin;
            lockBassBtn.setBounds(x, cy2, toggleW, 22);
            lockBassLabel.setBounds(x + toggleW + 3, cy2, tLblW, 22);

            x += colW;
            scaleTransposeRootBtn.setBounds(x, cy2, toggleW, 22);
            scaleTransposeRootLabel.setBounds(x + toggleW + 3, cy2, tLblW, 22);

            x += colW;
            resetColorsBtn.setBounds(x, cy2, btnW / 2, 22);
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

    // ── PerNote (CUSTOM) tab ──────────────────────────────────
    {
        // Two-column layout: naturals (7) on left, sharps (5) on right.
        // Each row: [label 28px] [combo ~160px] [custom label 52px] [custom editor rest]

        // Push rows down enough to clear the "CUSTOM MODE" header text at tabY+4
        const int pnTopOffset = juce::roundToInt(22 * L.sy);
        const int tabH = L.animY - L.tabY - pnTopOffset;
        const int rowH = tabH / 8;   // 7 natural rows + a bit of padding
        const int colHalf = W / 2;
        const int pnLblW = juce::roundToInt(24 * L.sf);
        const int pnCmbW = juce::roundToInt(120 * L.sf);
        const int pnHintW = juce::roundToInt(46 * L.sf);
        const int g3 = juce::roundToInt(3 * L.sf);
        const int pnRowStart = L.tabY + pnTopOffset;

        // Column 1: naturals
        for (int i = 0; i < 7; ++i)
        {
            const int pc = PN_NATURALS[i];
            const int rowY = pnRowStart + i * rowH + (rowH - comboH) / 2;
            int x = ox + margin;
            pnChordLabels[pc].setBounds(x, rowY, pnLblW, comboH);    x += pnLblW + g3;
            pnChordCombos[pc].setBounds(x, rowY, pnCmbW, comboH);    x += pnCmbW + g3;
            pnCustomLabel[pc].setBounds(x, rowY, pnHintW, comboH);   x += pnHintW + g3;
            const int edW = ox + colHalf - x - g3;
            pnCustomEditors[pc].setBounds(x, rowY, juce::jmax(40, edW), comboH);
        }

        // Column 2: sharps
        for (int i = 0; i < 5; ++i)
        {
            const int pc = PN_SHARPS[i];
            const int rowY = pnRowStart + i * rowH + (rowH - comboH) / 2;
            int x = ox + colHalf + margin;
            pnChordLabels[pc].setBounds(x, rowY, pnLblW, comboH);    x += pnLblW + g3;
            pnChordCombos[pc].setBounds(x, rowY, pnCmbW, comboH);    x += pnCmbW + g3;
            pnCustomLabel[pc].setBounds(x, rowY, pnHintW, comboH);   x += pnHintW + g3;
            const int edW = ox + W - margin - x;
            pnCustomEditors[pc].setBounds(x, rowY, juce::jmax(40, edW), comboH);
        }

        // RND / RST below the last sharp row – left and right edges aligned to the combo above
        {
            const int btnY = pnRowStart + 5 * rowH + (rowH - comboH) / 2;
            const int rndRstX = ox + colHalf + margin + pnLblW + g3;  // same left edge as pnChordCombos
            const int btnW = (pnCmbW - g3) / 2;
            pnRandomizeBtn.setBounds(rndRstX, btnY, btnW, comboH);
            pnResetBtn.setBounds(rndRstX + btnW + g3, btnY, pnCmbW - btnW - g3, comboH);
        }

        // Hint text below RND/RST (row index 6 in right column)
        {
            const int hintY = pnRowStart + 6 * rowH + (rowH - comboH) / 2;
            const int hintX = ox + colHalf + margin;
            const int hintW = W / 2 - margin * 2;
            pnHintLabel.setBounds(hintX, hintY, hintW, comboH * 2);
        }
    }
    // ── Animation rows – all three sets share identical screen positions.
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
        // The cloud-mute toggle is a single shared widget; we position it here once.
        // Manual and PerNote row-1 blocks land at the same pixel coords so it fits all three.
        {
            const int muteLblW = juce::roundToInt(100 * L.sf);
            const int muteGap = juce::roundToInt(16 * L.sf);
            int x = ox + margin;
            scaleAnimModeLabel.setBounds(x, cy1, lblW, comboH);     x += lblW + gap;
            scaleAnimModeCombo.setBounds(x, cy1, modeCmbW, comboH); x += modeCmbW + g20;
            scaleAnimSyncBtn.setBounds(x, cy1, syncTogW, syncTogW); x += syncTogW + gap;
            scaleAnimSyncLabel.setBounds(x, cy1, syncLblW, comboH); x += syncLblW + muteGap;
            cloudMuteInputBtn.setBounds(x, cy1, syncTogW, syncTogW); x += syncTogW + gap;
            cloudMuteInputLabel.setBounds(x, cy1, muteLblW, comboH);
        }
        // Manual anim row 1  (same positions)
        {
            int x = ox + margin;
            manAnimModeLabel.setBounds(x, cy1, lblW, comboH);     x += lblW + gap;
            manAnimModeCombo.setBounds(x, cy1, modeCmbW, comboH); x += modeCmbW + g20;
            manAnimSyncBtn.setBounds(x, cy1, syncTogW, syncTogW); x += syncTogW + gap;
            manAnimSyncLabel.setBounds(x, cy1, syncLblW, comboH);
            // cloudMuteInputBtn/Label already positioned by scale block above
        }
        // PerNote anim row 1  (same positions)
        {
            int x = ox + margin;
            pnAnimModeLabel.setBounds(x, cy1, lblW, comboH);     x += lblW + gap;
            pnAnimModeCombo.setBounds(x, cy1, modeCmbW, comboH); x += modeCmbW + g20;
            pnAnimSyncBtn.setBounds(x, cy1, syncTogW, syncTogW); x += syncTogW + gap;
            pnAnimSyncLabel.setBounds(x, cy1, syncLblW, comboH);
            // cloudMuteInputBtn/Label already positioned by scale block above
        }
    }

    {
        // Row 2: aligned to row 1 grid (mode + sync)
        const int cy2 = L.animRow2Y + (L.animRowH - comboH) / 2;

        const int lblW = juce::roundToInt(62 * L.sf);
        const int modeCmbW = juce::roundToInt(128 * L.sf);
        const int syncTogW = juce::roundToInt(22 * L.sf);
        const int syncLblW = juce::roundToInt(80 * L.sf);
        const int g20 = juce::roundToInt(20 * L.sf);
        const int varBtnW = juce::roundToInt(120 * L.sf);

        int baseX = ox + margin;
        int rateX = baseX + lblW + gap;                  // EXACTLY where animate combo sits
        int allowX = rateX + modeCmbW + g20;             // EXACTLY where BPM sync sits

        // Scale
        {
            scaleAnimRateLabel.setBounds(baseX, cy2, lblW, comboH);
            scaleAnimRateCombo.setBounds(rateX, cy2, modeCmbW, comboH);
            scaleAnimRateSlider.setBounds(rateX, cy2 - 1, modeCmbW, comboH + 2);

            scaleAnimUpToBtn.setBounds(allowX, cy2, varBtnW, comboH);
        }
        // Manual
        {
            manAnimRateLabel.setBounds(baseX, cy2, lblW, comboH);
            manAnimRateCombo.setBounds(rateX, cy2, modeCmbW, comboH);
            manAnimRateSlider.setBounds(rateX, cy2 - 1, modeCmbW, comboH + 2);

            manAnimUpToBtn.setBounds(allowX, cy2, varBtnW, comboH);
        }
        // PerNote
        {
            pnAnimRateLabel.setBounds(baseX, cy2, lblW, comboH);
            pnAnimRateCombo.setBounds(rateX, cy2, modeCmbW, comboH);
            pnAnimRateSlider.setBounds(rateX, cy2 - 1, modeCmbW, comboH + 2);

            pnAnimUpToBtn.setBounds(allowX, cy2, varBtnW, comboH);
        }

        // Cluster decay (reference position for cloud too)
        {
            const int dcLblW = juce::roundToInt(44 * L.sf);
            const int dcSlW = juce::roundToInt(92 * L.sf);

            const int decayX = allowX + varBtnW + juce::roundToInt(10 * L.sf);

            scaleClusterDecayLabel.setBounds(decayX, cy2, dcLblW, comboH);
            scaleClusterDecaySlider.setBounds(decayX + dcLblW + gap, cy2 - 1, dcSlW, comboH + 2);

            pnClusterDecayLabel.setBounds(decayX, cy2, dcLblW, comboH);
            pnClusterDecaySlider.setBounds(decayX + dcLblW + gap, cy2 - 1, dcSlW, comboH + 2);
        }
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
            const int lblW = juce::roundToInt(62 * L.sf);
            const int boxW = juce::roundToInt(110 * L.sf);
            const int g20 = juce::roundToInt(20 * L.sf);

            // shift decay UP by one anim row height
            const int cyDecay = cy3 - L.animRowH;

            int baseX = ox + margin;
            int col1 = baseX + lblW + gap;
            int col2 = col1 + boxW + g20;
            int col3 = col2 + boxW + g20;

            // density sits one row higher now
            scaleCloudDensityLabel.setBounds(baseX, cy3, lblW, comboH);
            scaleCloudDensityCombo.setBounds(col1, cy3, boxW, comboH);

            // rest stays on original row
            scaleCloudSpreadLabel.setBounds(col2, cy3, lblW, comboH);
            scaleCloudSpreadCombo.setBounds(col2 + lblW + gap, cy3, boxW, comboH);

            const int dcLblW = juce::roundToInt(44 * L.sf);
            const int dcSlW = juce::roundToInt(92 * L.sf);

            scaleCloudDecayLabel.setBounds(col3, cyDecay, dcLblW, comboH);
            scaleCloudDecaySlider.setBounds(col3 + dcLblW + gap, cyDecay - 1, dcSlW, comboH + 2);
        }

        // Manual – Cloud row 3
        {
            const int lblW = juce::roundToInt(62 * L.sf);
            const int boxW = juce::roundToInt(110 * L.sf);
            const int g20 = juce::roundToInt(20 * L.sf);

            // shift decay UP by one anim row height
            const int cyDecay = cy3 - L.animRowH;

            int baseX = ox + margin;
            int col1 = baseX + lblW + gap;
            int col2 = col1 + boxW + g20;
            int col3 = col2 + boxW + g20;

            // density sits one row higher now
            manCloudDensityLabel.setBounds(baseX, cy3, lblW, comboH);
            manCloudDensityCombo.setBounds(col1, cy3, boxW, comboH);

            // rest stays on original row (Using Octave instead of Spread)
            manCloudOctaveLabel.setBounds(col2, cy3, lblW, comboH);
            manCloudOctaveCombo.setBounds(col2 + lblW + gap, cy3, boxW, comboH);

            const int dcLblW = juce::roundToInt(44 * L.sf);
            const int dcSlW = juce::roundToInt(92 * L.sf);

            manCloudDecayLabel.setBounds(col3, cyDecay, dcLblW, comboH);
            manCloudDecaySlider.setBounds(col3 + dcLblW + gap, cyDecay - 1, dcSlW, comboH + 2);
        }

        // PerNote – Cloud row 3
        {
            const int lblW = juce::roundToInt(62 * L.sf);
            const int boxW = juce::roundToInt(110 * L.sf);
            const int g20 = juce::roundToInt(20 * L.sf);

            // shift decay UP by one anim row height
            const int cyDecay = cy3 - L.animRowH;

            int baseX = ox + margin;
            int col1 = baseX + lblW + gap;
            int col2 = col1 + boxW + g20;
            int col3 = col2 + boxW + g20;

            // density sits one row higher now
            pnCloudDensityLabel.setBounds(baseX, cy3, lblW, comboH);
            pnCloudDensityCombo.setBounds(col1, cy3, boxW, comboH);

            // rest stays on original row (Using Octave instead of Spread)
            pnCloudOctaveLabel.setBounds(col2, cy3, lblW, comboH);
            pnCloudOctaveCombo.setBounds(col2 + lblW + gap, cy3, boxW, comboH);

            const int dcLblW = juce::roundToInt(44 * L.sf);
            const int dcSlW = juce::roundToInt(92 * L.sf);

            pnCloudDecayLabel.setBounds(col3, cyDecay, dcLblW, comboH);
            pnCloudDecaySlider.setBounds(col3 + dcLblW + gap, cyDecay - 1, dcSlW, comboH + 2);
        }

        // Scale – Cluster row 3: Low notes + Low oct range + Vel from (min)
        {
            const int notesLblW = juce::roundToInt(58 * L.sf);
            const int notesCW = juce::roundToInt(38 * L.sf);
            const int octLblW = juce::roundToInt(28 * L.sf);
            const int octCW = juce::roundToInt(36 * L.sf);
            const int sepW = juce::roundToInt(18 * L.sf);
            const int secGap = juce::roundToInt(8 * L.sf);
            const int vLblW = juce::roundToInt(58 * L.sf);
            const int vCmbW = juce::roundToInt(96 * L.sf);
            int x = ox + margin;
            scaleClusterLowNotesLabel.setBounds(x, cy3, notesLblW, comboH); x += notesLblW + gap;
            scaleClusterLowNotesCombo.setBounds(x, cy3, notesCW, comboH);   x += notesCW + gap;
            scaleClusterLowOctLabel.setBounds(x, cy3, octLblW, comboH);     x += octLblW + gap;
            scaleClusterLowOctStartCombo.setBounds(x, cy3, octCW, comboH);  x += octCW;
            scaleClusterLowOctSepLabel.setBounds(x, cy3, sepW, comboH);     x += sepW;
            scaleClusterLowOctEndCombo.setBounds(x, cy3, octCW, comboH);    x += octCW + secGap;
            scaleClusterVelLabel.setBounds(x, cy3, vLblW, comboH);          x += vLblW + gap;
            scaleClusterVelMinCombo.setBounds(x, cy3, vCmbW, comboH);
        }

        // PerNote – Cluster row 3: Low notes + Low oct range + Vel from (min)
        {
            const int notesLblW = juce::roundToInt(58 * L.sf);
            const int notesCW = juce::roundToInt(38 * L.sf);
            const int octLblW = juce::roundToInt(28 * L.sf);
            const int octCW = juce::roundToInt(36 * L.sf);
            const int sepW = juce::roundToInt(18 * L.sf);
            const int secGap = juce::roundToInt(8 * L.sf);
            const int vLblW = juce::roundToInt(58 * L.sf);
            const int vCmbW = juce::roundToInt(96 * L.sf);
            int x = ox + margin;
            pnClusterLowNotesLabel.setBounds(x, cy3, notesLblW, comboH); x += notesLblW + gap;
            pnClusterLowNotesCombo.setBounds(x, cy3, notesCW, comboH);   x += notesCW + gap;
            pnClusterLowOctLabel.setBounds(x, cy3, octLblW, comboH);     x += octLblW + gap;
            pnClusterLowOctStartCombo.setBounds(x, cy3, octCW, comboH);  x += octCW;
            pnClusterLowOctSepLabel.setBounds(x, cy3, sepW, comboH);     x += sepW;
            pnClusterLowOctEndCombo.setBounds(x, cy3, octCW, comboH);    x += octCW + secGap;
            pnClusterVelLabel.setBounds(x, cy3, vLblW, comboH);          x += vLblW + gap;
            pnClusterVelMinCombo.setBounds(x, cy3, vCmbW, comboH);
        }
    }

    {
        // Row 4: Cloud velocity range + Cluster High row (High notes + High oct + Vel to)
        const int cy4 = L.animRow4Y + (L.animRowH - comboH) / 2;

        // Scale – Cloud vel (aligned with density/spread grid)
        {
            const int lblW = juce::roundToInt(62 * L.sf);
            const int boxW = juce::roundToInt(110 * L.sf);
            const int g20 = juce::roundToInt(20 * L.sf);

            int baseX = ox + margin;
            int col1 = baseX + lblW + gap;
            int col2 = col1 + boxW + g20;

            scaleCloudVelLabel.setBounds(baseX, cy4, lblW, comboH);
            scaleCloudVelMinCombo.setBounds(col1, cy4, boxW, comboH);

            scaleCloudVelDashLabel.setBounds(col2, cy4, lblW, comboH);
            scaleCloudVelMaxCombo.setBounds(col2 + lblW + gap, cy4, boxW, comboH);
        }
        // Manual – Cloud vel
        {
            const int lblW = juce::roundToInt(62 * L.sf);
            const int boxW = juce::roundToInt(110 * L.sf);
            const int g20 = juce::roundToInt(20 * L.sf);

            int baseX = ox + margin;
            int col1 = baseX + lblW + gap;
            int col2 = col1 + boxW + g20;

            manCloudVelLabel.setBounds(baseX, cy4, lblW, comboH);
            manCloudVelMinCombo.setBounds(col1, cy4, boxW, comboH);

            manCloudVelDashLabel.setBounds(col2, cy4, lblW, comboH);
            manCloudVelMaxCombo.setBounds(col2 + lblW + gap, cy4, boxW, comboH);
        }
        // PerNote – Cloud vel
        {
            const int lblW = juce::roundToInt(62 * L.sf);
            const int boxW = juce::roundToInt(110 * L.sf);
            const int g20 = juce::roundToInt(20 * L.sf);

            int baseX = ox + margin;
            int col1 = baseX + lblW + gap;
            int col2 = col1 + boxW + g20;

            pnCloudVelLabel.setBounds(baseX, cy4, lblW, comboH);
            pnCloudVelMinCombo.setBounds(col1, cy4, boxW, comboH);

            pnCloudVelDashLabel.setBounds(col2, cy4, lblW, comboH);
            pnCloudVelMaxCombo.setBounds(col2 + lblW + gap, cy4, boxW, comboH);
        }
        // Scale – Cluster row 4: High notes + High oct range + Vel to (max)
        {
            const int notesLblW = juce::roundToInt(58 * L.sf);
            const int notesCW = juce::roundToInt(38 * L.sf);
            const int octLblW = juce::roundToInt(28 * L.sf);
            const int octCW = juce::roundToInt(36 * L.sf);
            const int sepW = juce::roundToInt(18 * L.sf);
            const int secGap = juce::roundToInt(8 * L.sf);
            const int vToLblW = juce::roundToInt(58 * L.sf);
            const int vCmbW2 = juce::roundToInt(96 * L.sf);
            int x = ox + margin;
            scaleClusterHighNotesLabel.setBounds(x, cy4, notesLblW, comboH); x += notesLblW + gap;
            scaleClusterHighNotesCombo.setBounds(x, cy4, notesCW, comboH);   x += notesCW + gap;
            scaleClusterHighOctLabel.setBounds(x, cy4, octLblW, comboH);     x += octLblW + gap;
            scaleClusterHighOctStartCombo.setBounds(x, cy4, octCW, comboH);  x += octCW;
            scaleClusterHighOctSepLabel.setBounds(x, cy4, sepW, comboH);     x += sepW;
            scaleClusterHighOctEndCombo.setBounds(x, cy4, octCW, comboH);    x += octCW + secGap;
            scaleClusterVelDashLabel.setBounds(x, cy4, vToLblW, comboH);     x += vToLblW + gap;
            scaleClusterVelMaxCombo.setBounds(x, cy4, vCmbW2, comboH);
        }
        // PerNote – Cluster row 4: High notes + High oct range + Vel to (max)
        {
            const int notesLblW = juce::roundToInt(58 * L.sf);
            const int notesCW = juce::roundToInt(38 * L.sf);
            const int octLblW = juce::roundToInt(28 * L.sf);
            const int octCW = juce::roundToInt(36 * L.sf);
            const int sepW = juce::roundToInt(18 * L.sf);
            const int secGap = juce::roundToInt(8 * L.sf);
            const int vToLblW = juce::roundToInt(58 * L.sf);
            const int vCmbW2 = juce::roundToInt(96 * L.sf);
            int x = ox + margin;
            pnClusterHighNotesLabel.setBounds(x, cy4, notesLblW, comboH); x += notesLblW + gap;
            pnClusterHighNotesCombo.setBounds(x, cy4, notesCW, comboH);   x += notesCW + gap;
            pnClusterHighOctLabel.setBounds(x, cy4, octLblW, comboH);     x += octLblW + gap;
            pnClusterHighOctStartCombo.setBounds(x, cy4, octCW, comboH);  x += octCW;
            pnClusterHighOctSepLabel.setBounds(x, cy4, sepW, comboH);     x += sepW;
            pnClusterHighOctEndCombo.setBounds(x, cy4, octCW, comboH);    x += octCW + secGap;
            pnClusterVelDashLabel.setBounds(x, cy4, vToLblW, comboH);     x += vToLblW + gap;
            pnClusterVelMaxCombo.setBounds(x, cy4, vCmbW2, comboH);
        }
    }
}