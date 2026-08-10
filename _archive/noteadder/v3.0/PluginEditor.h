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

    // ── APVTS attachment aliases ───────────────────────────────
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA  = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // ── Header (always visible) ───────────────────────────────
    juce::Label      titleLabel;
    juce::TextButton manualBtn  { "MANUAL" };
    juce::TextButton scaleBtn   { "SCALE"  };
    juce::TextButton randomizeBtn{ "RND"  };
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

    // APVTS attachments for manual rows
    std::array<std::unique_ptr<BA>,  NoteAdderProcessor::NUM_ROWS> enabledAtts;
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

    juce::Label    invLabel;
    juce::ComboBox invModeCombo;
    juce::ComboBox invPickerCombo;
    juce::Label    invPickerLabel;
    std::unique_ptr<CBA> invModeComboAtt;
    // invPickerCombo is dynamic – managed manually

    juce::Label previewLabel;

    // ── Animation section (always visible) ────────────────────
    juce::Label        animModeLabel;
    juce::ComboBox     animModeCombo;   // managed manually (repopulated per tab)
    juce::ToggleButton animSyncBtn;
    juce::Label        animSyncLabel;
    std::unique_ptr<BA> animSyncAtt;

    juce::Label    animRateLabel;
    juce::ComboBox animRateCombo;       // managed manually (switches between two params)

    // Wander params
    juce::Label    wanderProbLabel, wanderMaxLabel;
    juce::ComboBox wanderProbCombo, wanderMaxCombo;
    std::unique_ptr<CBA> wanderProbComboAtt, wanderMaxComboAtt;

    // Cloud params row 1
    juce::Label    cloudDensityLabel, cloudSpreadLabel, cloudDecayLabel;
    juce::ComboBox cloudDensityCombo, cloudSpreadCombo, cloudDecayCombo;
    std::unique_ptr<CBA> cloudDensComboAtt, cloudSpreadComboAtt, cloudDecayComboAtt;

    // Cloud params row 2 (velocity range)
    juce::Label    cloudVelLabel, cloudVelDashLabel;
    juce::ComboBox cloudVelMinCombo, cloudVelMaxCombo;
    std::unique_ptr<CBA> cloudVelMinComboAtt, cloudVelMaxComboAtt;

    // ── Preset file chooser (held to prevent early destruction) ──
    std::shared_ptr<juce::FileChooser> fileChooser;

    // ── Helpers ───────────────────────────────────────────────
    void buildManualTab();
    void buildScaleTab();
    void buildAnimSection();
    void createAttachments();

    void updateModeUI();
    void updateRowEnabled(int i);
    void updateChordPreview();
    void updateInversionUI();
    void updateAnimUI();
    void repopulateRateCombo();
    void repopulateAnimModeCombo();
    void rebuildPickerCombo();

    void doManualRandomize();
    void saveManualPreset();
    void loadManualPreset();

    static void populateSemiCombo      (juce::ComboBox&);
    static void populateOctCombo       (juce::ComboBox&);
    static void populateRootCombo      (juce::ComboBox&);
    static void populateScaleCombo     (juce::ComboBox&);
    static void populateCountCombo     (juce::ComboBox&);
    static void populateHarmonicCombo  (juce::ComboBox&);
    static void populateWanderProbCombo(juce::ComboBox&);
    static void populateWanderMaxCombo (juce::ComboBox&);
    static void populateCloudDensCombo (juce::ComboBox&);
    static void populateCloudSpreadCombo(juce::ComboBox&);
    static void populateCloudDecayCombo(juce::ComboBox&);
    static void populateCloudVelCombo  (juce::ComboBox&);

    static int semiToId(int v) { return v + 13; }
    static int idToSemi(int id){ return id - 13; }
    static int octToId (int v) { return v + 8;  }
    static int idToOct (int id){ return id - 8;  }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderEditor)
};
