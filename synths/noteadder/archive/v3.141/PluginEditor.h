#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ── Piano-roll visualiser ─────────────────────────────────────────────────────
class PianoRollDisplay : public juce::Component,
    private juce::Timer
{
public:
    explicit PianoRollDisplay(NoteAdderProcessor& p) : proc(p)
    {
        startTimerHz(30);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity(true);
    }
    ~PianoRollDisplay() override { stopTimer(); releaseAllGuiNotes(); }
    void paint(juce::Graphics&) override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    int  noteAtPosition(int px, int py) const noexcept;
    void guiNoteOn(int note) noexcept;
    void guiNoteOff(int note) noexcept;
    void releaseAllGuiNotes() noexcept;

    NoteAdderProcessor& proc;
    int currentDragNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollDisplay)
};

// ─────────────────────────────────────────────────────────────────────────────

class NoteAdderEditor : public juce::AudioProcessorEditor
{
public:
    explicit NoteAdderEditor(NoteAdderProcessor&);
    ~NoteAdderEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    NoteAdderProcessor& proc;

    // ── Piano-roll strip ──────────────────────────────────────
    PianoRollDisplay pianoRoll;

    // ── Audit button ──────────────────────────────────────────
    juce::TextButton auditBtn{ "AUD" };

    // ── REC button ────────────────────────────────────────────
    juce::TextButton recBtn{ "REC" };
    juce::Timer* recBlinkTimer = nullptr;   // handled by inner class below

    // Collected events since REC was pressed (GUI thread only after stop)
    std::vector<NoteAdderProcessor::RecordedEvent> recordedEvents;
    double recBpm = 120.0;   // BPM snapshot taken at stop time

    // Blink timer drives the glow animation while recording
    struct RecBlinkTimer : public juce::Timer
    {
        NoteAdderEditor& ed;
        explicit RecBlinkTimer(NoteAdderEditor& e) : ed(e) {}
        void timerCallback() override
        {
            // Drain FIFO periodically to prevent overflow on long sessions
            if (ed.proc.isRecording.load(std::memory_order_relaxed))
            {
                auto drained = ed.proc.drainRecordedEvents();
                ed.recordedEvents.insert(ed.recordedEvents.end(),
                    drained.begin(), drained.end());
            }
            ed.repaint();
        }
    };
    std::unique_ptr<RecBlinkTimer> recBlinker;

    void startRecording();
    void stopRecordingAndSave();

    // ── APVTS attachment aliases ───────────────────────────────
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;

    // ── Header ────────────────────────────────────────────────
    juce::Label      titleLabel;
    juce::TextButton manualBtn{ "MANUAL" };
    juce::TextButton scaleBtn{ "SCALE" };
    juce::TextButton randomizeBtn{ "RND" };
    juce::TextButton savePresetBtn{ "SAVE" };
    juce::TextButton loadPresetBtn{ "LOAD" };

    // ── Global control row (below header, always visible) ─────
    juce::Label  humanizeTimeLabel, humanizeVelLabel;
    juce::Slider humanizeTimeSlider, humanizeVelSlider;
    std::unique_ptr<SA> humanizeTimeAtt, humanizeVelAtt;

    // ── Manual tab ────────────────────────────────────────────
    juce::Label colSemiHdr, colOctHdr;

    struct ManualRow
    {
        juce::ToggleButton enableBtn;
        juce::Label        nameLabel;
        juce::ComboBox     semiCombo;
        juce::ComboBox     octCombo;
    };
    std::array<ManualRow, NoteAdderProcessor::NUM_ROWS> rows;

    std::array<std::unique_ptr<BA>, NoteAdderProcessor::NUM_ROWS> enabledAtts;
    std::array<std::unique_ptr<CBA>, NoteAdderProcessor::NUM_ROWS> semiComboAtts;
    std::array<std::unique_ptr<CBA>, NoteAdderProcessor::NUM_ROWS> octComboAtts;

    // ── Scale tab ─────────────────────────────────────────────
    juce::Label    rootLabel, scaleLabel, countLabel, harmonicModeLabel;
    juce::ComboBox rootCombo, scaleCombo, countCombo, harmonicModeCombo;
    std::unique_ptr<CBA> rootComboAtt, scaleComboAtt, countComboAtt, harmonicModeComboAtt;

    juce::ToggleButton discardBtn, discardInputBtn;
    juce::Label        discardLabel, discardInputLabel;
    std::unique_ptr<BA> discardAtt, discardInputAtt;

    juce::ToggleButton randomSkipBtn, lockBassBtn;
    juce::Label        randomSkipLabel, lockBassLabel;
    std::unique_ptr<BA> randomSkipAtt, lockBassAtt;
    juce::TextButton   resetColorsBtn{ "RST CLR" };
    juce::TextButton   bwColorsBtn{ "BW CLR" };

    juce::TextButton   resetManualBtn{ "RST" };

    juce::Label    invLabel;
    juce::ComboBox invModeCombo;
    juce::ComboBox invPickerCombo;
    juce::Label    invPickerLabel;
    std::unique_ptr<CBA> invModeComboAtt;

    juce::Label previewLabel;

    // ════════════════════════════════════════════════════════════════
    // ── SCALE animation section  (visible only in Scale mode)      ──
    // ── Modes: 0=Off  1=ReVoice  2=Wander  3=Drift  4=Cloud        ──
    // ── Param IDs: animmode / animsync / animrate / animratefree    ──
    // ════════════════════════════════════════════════════════════════

