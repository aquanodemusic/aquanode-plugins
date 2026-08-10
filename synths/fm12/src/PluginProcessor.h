#pragma once

#include <JuceHeader.h>
#include <array>

class FM12SynthAudioProcessor;

//==============================================================================
// FM12Voice - Optimized Voice Class
//==============================================================================
class FM12Voice : public juce::SynthesiserVoice
{
public:
    static constexpr int numOperators = 12;

    FM12Voice(FM12SynthAudioProcessor& p);

    bool canPlaySound(juce::SynthesiserSound*) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void setCurrentPlaybackSampleRate(double newRate) override;

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
        int startSample,
        int numSamples) override;

    bool isVoiceActive() const override { return isVoiceActiveFlag; }

    static juce::String opParamID(int op, const char* name);
    static juce::String routeID(int from, int to);
    static juce::String feedbackID(int op);

    juce::SmoothedValue<float> smoothedOpLevels[numOperators];
    juce::SmoothedValue<float> smoothedMasterVol;

private:
    FM12SynthAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;

    double sampleRate = 44100.0;
    int noteMidi = 0;
    float level = 0.0f;
    bool isVoiceActiveFlag = false;

    // Pre-allocated arrays for performance
    alignas(16) float opPhase[numOperators] = { 0.0f };
    alignas(16) float opDryPhase[numOperators] = { 0.0f }; // unmodulated phase for Cancel
    alignas(16) float opLastOutput[numOperators] = { 0.0f };
    alignas(16) float opOutput[numOperators] = { 0.0f };
    alignas(16) float currentRawSamples[numOperators] = { 0.0f };
    bool isCarrier[numOperators] = { false };

    std::array<juce::ADSR, numOperators> opEnvelopes;

    // Per-operator delay ring buffer for anyFM-style experimental feedback
    static constexpr int expDelaySize = 4096;
    static constexpr int expDelayMask = expDelaySize - 1;
    static constexpr int expDelayFixed = 1024;
    float expDelayBuf[numOperators][expDelaySize] = {};
    int   expDelayWrite[numOperators] = {};
    float expFeedback[numOperators] = {};

    // Voice-steal kill ramp
    static constexpr int killRampSamples = 128;
    bool  isBeingKilled = false;
    int   killRampCounter = 0;

    // DC blocker state (eliminates rare DC offset in LFM / ExpFM modes)
    // Simple one-pole highpass: y[n] = x[n] - x[n-1] + R·y[n-1], R ≈ 0.9999
    float dcBlockX = 0.0f;
    float dcBlockY = 0.0f;

    // Optimized connection storage
    struct Connection { int source, dest; };
    std::vector<Connection> activeConnections;
    std::vector<int>        activeOperatorIndices;

    // Cached parameter pointers
    std::atomic<float>* pMasterVol = nullptr;
    std::atomic<float>* pNyquist = nullptr;
    std::atomic<float>* pFmEngineMode = nullptr; // 0=PM, 1=LFM, 2=LFM-TZ, 3=ExpFM, 4=ExpFM-TZ
    std::atomic<float>* pExpFbMode = nullptr;

    std::atomic<float>* pOpRatio[numOperators] = {};
    std::atomic<float>* pOpLevel[numOperators] = {};
    std::atomic<float>* pOpFeedback[numOperators] = {};
    std::atomic<float>* pOpAttack[numOperators] = {};
    std::atomic<float>* pOpDecay[numOperators] = {};
    std::atomic<float>* pOpSustain[numOperators] = {};
    std::atomic<float>* pOpRelease[numOperators] = {};
    std::atomic<float>* pOpCancel[numOperators] = {}; // Cancel: subtract dry carrier
    std::atomic<float>* pOpDelay[numOperators] = {}; // onset delay in seconds
    std::atomic<float>* pOpOsc[numOperators] = {}; // waveform: 0=Sine 1=Tri 2=Saw 3=Sqr 4-6=User
    std::atomic<float>* pRoute[numOperators][numOperators] = {}; // null on diagonal

    // Per-operator onset delay state
    float opDelayRemaining[numOperators] = {};  // seconds until noteOn fires
    bool  opNoteOnFired[numOperators] = {};  // true once noteOn has been called
    bool  noteHeld = false; // cleared on stopNote to cancel pending delays
};

//==============================================================================
// FM12SynthAudioProcessor
//==============================================================================
class FM12SynthAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int sineTableSize = 2048;
    static constexpr int sineTableMask = sineTableSize - 1;
    static constexpr int maxVoices = 12;

    FM12SynthAudioProcessor();
    ~FM12SynthAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static inline float fastSin(float phase) noexcept;

    // Samples the wave for a given operator.
    // Falls back to the built-in sine when no user wave has been loaded for that op.
    // Phase is normalised [0, 1). Called from FM12Voice::renderNextBlock.
    inline float sampleWave(int op, float phase) const noexcept;

    // ── Per-operator user wave (512 / 1024 / 2048 samples each) ──────────────
    // userWaveData[op] is a raw float buffer; userWaveSize[op] == 0 means
    // "use built-in sine for this operator".
    // The spinlock protects writes from the editor thread against audio-thread reads.
    juce::SpinLock userWaveLock;
    float          userWaveData[12][2048] = {};
    int            userWaveSize[12] = {};

    juce::AudioProcessorValueTreeState apvts;
    static std::array<float, sineTableSize> sineTable;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::Synthesiser synth;

    // Chorus effect state
    static constexpr int maxChorusDelay = 8192;
    alignas(16) float chorusDelayBufferL[maxChorusDelay] = { 0.0f };
    alignas(16) float chorusDelayBufferR[maxChorusDelay] = { 0.0f };
    int   chorusWritePos = 0;
    float chorusLFOPhase = 0.0f;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM12SynthAudioProcessor)
};