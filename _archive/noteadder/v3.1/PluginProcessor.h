#pragma once
#include <JuceHeader.h>
#include <map>
#include <atomic>

class NoteAdderProcessor : public juce::AudioProcessor
{
public:
    static constexpr int NUM_ROWS = 7;
    static constexpr int NUM_SCALES = 16;
    static constexpr int NUM_SUBDIVS = 9;
    static constexpr int NUM_FREE_RATES = 12;

    // Rate / velocity tables – exposed so the editor can populate combos
    static const double SUBDIV_FACTORS[9];
    static const int    FREE_RATE_MS[12];
    static const int    VEL_TABLE[10];
    static const int    CLOUD_DECAY_MS[5];

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
    juce::AudioParameterInt* modeParam = nullptr;

    // ── Manual ────────────────────────────────────────────────
    juce::AudioParameterBool* enabledParams[NUM_ROWS] = {};
    juce::AudioParameterInt* semiParams[NUM_ROWS] = {};
    juce::AudioParameterInt* octaveParams[NUM_ROWS] = {};

    // ── Scale ─────────────────────────────────────────────────
    juce::AudioParameterInt* rootNoteParam = nullptr;
    juce::AudioParameterInt* scaleTypeParam = nullptr;
    juce::AudioParameterInt* noteCountParam = nullptr;
    juce::AudioParameterInt* harmonicModeParam = nullptr;
    juce::AudioParameterBool* discardParam = nullptr;
    juce::AudioParameterBool* discardInputParam = nullptr;
    juce::AudioParameterBool* randomSkipParam = nullptr;
    juce::AudioParameterBool* lockBassParam = nullptr;

    juce::AudioParameterInt* inversionModeParam = nullptr;
    juce::AudioParameterInt* inversionPickerParam = nullptr;

    // ── Animation (Scale mode) ────────────────────────────────
    juce::AudioParameterInt*  animModeParam     = nullptr;
    juce::AudioParameterBool* animSyncBPMParam  = nullptr;
    juce::AudioParameterInt*  animRateParam     = nullptr;
    juce::AudioParameterInt*  animRateFreeParam = nullptr;
    juce::AudioParameterInt*  wanderProbParam   = nullptr;
    juce::AudioParameterInt*  wanderMaxParam    = nullptr;
    juce::AudioParameterInt*  cloudDensityParam = nullptr;
    juce::AudioParameterInt*  cloudSpreadParam  = nullptr;
    juce::AudioParameterInt*  cloudVelMinParam  = nullptr;
    juce::AudioParameterInt*  cloudVelMaxParam  = nullptr;
    juce::AudioParameterInt*  cloudDecayParam   = nullptr;

    // ── Animation (Manual mode – independent set) ─────────────
    // Wander and Drift are scale-only.
    // manualAnimModeParam: 0=Off  1=ReVoice  2=Cloud
    juce::AudioParameterInt*  manualAnimModeParam     = nullptr;
    juce::AudioParameterBool* manualAnimSyncBPMParam  = nullptr;
    juce::AudioParameterInt*  manualAnimRateParam     = nullptr;
    juce::AudioParameterInt*  manualAnimRateFreeParam = nullptr;
    juce::AudioParameterInt*  manualCloudDensityParam = nullptr;
    juce::AudioParameterInt*  manualCloudOctaveParam  = nullptr;   // 0-3 = 0/±1/±2/±3 octaves
    juce::AudioParameterInt*  manualCloudVelMinParam  = nullptr;
    juce::AudioParameterInt*  manualCloudVelMaxParam  = nullptr;
    juce::AudioParameterInt*  manualCloudDecayParam   = nullptr;

    // ── Per-pitch-class colours (stored as packed 0xRRGGBB int) ──
    juce::AudioParameterInt* noteColorParams[12] = {};

    // Returns the current colour for pitch class pc (0=C ... 11=B).
    juce::Colour getNoteColour(int pc) const noexcept
    {
        jassert(pc >= 0 && pc < 12);
        const int packed = noteColorParams[pc]->get();
        return juce::Colour((juce::uint8)((packed >> 16) & 0xFF),
                            (juce::uint8)((packed >>  8) & 0xFF),
                            (juce::uint8)( packed        & 0xFF));
    }

    // ── Scale tables ──────────────────────────────────────────
    static const int  SCALE_INTERVALS[NUM_SCALES][7];
    static const int  SCALE_SIZES[NUM_SCALES];
    static const char* SCALE_NAMES[NUM_SCALES];

    // ── Piano-roll visualizer: active output notes ─────────────
    // Written exclusively from the audio thread; read by the GUI timer.
    // Two 64-bit words cover the full 0-127 MIDI note range.
    std::atomic<uint64_t> pianoRollNotesLow  { 0 };   // notes  0 – 63
    std::atomic<uint64_t> pianoRollNotesHigh { 0 };   // notes 64 – 127

    // ── GUI-click notes (written by GUI thread, read by audio) ──
    // Set a bit to trigger a note-on; clear it to trigger a note-off.
    std::atomic<uint64_t> guiNotesLow  { 0 };   // notes  0 – 63
    std::atomic<uint64_t> guiNotesHigh { 0 };   // notes 64 – 127

    // ── Audit mode (sine-wave preview of all active output notes) ──
    std::atomic<bool> auditEnabled { false };

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

    static constexpr int MAX_CLOUD_NOTES = 96;

    std::map<int, AnimNoteState> animStates;
    std::vector<CloudNote>       cloudNotes;

    std::array<std::array<std::vector<int>, 128>, 16> activeAddedNotes;
    std::array<std::array<bool, 128>, 16>             discardedNotes;

    juce::Random     rng;
    std::vector<int> lastChordNotes;
    int              cycleIndex = 0;
    double           currentSampleRate = 44100.0;
    int              prevAnimMode = 0;

    // Previous GUI note state (audio thread only – for delta detection)
    uint64_t prevGuiLow  = 0;
    uint64_t prevGuiHigh = 0;

    // Audit oscillator state (audio thread only)
    double auditPhases[128]      = {};
    double auditRampLevel[128]   = {};   // 0.0 → 1.0 ramp envelope per note
    bool   auditRampingDown[128] = {};   // true while note is fading out

    // ── Audio-thread helpers ──────────────────────────────────
    std::vector<int> computeAddedNotes(int note);
    bool             isNoteInScale(int note)          const;
    static void      rotateInversion(std::vector<int>&, int inv);

    int    nextScaleNoteAbove(int note)              const;
    int    nextScaleNoteBelow(int note)              const;
    int    noteAtScaleDegreeOffset(int baseNote, int deg) const;
    double computeTickSamples()                      const;

    void tickReVoice(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickWander(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickDrift(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickCloud(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);

    void processCloudNoteOffs(juce::MidiBuffer&, int numSamples);
    void killAnimForNote(int ch, int note, juce::MidiBuffer&, int samplePos);
    void killAllAnimation(juce::MidiBuffer&, int samplePos);

    // ── Piano-roll bitmask helpers (audio thread only) ────────
    void pianoRollNoteOn (int note) noexcept;
    void pianoRollNoteOff(int note) noexcept;
    void pianoRollAllOff ()         noexcept;
    void updatePianoRollFromBuffer(const juce::MidiBuffer&) noexcept;
    void injectGuiNotes(juce::MidiBuffer& input) noexcept;   // merges GUI events INTO input
    void renderAuditTones(juce::AudioBuffer<float>&, int numSamples) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderProcessor)
};
