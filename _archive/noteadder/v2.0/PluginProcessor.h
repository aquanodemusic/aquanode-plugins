#pragma once
#include <JuceHeader.h>
#include <map>

class NoteAdderProcessor : public juce::AudioProcessor
{
public:
    static constexpr int NUM_ROWS   = 7;
    static constexpr int NUM_SCALES = 9;

    // Rate tables – exposed so the editor can populate combos
    static const double SUBDIV_FACTORS[9];   // beat multipliers for 1/1..1/16T
    static const int    FREE_RATE_MS[12];    // free-rate values in ms
    static const int    VEL_TABLE[10];       // velocity levels for cloud
    static const int    CLOUD_DECAY_MS[5];   // cloud note-length presets in ms
    static constexpr int NUM_SUBDIVS    = 9;
    static constexpr int NUM_FREE_RATES = 12;

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

    // ── Mode ──────────────────────────────────────────────────
    juce::AudioParameterInt* modeParam;

    // ── Manual ────────────────────────────────────────────────
    juce::AudioParameterBool* enabledParams[NUM_ROWS];
    juce::AudioParameterInt*  semiParams[NUM_ROWS];
    juce::AudioParameterInt*  octaveParams[NUM_ROWS];

    // ── Scale ─────────────────────────────────────────────────
    juce::AudioParameterInt*  rootNoteParam;
    juce::AudioParameterInt*  scaleTypeParam;
    juce::AudioParameterInt*  noteCountParam;
    juce::AudioParameterBool* discardParam;
    juce::AudioParameterBool* discardInputParam;
    juce::AudioParameterBool* randomSkipParam;
    juce::AudioParameterBool* lockBassParam;

    // 0=Normal 1=Picker 2=VoiceLeader 3=Drop2 4=CycleUp 5=CycleDown 6=RandomCycle
    juce::AudioParameterInt* inversionModeParam;
    juce::AudioParameterInt* inversionPickerParam;

    // ── Animation ─────────────────────────────────────────────
    // 0=Off  1=ReVoice  2=Wander  3=Drift  4=Cloud
    juce::AudioParameterInt*  animModeParam;
    juce::AudioParameterBool* animSyncBPMParam;   // true = sync to host BPM
    juce::AudioParameterInt*  animRateParam;      // 0-8  : subdivision index (BPM mode)
    juce::AudioParameterInt*  animRateFreeParam;  // 0-11 : free-rate index (ms table)
    juce::AudioParameterInt*  wanderProbParam;    // 0-9  → (val+1)*10 % per voice per tick
    juce::AudioParameterInt*  wanderMaxParam;     // 0-6  → val+1 max scale-degree steps
    juce::AudioParameterInt*  cloudDensityParam;  // 0-7  → val+1 notes spawned per tick
    juce::AudioParameterInt*  cloudSpreadParam;   // 0-6  → val+1 degree spread from root
    juce::AudioParameterInt*  cloudVelMinParam;   // 0-9  index into VEL_TABLE
    juce::AudioParameterInt*  cloudVelMaxParam;   // 0-9  index into VEL_TABLE
    juce::AudioParameterInt*  cloudDecayParam;    // 0-4  index into CLOUD_DECAY_MS

    static const int SCALE_INTERVALS[NUM_SCALES][7];
    static const int SCALE_SIZES[NUM_SCALES];

private:
    // ── Per-held-note animation state ─────────────────────────
    struct AnimNoteState
    {
        int channel   = 0;
        int velocity  = 100;
        int inputNote = 0;

        std::vector<int> baseNotes;    // original chord notes from computeAddedNotes
        std::vector<int> currentNotes; // currently sounding animation notes
        std::vector<int> wanderSteps;  // wander: step offset from baseNotes[i]

        int    invStep          = 0;   // ReVoice: current inversion step
        int    driftDegreeShift = 0;   // Drift:   scale degrees shifted from root
        double samplesUntilTick = 0.0;
    };

    // ── Cloud note with scheduled note-off ────────────────────
    struct CloudNote
    {
        int heldKey;          // ch * 128 + inputNote → matches animStates key
        int channel;
        int midiNote;
        int samplesRemaining;
    };

    static constexpr int MAX_CLOUD_NOTES = 96; // safety cap

    std::map<int, AnimNoteState> animStates;  // key = ch * 128 + note
    std::vector<CloudNote>       cloudNotes;

    // ── Existing per-note tracking ────────────────────────────
    std::array<std::array<std::vector<int>, 128>, 16> activeAddedNotes;
    std::array<std::array<bool, 128>, 16>             discardedNotes;

    juce::Random     rng;
    std::vector<int> lastChordNotes;
    int              cycleIndex       = 0;
    double           currentSampleRate = 44100.0;
    int              prevAnimMode      = 0; // detect mode changes

    // ── Audio-thread helpers ──────────────────────────────────
    std::vector<int> computeAddedNotes(int note);
    bool             isNoteInScale(int note) const;
    static void      rotateInversion(std::vector<int>& notes, int inv);

    int    nextScaleNoteAbove     (int note)              const;
    int    nextScaleNoteBelow     (int note)              const;
    int    noteAtScaleDegreeOffset(int baseNote, int deg) const;
    double computeTickSamples     ()                      const;

    // Animation tick helpers
    void tickReVoice(AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickWander (AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickDrift  (AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);
    void tickCloud  (AnimNoteState&, int ch, int samplePos, juce::MidiBuffer&);

    void processCloudNoteOffs(juce::MidiBuffer&, int numSamples);
    void killAnimForNote     (int ch, int note, juce::MidiBuffer&, int samplePos);
    void killAllAnimation    (juce::MidiBuffer&, int samplePos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderProcessor)
};
