#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class NoteAdderEditor : public juce::AudioProcessorEditor
{
public:
    explicit NoteAdderEditor(NoteAdderProcessor&);
    ~NoteAdderEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    NoteAdderProcessor& proc;

    // ── Header ────────────────────────────────────────────────
    juce::Label      titleLabel;
    juce::TextButton randomizeBtn{ "RND" };
    juce::TextButton manualBtn{ "MANUAL" };
    juce::TextButton scaleBtn{ "SCALE" };

    // ── Manual section ────────────────────────────────────────
    juce::Label colSemiHdr, colOctHdr;

    struct ManualRow
    {
        juce::ToggleButton enableBtn;
        juce::Label        nameLabel;
        juce::ComboBox     semiCombo;
        juce::ComboBox     octCombo;
    };
    std::array<ManualRow, NoteAdderProcessor::NUM_ROWS> rows;

    // ── Scale section ─────────────────────────────────────────
    juce::Label    rootLabel, scaleLabel, countLabel;
    juce::ComboBox rootCombo, scaleCombo, countCombo;

    juce::ToggleButton discardBtn, discardInputBtn;
    juce::Label        discardLabel, discardInputLabel;

    juce::ToggleButton randomSkipBtn, lockBassBtn;
    juce::Label        randomSkipLabel, lockBassLabel;

    juce::Label    invLabel;
    juce::ComboBox invModeCombo;
    juce::ComboBox invPickerCombo;
    juce::Label    invPickerLabel;

    juce::Label previewLabel;

    // ── Animation section ─────────────────────────────────────
    // Row 1: mode + BPM sync
    juce::Label        animModeLabel;
    juce::ComboBox     animModeCombo;
    juce::ToggleButton animSyncBtn;
    juce::Label        animSyncLabel;

    // Row 2: rate (single combo repopulated by sync state)
    juce::Label    animRateLabel;
    juce::ComboBox animRateCombo;

    // Row 3: mode-specific params (wander OR cloud A)
    // Wander
    juce::Label    wanderProbLabel, wanderMaxLabel;
    juce::ComboBox wanderProbCombo, wanderMaxCombo;
    // Cloud
    juce::Label    cloudDensityLabel, cloudSpreadLabel, cloudDecayLabel;
    juce::ComboBox cloudDensityCombo, cloudSpreadCombo,  cloudDecayCombo;

    // Row 4: cloud velocity range (cloud mode only)
    juce::Label    cloudVelLabel, cloudVelDashLabel;
    juce::ComboBox cloudVelMinCombo, cloudVelMaxCombo;

    // ── Helpers ───────────────────────────────────────────────
    void buildManualRows();
    void buildScaleSection();
    void buildAnimSection();

    void syncAllFromParams();
    void syncAnimFromParams();
    void syncManualRow(int i);

    void updateModeUI();
    void updateRowEnabled(int i);
    void updateChordPreview();
    void updateInversionUI();
    void updateAnimUI();
    void repopulateRateCombo();

    void rebuildPickerCombo();
    void doManualRandomize();

    static void populateSemiCombo   (juce::ComboBox&);
    static void populateOctCombo    (juce::ComboBox&);
    static void populateRootCombo   (juce::ComboBox&);
    static void populateScaleCombo  (juce::ComboBox&);
    static void populateCountCombo  (juce::ComboBox&);

    // Animation combo populators
    static void populateAnimModeCombo   (juce::ComboBox&);
    static void populateWanderProbCombo (juce::ComboBox&);
    static void populateWanderMaxCombo  (juce::ComboBox&);
    static void populateCloudDensCombo  (juce::ComboBox&);
    static void populateCloudSpreadCombo(juce::ComboBox&);
    static void populateCloudDecayCombo (juce::ComboBox&);
    static void populateCloudVelCombo   (juce::ComboBox&);

    static int semiToId(int v) { return v + 13; }
    static int idToSemi(int id) { return id - 13; }
    static int octToId(int v) { return v + 8; }
    static int idToOct(int id) { return id - 8; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderEditor)
};
