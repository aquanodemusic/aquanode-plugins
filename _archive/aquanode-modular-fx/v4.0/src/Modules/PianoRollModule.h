#pragma once

#include "ModuleCore.h"

// Piano Roll - a looping note sequencer you draw into. Time runs left to
// right, pitch bottom to top. Its Midi Out goes into anything with a Midi
// In / Add Midi In (generators, the drums, a Midi Add to chord it up), and
// like the Arp it REPLACES the played keys for whatever it feeds.
//
// The whole roll lives in the module card: transport and tools on top, the
// grid below, where notes are drawn, moved, stretched and deleted and where
// you slide around. The card is wider than other modules and can be resized
// from the grip in its bottom-right corner. Up to kMaxBars bars, 4/4.
//
// Tempo: its own Tempo knob, or "Host" to lock to the DAW. Locking is always
// safe - when there is no host tempo (standalone exe, Android app, a host
// that reports none) it quietly keeps running on the Tempo knob and the UI
// says so. Locking follows the host's TEMPO only; play/pause stay yours.
//
// MIDI files: Import replaces the notes with every note of every track of a
// .mid file (tempo taken from the file); Export writes a type-1 .mid with
// one track. On Android both go through the Storage Access Framework the
// same way Record and the sample loader do (content:// documents).
//
// Threading: the editable note list belongs to the message thread. Every
// edit publishes an immutable sorted snapshot that the audio thread latches
// once per block, so drawing while it plays never blocks or tears.
struct PianoRollNote
{
    double start { 0.0 };     // beats from the start of the loop
    double length { 1.0 };    // beats
    int pitch { 60 };
    float velocity { 0.8f };  // 0..1
};

class PianoRollModule : public aquanode::SynthModule
{
public:
    // must match the descriptor's param order
    enum ParamIndex { pTempo = 0, pBars, pSync, pLoop, pPlaying, pGrid, pNewVelocity, pViewW, pViewH };

    static constexpr int kMaxBars = 256;
    static constexpr int kMaxNotes = 8192;
    static constexpr double kBeatsPerBar = 4.0;

    using NoteList = std::vector<PianoRollNote>;

    PianoRollModule();
    ~PianoRollModule() override;

    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }
    bool runsWithoutListeners() const override { return true; }   // playhead moves even unpatched

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;
    int midiDriverAdvanceEvents (double sr, aquanode::MidiDriverEvent* events, int maxEvents) override;

    //=== editing (message thread) =============================================
    const NoteList& getNotes() const { return notes; }
    void setNotes (NoteList newNotes);              // sorts, clamps and publishes
    void pushUndo();                                // call once before an edit gesture
    bool undo();
    bool canUndo() const { return ! undoStack.empty(); }

    int numBars() const;
    double loopLengthBeats() const { return numBars() * kBeatsPerBar; }
    double gridBeats() const;
    static const juce::StringArray& gridChoices();

    //=== transport (message thread) ===========================================
    bool isPlaying() const { return getParameter ("playing") > 0.5f; }
    void setPlaying (bool shouldPlay) { setParameter ("playing", shouldPlay ? 1.0f : 0.0f); }
    void rewind() { seek (0.0); }
    void seek (double beat) { seekRequest.store (juce::jmax (0.0, beat), std::memory_order_release); }

    double getPlayheadBeats() const { return uiPlayhead.load (std::memory_order_relaxed); }
    double getEffectiveBpm() const { return uiBpm.load (std::memory_order_relaxed); }
    bool wantsHostTempo() const { return getParameter ("sync") > 0.5f; }
    bool isHostTempoAvailable() const { return uiHostTempo.load (std::memory_order_relaxed); }

    // plays a short note right away, so placing a note lets you hear it
    void previewNote (int pitch, float velocity01);

    //=== MIDI files ===========================================================
    void chooseAndImportMidi();
    void chooseAndExportMidi();
    bool importMidi (juce::InputStream& in);        // replaces the notes
    bool exportMidi (juce::OutputStream& out) const;

    //=== UI / state ===========================================================
    void uiButtonClicked (const juce::String& paramId) override;
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    // the card holds the whole roll and can be resized from its corner grip
    static constexpr int kDefaultWidth = 520, kMinWidth = 380, kMaxWidth = 1800;
    static constexpr int kDefaultHeight = 290, kMinHeight = 200, kMaxHeight = 1200;
    int extraContentHeight() const override
    {
        return juce::jlimit (kMinHeight, kMaxHeight, (int) std::lround (getParameter ("viewH")));
    }
    int preferredModuleWidth() const override
    {
        return juce::jlimit (kMinWidth, kMaxWidth, (int) std::lround (getParameter ("viewW")));
    }

    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

    // a counter the UI polls to notice note changes made elsewhere (import,
    // patch load, undo from the other view)
    int getEditCounter() const { return editCounter.load (std::memory_order_relaxed); }

private:
    void publish();
    void releaseAllActive (aquanode::MidiDriverEvent* events, int& n, int maxEvents);
    size_t firstNoteAtOrAfter (double beat) const;

    //=== message thread =======================================================
    NoteList notes;
    std::vector<NoteList> undoStack;
    std::vector<std::shared_ptr<const NoteList>> retired;   // freed here, never on the audio thread
    std::unique_ptr<juce::FileChooser> chooser;
    std::atomic<int> editCounter { 0 };

    //=== shared ===============================================================
    mutable juce::SpinLock snapshotLock;
    std::shared_ptr<const NoteList> published;
    std::atomic<double> seekRequest { -1.0 };

    juce::AbstractFifo previewFifo { 32 };
    std::array<std::pair<int, float>, 32> previewData {};

    std::atomic<double> uiPlayhead { 0.0 };
    std::atomic<double> uiBpm { 120.0 };
    std::atomic<bool> uiHostTempo { false };

    //=== audio thread =========================================================
    std::shared_ptr<const NoteList> audioNotes;
    size_t nextIndex { 0 };
    double position { 0.0 };          // beats into the loop
    bool wasPlaying { false };
    bool listChanged { false };

    double activeEnd[128] {};         // beat the note ends; < 0 = not sounding
    int activeList[128] {};
    int numActive { 0 };

    int previewLeft[128] {};          // samples until a preview note lets go
    std::array<std::pair<int, float>, 32> pendingPreviews {};
    int numPendingPreviews { 0 };

};
