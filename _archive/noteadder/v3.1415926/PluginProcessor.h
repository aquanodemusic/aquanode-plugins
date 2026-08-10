#pragma once
#include <JuceHeader.h>
#include <map>
#include <atomic>

class NoteAdderProcessor : public juce::AudioProcessor
{
public:
    static constexpr int NUM_ROWS = 7;
    static constexpr int NUM_SCALES = 18;
    static constexpr int NUM_SUBDIVS = 9;

    // Rate / velocity tables – exposed so the editor can populate combos
    static const double SUBDIV_FACTORS[9];
    static const int    VEL_TABLE[10];

    NoteAdderProcessor();
    ~NoteAdderProcessor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()  const override { return true; }
    const juce::String getName() const override;
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ── APVTS ─────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Mode ──────────────────────────────────────────────────
    // 0 = Manual   1 = Scale   2 = PerNote (CUSTOM)
    juce::AudioParameterInt* modeParam = nullptr;

    // ── Global humanize controls (applied to added notes only) ───
    juce::AudioParameterInt* humanizeTimeParam = nullptr;  // 0–200 ms random time offset per added note
    juce::AudioParameterInt* humanizeVelParam = nullptr;  // 0–100 random velocity spread per added note

    // ── Global semitone transpose (applied after all MIDI logic) ─
    juce::AudioParameterInt* globalTransposeParam = nullptr;  // -12 to +12 semitones

    // ── Manual ────────────────────────────────────────────────
    juce::AudioParameterBool* enabledParams[NUM_ROWS] = {};
    juce::AudioParameterInt* semiParams[NUM_ROWS] = {};
    juce::AudioParameterInt* octaveParams[NUM_ROWS] = {};

    // ── Scale ─────────────────────────────────────────────────
    juce::AudioParameterInt* rootNoteParam = nullptr;
    juce::AudioParameterInt* scaleTypeParam = nullptr;
    juce::AudioParameterInt* noteCountParam = nullptr;
    juce::AudioParameterInt* harmonicModeParam = nullptr;
    juce::AudioParameterInt* harmonicMode2Param = nullptr;  // 0=None, 1=2nds…6=7ths second stacking layer
    // 0=Allow all  1=Discard non-scale  2=Play nearest in-scale note
    juce::AudioParameterInt*  discardParam = nullptr;
    juce::AudioParameterBool* discardInputParam = nullptr;
    juce::AudioParameterBool* randomSkipParam = nullptr;
    juce::AudioParameterBool* lockBassParam = nullptr;

    juce::AudioParameterInt* inversionModeParam = nullptr;
    juce::AudioParameterInt* inversionPickerParam = nullptr;

    // ── Animation (Scale mode) ────────────────────────────────
    juce::AudioParameterInt* animModeParam = nullptr;
    juce::AudioParameterBool* animSyncBPMParam = nullptr;
    juce::AudioParameterInt* animRateParam = nullptr;   // subdiv index 0–8
    juce::AudioParameterFloat* animRateFreeParam = nullptr;   // ms, 10–2000
    juce::AudioParameterBool* animUpToParam = nullptr;   // "up to" rate variation
    juce::AudioParameterInt* wanderProbParam = nullptr;
    juce::AudioParameterInt* wanderMaxParam = nullptr;
    juce::AudioParameterInt* cloudDensityParam = nullptr;
    juce::AudioParameterInt* cloudSpreadParam = nullptr;
    juce::AudioParameterInt* cloudVelMinParam = nullptr;
    juce::AudioParameterInt* cloudVelMaxParam = nullptr;
    juce::AudioParameterFloat* cloudDecayParam = nullptr;   // ms, 20–2000

    // ── Animation (Manual mode – independent set) ─────────────
    // Wander and Drift are scale-only.
    // manualAnimModeParam: 0=Off  1=ReVoice  2=Cloud
    juce::AudioParameterInt* manualAnimModeParam = nullptr;
    juce::AudioParameterBool* manualAnimSyncBPMParam = nullptr;
    juce::AudioParameterInt* manualAnimRateParam = nullptr;
    juce::AudioParameterFloat* manualAnimRateFreeParam = nullptr;   // ms, 10–2000
    juce::AudioParameterBool* manualAnimUpToParam = nullptr;   // "up to" rate variation
    juce::AudioParameterInt* manualCloudDensityParam = nullptr;
    juce::AudioParameterInt* manualCloudOctaveParam = nullptr;   // 0-3 = 0/±1/±2/±3 octaves
    juce::AudioParameterInt* manualCloudVelMinParam = nullptr;
    juce::AudioParameterInt* manualCloudVelMaxParam = nullptr;
    juce::AudioParameterFloat* manualCloudDecayParam = nullptr;   // ms, 20–2000

