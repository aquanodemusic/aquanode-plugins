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
    static juce::String opPhaseID(int op);
    static juce::String routeID(int from, int to);
    static juce::String feedbackID(int op);

private:
    FM12SynthAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;

    double sampleRate = 44100.0;
    int noteMidi = 0;
    float level = 0.0f;
    bool isVoiceActiveFlag = false;

    // Pre-allocated arrays for performance
    alignas(16) float opPhase[numOperators] = { 0.0f };
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

    // Voice-steal kill ramp — avoids click when JUCE steals a busy voice
    static constexpr int killRampSamples = 128;
    bool  isBeingKilled = false;
    int   killRampCounter = 0;

    // Optimized connection storage
    struct Connection { int source, dest; };
    std::vector<Connection> activeConnections;
    std::vector<int> activeOperatorIndices;

    // Cached parameter pointers — set once in constructor, dereferenced in audio loop.
    // Eliminates ~172 string hash lookups per block per voice.
    std::atomic<float>* pMasterVol = nullptr;
    std::atomic<float>* pNyquist = nullptr;
    std::atomic<float>* pFmMode = nullptr;
    std::atomic<float>* pExpFbMode = nullptr;
    std::atomic<float>* pOpRatio[numOperators] = {};
    std::atomic<float>* pOpLevel[numOperators] = {};
    std::atomic<float>* pOpFeedback[numOperators] = {};
    std::atomic<float>* pOpPhase[numOperators] = {};
    std::atomic<float>* pOpAttack[numOperators] = {};
    std::atomic<float>* pOpDecay[numOperators] = {};
    std::atomic<float>* pOpSustain[numOperators] = {};
    std::atomic<float>* pOpRelease[numOperators] = {};
    std::atomic<float>* pRoute[numOperators][numOperators] = {}; // null on diagonal
};

//==============================================================================
// FM12SynthAudioProcessor - Main Processor with Chorus
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

    // Fast sine lookup
    static inline float fastSin(float phase) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    static std::array<float, sineTableSize> sineTable;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::Synthesiser synth;

    // Chorus effect state
    static constexpr int maxChorusDelay = 8192;
    alignas(16) float chorusDelayBufferL[maxChorusDelay] = { 0.0f };
    alignas(16) float chorusDelayBufferR[maxChorusDelay] = { 0.0f };
    int chorusWritePos = 0;
    float chorusLFOPhase = 0.0f;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM12SynthAudioProcessor)
};