    juce::Label        scaleAnimModeLabel;
    juce::ComboBox     scaleAnimModeCombo;       // manually managed (no CBA)
    juce::ToggleButton scaleAnimSyncBtn;
    juce::Label        scaleAnimSyncLabel;
    std::unique_ptr<BA>  scaleAnimSyncAtt;       // param: "animsync"

    juce::Label    scaleAnimRateLabel;
    juce::ComboBox scaleAnimRateCombo;           // BPM-sync mode: subdivision picker
    juce::Slider   scaleAnimRateSlider;          // free mode: ms slider with value box
    std::unique_ptr<SA>  scaleAnimRateSliderAtt; // attached to "animratefree"

    juce::Label    scaleWanderProbLabel, scaleWanderMaxLabel;
    juce::ComboBox scaleWanderProbCombo, scaleWanderMaxCombo;
    std::unique_ptr<CBA> scaleWanderProbAtt, scaleWanderMaxAtt;

    juce::Label    scaleCloudDensityLabel, scaleCloudSpreadLabel, scaleCloudDecayLabel;
    juce::ComboBox scaleCloudDensityCombo, scaleCloudSpreadCombo;
    juce::Slider   scaleCloudDecaySlider;        // ms slider replacing old combo
    std::unique_ptr<SA>  scaleCloudDecayAtt;
    std::unique_ptr<CBA> scaleCloudDensAtt, scaleCloudSpreadAtt;

    juce::Label    scaleCloudVelLabel, scaleCloudVelDashLabel;
    juce::ComboBox scaleCloudVelMinCombo, scaleCloudVelMaxCombo;
    std::unique_ptr<CBA> scaleCloudVelMinAtt, scaleCloudVelMaxAtt;

    // ════════════════════════════════════════════════════════════════
    // ── MANUAL animation section  (visible only in Manual mode)    ──
    // ── Modes: 0=Off  1=ReVoice  2=Cloud                           ──
    // ── Param IDs: manimmode / manimsync / manimrate / manimratefree ──
    // ════════════════════════════════════════════════════════════════

    juce::Label        manAnimModeLabel;
    juce::ComboBox     manAnimModeCombo;         // manually managed (no CBA)
    juce::ToggleButton manAnimSyncBtn;
    juce::Label        manAnimSyncLabel;
    std::unique_ptr<BA>  manAnimSyncAtt;         // param: "manimsync"

    juce::Label    manAnimRateLabel;
    juce::ComboBox manAnimRateCombo;             // BPM-sync mode
    juce::Slider   manAnimRateSlider;            // free mode ms slider
    std::unique_ptr<SA>  manAnimRateSliderAtt;   // attached to "manimratefree"

    juce::Label    manCloudDensityLabel, manCloudOctaveLabel, manCloudDecayLabel;
    juce::ComboBox manCloudDensityCombo, manCloudOctaveCombo;
    juce::Slider   manCloudDecaySlider;
    std::unique_ptr<SA>  manCloudDecayAtt;
    std::unique_ptr<CBA> manCloudDensAtt, manCloudOctaveAtt;

    juce::Label    manCloudVelLabel, manCloudVelDashLabel;
    juce::ComboBox manCloudVelMinCombo, manCloudVelMaxCombo;
    std::unique_ptr<CBA> manCloudVelMinAtt, manCloudVelMaxAtt;

    // ── Preset file chooser ───────────────────────────────────
    std::shared_ptr<juce::FileChooser> fileChooser;

    // ── Helpers ───────────────────────────────────────────────
    void buildManualTab();
    void buildScaleTab();
    void buildScaleAnimSection();
    void buildManualAnimSection();
    void createAttachments();

    void updateModeUI();
    void updateRowEnabled(int i);
    void updateChordPreview();
    void updateInversionUI();
    void updateAnimUI();          // dispatches to scale or manual version
    void updateScaleAnimUI();
    void updateManualAnimUI();
    void updateFonts();           // called from resized() to scale all text

    void repopulateScaleRateCombo();
    void repopulateManualRateCombo();

    void rebuildPickerCombo();
    void doManualRandomize();
    void saveManualPreset();
    void loadManualPreset();

    void drawScaleColorKey(juce::Graphics&) const;
    int  chipIndexAtPoint(int x, int y) const noexcept;
    void openNoteColourPicker(int pitchClass);

    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

    static void populateSemiCombo(juce::ComboBox&);
    static void populateOctCombo(juce::ComboBox&);
    static void populateRootCombo(juce::ComboBox&);
    static void populateScaleCombo(juce::ComboBox&);
    static void populateCountCombo(juce::ComboBox&);
    static void populateHarmonicCombo(juce::ComboBox&);
    static void populateWanderProbCombo(juce::ComboBox&);
    static void populateWanderMaxCombo(juce::ComboBox&);
    static void populateCloudDensCombo(juce::ComboBox&);
    static void populateCloudSpreadCombo(juce::ComboBox&);
    static void populateCloudOctaveCombo(juce::ComboBox&);
    static void populateCloudVelCombo(juce::ComboBox&);

    static int semiToId(int v) { return v + 13; }
    static int idToSemi(int id) { return id - 13; }
    static int octToId(int v) { return v + 8; }
    static int idToOct(int id) { return id - 8; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderEditor)
};