    // ── Per-Note (CUSTOM) mode ────────────────────────────────
    // Chord type per pitch class: 0=Maj 1=Maj7 2=Maj9 3=Min 4=Min7 5=Min9 6=69 7=Custom
    juce::AudioParameterInt* pnChordTypeParams[12] = {};

    // Custom semitone-offset strings (e.g. "-7,-3,5"), stored here for audio-thread access.
    // Updated by the GUI thread; read by the audio thread via pnCustomLock.
    juce::ReadWriteLock pnCustomLock;
    std::array<juce::String, 12> pnCustomStrings;

    // Parse a comma-separated offset string like "-7,-3,5" into a vector of ints.
    static std::vector<int> parseCustomOffsets(const juce::String& s);

    // Returns the semitone offsets (relative to note) for the given chord type.
    // For chordType==7 (Custom), pass the already-parsed offsets.
    static const int* getChordOffsets(int chordType, int& count) noexcept;

    // ── PerNote animation (independent from scale / manual) ──
    // Modes: 0=Off  1=ReVoice  2=Cloud
    juce::AudioParameterInt*   pnAnimModeParam     = nullptr;
    juce::AudioParameterBool*  pnAnimSyncBPMParam  = nullptr;
    juce::AudioParameterInt*   pnAnimRateParam     = nullptr;
    juce::AudioParameterFloat* pnAnimRateFreeParam = nullptr;
    juce::AudioParameterBool*  pnAnimUpToParam     = nullptr;   // "up to" rate variation
    juce::AudioParameterInt*   pnCloudDensityParam = nullptr;
    juce::AudioParameterInt*   pnCloudOctaveParam  = nullptr;
    juce::AudioParameterInt*   pnCloudVelMinParam  = nullptr;
    juce::AudioParameterInt*   pnCloudVelMaxParam  = nullptr;
    juce::AudioParameterFloat* pnCloudDecayParam   = nullptr;

    // ── Cloud mode: mute incoming/added notes, play cloud notes only ──
    juce::AudioParameterBool* cloudMuteInputParam = nullptr;

    juce::AudioParameterInt* noteColorParams[12] = {};

    // Returns the current colour for pitch class pc (0=C ... 11=B).
    juce::Colour getNoteColour(int pc) const noexcept
    {
        jassert(pc >= 0 && pc < 12);
        const int packed = noteColorParams[pc]->get();
        return juce::Colour((juce::uint8)((packed >> 16) & 0xFF),
            (juce::uint8)((packed >> 8) & 0xFF),
            (juce::uint8)(packed & 0xFF));
    }

    // ── Scale tables ──────────────────────────────────────────
    static const int  SCALE_INTERVALS[NUM_SCALES][7];
    static const int  SCALE_SIZES[NUM_SCALES];
    static const char* SCALE_NAMES[NUM_SCALES];

    // ── Piano-roll visualizer: active output notes ─────────────
    // Written exclusively from the audio thread; read by the GUI timer.
    // Two 64-bit words cover the full 0-127 MIDI note range.
    std::atomic<uint64_t> pianoRollNotesLow{ 0 };   // notes  0 – 63
    std::atomic<uint64_t> pianoRollNotesHigh{ 0 };   // notes 64 – 127

    // ── GUI-click notes (written by GUI thread, read by audio) ──
    // Set a bit to trigger a note-on; clear it to trigger a note-off.
    std::atomic<uint64_t> guiNotesLow{ 0 };   // notes  0 – 63
    std::atomic<uint64_t> guiNotesHigh{ 0 };   // notes 64 – 127

    // ── Audit mode (sine-wave preview of all active output notes) ──
    std::atomic<bool> auditEnabled{ false };

    // ── MIDI Recording ─────────────────────────────────────────
    // GUI sets isRecording; audio thread appends events.
    std::atomic<bool> isRecording{ false };
    double recStartTime = 0.0;   // reset by GUI before arming; accumulated by audio thread

    struct RecordedEvent
    {
        double timestampSeconds;
        juce::MidiMessage message;
    };

    // Called by the audio thread to append events while recording.
    // Lock-free: uses a juce::AbstractFifo + pre-allocated ring buffer.
    void recordMidiEvent(const juce::MidiMessage& msg, int samplePos) noexcept;

