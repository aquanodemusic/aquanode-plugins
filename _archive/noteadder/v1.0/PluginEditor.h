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

    // Toggle row 1: discard out-of-scale / discard input note
    juce::ToggleButton discardBtn, discardInputBtn;
    juce::Label        discardLabel, discardInputLabel;

    // Toggle row 2: random skip / lock bass
    juce::ToggleButton randomSkipBtn, lockBassBtn;
    juce::Label        randomSkipLabel, lockBassLabel;

    // Inversion row
    juce::Label    invLabel;
    juce::ComboBox invModeCombo;
    juce::ComboBox invPickerCombo;   // visible only in Picker mode
    juce::Label    invPickerLabel;   // "Inversion:" label for picker

    // Preview
    juce::Label previewLabel;

    // ── Helpers ───────────────────────────────────────────────
    void buildManualRows();
    void buildScaleSection();
    void syncAllFromParams();
    void syncManualRow(int i);
    void updateModeUI();
    void updateRowEnabled(int i);
    void updateChordPreview();
    void updateInversionUI();
    void rebuildPickerCombo();
    void doManualRandomize();

    static void populateSemiCombo(juce::ComboBox&);
    static void populateOctCombo(juce::ComboBox&);
    static void populateRootCombo(juce::ComboBox&);
    static void populateScaleCombo(juce::ComboBox&);
    static void populateCountCombo(juce::ComboBox&);

    static int semiToId(int v) { return v + 13; }
    static int idToSemi(int id) { return id - 13; }
    static int octToId(int v) { return v + 8; }
    static int idToOct(int id) { return id - 8; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderEditor)
};