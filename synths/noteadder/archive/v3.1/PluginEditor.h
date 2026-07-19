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

    // ── APVTS attachment aliases ───────────────────────────────
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // ── Header ────────────────────────────────────────────────
    juce::Label      titleLabel;
    juce::TextButton manualBtn{ "MANUAL" };
    juce::TextButton scaleBtn{ "SCALE" };
    juce::TextButton randomizeBtn{ "RND" };
    juce::TextButton savePresetBtn{ "SAVE" };
    juce::TextButton loadPresetBtn{ "LOAD" };

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
    juce::ComboBox scaleAnimRateCombo;           // manually managed

    juce::Label    scaleWanderProbLabel, scaleWanderMaxLabel;
    juce::ComboBox scaleWanderProbCombo, scaleWanderMaxCombo;
    std::unique_ptr<CBA> scaleWanderProbAtt, scaleWanderMaxAtt;

    juce::Label    scaleCloudDensityLabel, scaleCloudSpreadLabel, scaleCloudDecayLabel;
    juce::ComboBox scaleCloudDensityCombo, scaleCloudSpreadCombo, scaleCloudDecayCombo;
    std::unique_ptr<CBA> scaleCloudDensAtt, scaleCloudSpreadAtt, scaleCloudDecayAtt;

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
    juce::ComboBox manAnimRateCombo;             // manually managed

    juce::Label    manCloudDensityLabel, manCloudOctaveLabel, manCloudDecayLabel;
    juce::ComboBox manCloudDensityCombo, manCloudOctaveCombo, manCloudDecayCombo;
    std::unique_ptr<CBA> manCloudDensAtt, manCloudOctaveAtt, manCloudDecayAtt;

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
    static void populateCloudDecayCombo(juce::ComboBox&);
    static void populateCloudVelCombo(juce::ComboBox&);

    static int semiToId(int v) { return v + 13; }
    static int idToSemi(int id) { return id - 13; }
    static int octToId(int v) { return v + 8; }
    static int idToOct(int id) { return id - 8; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderEditor)
};