    // Called by the GUI thread to drain all recorded events.
    std::vector<RecordedEvent> drainRecordedEvents();

    // Current playback BPM – updated each processBlock from host transport.
    std::atomic<double> currentBpm{ 120.0 };

private:
    // ── Per-held-note animation state ─────────────────────────
    struct AnimNoteState
    {
        int channel = 0;
        int velocity = 100;
        int inputNote = 0;

        std::vector<int> baseNotes;
        std::vector<int> currentNotes;
        std::vector<int> wanderSteps;

        int    invStep = 0;
        int    driftDegreeShift = 0;
        double samplesUntilTick = 0.0;
    };

    // ── Cloud note with scheduled note-off ────────────────────
    struct CloudNote
    {
        int heldKey;
        int channel;
        int midiNote;
        int samplesRemaining;
    };

    // ── Humanize-time deferred note (fires in a future block) ─
    struct PendingNote
    {
        int midiChannel;   // 1-based
        int inputNote;     // originating input note (for cancellation on note-off)
        int addedNote;
        int velocity;
        int samplesRemaining;  // samples from start of next block until fire
    };

    static constexpr int MAX_CLOUD_NOTES = 96;

    std::map<int, AnimNoteState> animStates;
    std::vector<CloudNote>       cloudNotes;
    std::vector<PendingNote>     pendingHumanizedNotes;

    std::array<std::array<std::vector<int>, 128>, 16> activeAddedNotes;
    std::array<std::array<bool, 128>, 16>             discardedNotes;
    // Maps original input note → rerouted note for "play nearest" mode; -1 = no reroute.
    std::array<std::array<int, 128>, 16>              reroutedNoteMap;

    juce::Random     rng;
    std::vector<int> lastChordNotes;
    int              cycleIndex = 0;
    double           currentSampleRate = 44100.0;
    int              currentBlockSize = 512;
    int              prevAnimMode = 0;

    // ── Live retranspose tracking ─────────────────────────────
    // prevTranspose: the transpose value used in the previous block.
    // heldInputVelocity: velocity of each currently-held input note (per channel),
    //   or -1 when not held. Used to re-voice held notes on transpose change.
    int              prevTranspose = 0;
    std::array<std::array<int, 128>, 16> heldInputVelocity;

    // Previous GUI note state (audio thread only – for delta detection)
    uint64_t prevGuiLow = 0;
    uint64_t prevGuiHigh = 0;

    // Audit oscillator state (audio thread only)
    bool   auditWasEnabled = false;      // tracks previous enabled state for edge detection
    double auditPhases[128] = {};
    double auditRampLevel[128] = {};   // 0.0 → 1.0 ramp envelope per note
    bool   auditRampingDown[128] = {};   // true while note is fading out
    double auditVelocity[128] = {};    // 0.0 → 1.0 linear velocity scale per note

    // ── Audio-thread helpers ──────────────────────────────────
    std::vector<int> computeAddedNotes(int note);
    bool             isNoteInScale(int note)          const;
    static void      rotateInversion(std::vector<int>&, int inv);

    int    nextScaleNoteAbove(int note)              const;
    int    nextScaleNoteBelow(int note)              const;
    int    noteAtScaleDegreeOffset(int baseNote, int deg) const;
    int    nearestScaleNote(int note)                const;  // for "play nearest" mode
    double computeTickSamples()                      const;
    double computeVarTickSamples()                   noexcept; // randomised when "up to" active

    void tickReVoice(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickWander(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickDrift(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickCloud(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);

    void processCloudNoteOffs(juce::MidiBuffer&, int numSamples);
    void killAnimForNote(int ch, int note, juce::MidiBuffer&, int samplePos);
    void killAllAnimation(juce::MidiBuffer&, int samplePos);

    // ── MIDI recording ring buffer ────────────────────────────
    static constexpr int REC_FIFO_SIZE = 4096;
    juce::AbstractFifo recFifo{ REC_FIFO_SIZE };
    std::array<RecordedEvent, REC_FIFO_SIZE> recBuffer;

    // ── Piano-roll bitmask helpers (audio thread only) ────────
    void pianoRollNoteOn(int note, int velocity) noexcept;
    void pianoRollNoteOff(int note) noexcept;
    void pianoRollAllOff()         noexcept;
    void updatePianoRollFromBuffer(const juce::MidiBuffer&) noexcept;
    void injectGuiNotes(juce::MidiBuffer& input) noexcept;   // merges GUI events INTO input
    void renderAuditTones(juce::AudioBuffer<float>&, int numSamples) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderProcessor)
};