#pragma once
//==============================================================================
//  AquaVibrioNoteLayerManager.h
//
//  Note Lock: from the patch you have loaded, only the notes you select -
//  one selection after another - differ.
//
//  How it works:
//
//  * Selecting notes opens a SELECTION. The base patch is parked to one
//    side and every knob on the panel now edits the selected notes: the
//    knobs, Randomise, factory presets, Load and Mutate all just work,
//    because they all write the APVTS. The selected notes play the knobs
//    live; every other note keeps playing the parked base patch (or its own
//    layer).
//
//  * "Select notes" finishes the selection. Only the knobs that were changed
//    are kept, as a numbered LAYER, and the panel goes back to the base
//    patch. The new layer takes the lowest free number; notes it takes over
//    leave their old layers, and a layer left with no notes disappears, which
//    frees its number again.
//
//  * Selecting notes that all belong to one layer starts from that layer's
//    sound, so it can be reworked; a mixed selection starts from the base.
//
//  * Loading a whole patch while notes are selected gives those notes that
//    patch (every knob, apart from the global ones). With nothing selected
//    it is a new patch: it brings its own layers, or none.
//
//  Layers live in the plugin's state tree (so the DAW saves them) and in the
//  preset files. The audio thread never sees any of this directly: every
//  change is compiled into a NoteLayerSnapshot and published to the engine.
//==============================================================================

#include <JuceHeader.h>
#include <bitset>
#include <map>
#include <set>
#include "AquaVibrioNoteLayers.h"
#include "AquaVibrioMutator.h"

namespace aquavibrio
{

class Engine;
namespace original { class Engine; }

class NoteLayerManager : public juce::ChangeBroadcaster,
                         private juce::Timer
{
public:
    NoteLayerManager (juce::AudioProcessorValueTreeState& state, Engine& engine);
    ~NoteLayerManager() override;

    // A second engine (the Original one) that is sent every layout too.
    void setMirror (original::Engine* e);

    using Notes = std::bitset<128>;

    struct Layer
    {
        int number { 1 };
        int uid { 0 };
        Notes notes;
        std::map<int, float> values;   // parameter index -> raw value: only what this layer changes
        bool full { false };           // filled from a whole patch: every knob is its own
    };

    //== what the panel shows ==================================================
    std::vector<Layer> getLayers() const;
    int  numberForNote (int note) const;           // 0 = the base patch
    bool isSelected (int note) const;
    Notes getSelection() const;
    bool hasSelection() const;                     // a selection is being edited
    bool selectionIsEdited() const;
    int  upcomingNumber() const;                   // what the selection will become
    bool isChangedInSelection (int parameterIndex) const;
    juce::String describeSelection() const;

    //== selecting =============================================================
    void setSelected (int note, bool selected);
    void setSelection (const Notes& notes);
    void selectLayer (int number);
    void finishSelection();                        // "Select notes"
    void cancelSelection();
    void resetToBase (const Notes& notes);         // these notes play the base patch again
    void resetSelectionToBase();
    void clearAll();

    //== whole patches =========================================================
    // `load` writes a whole patch into the APVTS. With notes selected it is
    // the selection's new patch; otherwise it is the new base patch and
    // `noteLayers` (as saved in a preset; void for none) replaces the layers.
    void loadWholePatch (const std::function<void()>& load, const juce::var& noteLayers);

    //== mutation ==============================================================
    void mutate (Mutator::Strength strength);      // the base patch, or the selection
    void randomAssign (Mutator::Strength strength);
    bool canStepBack() const;
    bool canStepForward() const;
    void stepBack();
    void stepForward();

    //== saving ================================================================
    // The state as it would be with the open selection finished: the base
    // patch's values, and the layers including the selection.
    std::vector<float> committedBaseValues() const;
    std::vector<Layer> committedLayers() const;

    juce::ValueTree toValueTree() const;
    void restoreFrom (const juce::ValueTree& tree);   // closes any selection, keeps the APVTS as it is
    juce::var toJson() const;
    static std::vector<Layer> layersFromJson (const juce::var& json);

    // Writes the committed view into a copy of the APVTS state, for the host.
    void writeCommittedState (juce::ValueTree& stateCopy) const;

    static const juce::Identifier treeType;

    // Periodic housekeeping, also callable directly (tests, offline renders):
    // frees old layouts and lets the engine grow and recycle effect chains.
    void service();

    juce::CriticalSection& getLock() { return lock; }

private:
    void timerCallback() override { service(); }

    struct Session
    {
        bool open { false };
        int uid { 0 };
        Notes notes;
        std::vector<float> stash;      // the base patch
        std::vector<float> start;      // the knobs when the selection began
        std::set<int> sourceKeys;      // a reworked layer's own knobs
        bool full { false };
        bool wholePatch { false };
    };

    struct Generation
    {
        std::vector<float> values;
        std::vector<Layer> layers;
    };

    std::vector<float> readLive() const;
    void writeLive (const std::vector<float>& target, bool skipGlobal = false);
    void openSession (const Notes& notes);
    void deriveStart();
    void closeSession (bool restoreBase);
    bool editedLocked() const;
    std::map<int, float> sessionOverrides (const std::vector<float>& live) const;
    static void removeNotes (std::vector<Layer>& list, const Notes& notes);
    static int lowestFreeNumber (const std::vector<Layer>& list);
    std::vector<Layer> layersWithSessionCommitted (const std::vector<float>& live) const;
    void sortLayers();
    void publish();

    // history of mutations: context 0 = the base patch, otherwise a selection
    void pushGeneration (int context, bool withLayers);
    void restoreGeneration (const Generation& g);
    int historyContext() const { return session.open ? session.uid : 0; }

    juce::AudioProcessorValueTreeState& apvts;
    Engine& engine;
    original::Engine* mirror { nullptr };   // the Original engine receives the same layouts
    mutable juce::CriticalSection lock;
    std::vector<juce::RangedAudioParameter*> params;   // by parameter table index
    std::vector<std::atomic<float>*> raws;

    std::vector<Layer> layers;
    Session session;
    int nextUid { 1 };

    std::vector<Generation> history;
    int historyPos { -1 };
    int historyOwner { -1 };

    std::vector<std::unique_ptr<NoteLayerSnapshot>> snapshots;
    juce::uint64 serialCounter { 0 };
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteLayerManager)
};

} // namespace aquavibrio
