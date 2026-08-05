#pragma once
#include <JuceHeader.h>
#include <map>

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
    // Single source of truth for all parameters; enables full DAW automation.
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Raw pointers for fast audio-thread access (fetched from apvts after construction)
    // ── Mode ──────────────────────────────────────────────────
    juce::AudioParameterInt* modeParam = nullptr;   // 0=Manual 1=Scale

    // ── Manual ────────────────────────────────────────────────
    juce::AudioParameterBool* enabledParams[NUM_ROWS] = {};
    juce::AudioParameterInt* semiParams[NUM_ROWS] = {};
    juce::AudioParameterInt* octaveParams[NUM_ROWS] = {};

    // ── Scale ─────────────────────────────────────────────────
    juce::AudioParameterInt* rootNoteParam = nullptr;
    juce::AudioParameterInt* scaleTypeParam = nullptr;
    juce::AudioParameterInt* noteCountParam = nullptr;
    // 0=Secundal  1=Tertian  2=Quartal  3=Quintal  4=Sextal  5=Septimal
    juce::AudioParameterInt* harmonicModeParam = nullptr;
    juce::AudioParameterBool* discardParam = nullptr;
    juce::AudioParameterBool* discardInputParam = nullptr;
    juce::AudioParameterBool* randomSkipParam = nullptr;
    juce::AudioParameterBool* lockBassParam = nullptr;

    // 0=Normal 1=Picker 2=VoiceLeader 3=Drop2 4=CycleUp 5=CycleDown 6=RandomCycle
    juce::AudioParameterInt* inversionModeParam = nullptr;
    juce::AudioParameterInt* inversionPickerParam = nullptr;

    // ── Animation ─────────────────────────────────────────────
    // 0=Off  1=ReVoice  2=Wander  3=Drift  4=Cloud
    juce::AudioParameterInt* animModeParam = nullptr;
    juce::AudioParameterBool* animSyncBPMParam = nullptr;
    juce::AudioParameterInt* animRateParam = nullptr;   // 0-8  subdiv index (BPM mode)
    juce::AudioParameterInt* animRateFreeParam = nullptr;   // 0-11 ms-table index (free mode)
    juce::AudioParameterInt* wanderProbParam = nullptr;   // 0-9  → (val+1)*10 %
    juce::AudioParameterInt* wanderMaxParam = nullptr;   // 0-6  → val+1 scale-degree steps
    juce::AudioParameterInt* cloudDensityParam = nullptr;   // 0-7  → val+1 notes per tick
    juce::AudioParameterInt* cloudSpreadParam = nullptr;   // 0-6  → val+1 degree spread
    juce::AudioParameterInt* cloudVelMinParam = nullptr;   // 0-9  VEL_TABLE index
    juce::AudioParameterInt* cloudVelMaxParam = nullptr;   // 0-9  VEL_TABLE index
    juce::AudioParameterInt* cloudDecayParam = nullptr;   // 0-4  CLOUD_DECAY_MS index

    // ── Scale tables ──────────────────────────────────────────
    static const int  SCALE_INTERVALS[NUM_SCALES][7];
    static const int  SCALE_SIZES[NUM_SCALES];
    static const char* SCALE_NAMES[NUM_SCALES];

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

    std::map<int, AnimNoteState> animStates;   // key = ch * 128 + note
    std::vector<CloudNote>       cloudNotes;

    std::array<std::array<std::vector<int>, 128>, 16> activeAddedNotes;
    std::array<std::array<bool, 128>, 16>             discardedNotes;

    juce::Random     rng;
    std::vector<int> lastChordNotes;
    int              cycleIndex = 0;
    double           currentSampleRate = 44100.0;
    int              prevAnimMode = 0;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderProcessor)
};