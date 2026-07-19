#pragma once
#include <JuceHeader.h>

//==============================================================================
struct TranswaveVoice
{
    bool   active = false;
    int    midiNote = 0;
    float  velocity = 1.0f;
    double phase = 0.0;
    double framePhase = 0.0;

    // Random Cycle Jump states
    bool  jumpActive = false;
    float jumpFramePosNorm = 0.0f;
    float frameOffset = 0.0f;

    enum class Env { Idle, Attack, Decay, Sustain, Release } envStage = Env::Idle;
    float envLevel = 0.0f;
    float releaseStartLevel = 0.0f;
    float envTime = 0.0f;

    void noteOn(int note, float vel)
    {
        midiNote = note;
        velocity = vel;
        active = true;
        phase = 0.0;
        envStage = Env::Attack;
        envLevel = 0.0f;
        envTime = 0.0f;
        jumpActive = false;
        jumpFramePosNorm = 0.0f;
        frameOffset = 0.0f;
    }

    void noteOff()
    {
        releaseStartLevel = envLevel;
        envStage = Env::Release;
        envTime = 0.0f;
    }
};

//==============================================================================
class TranswaveAudioProcessor : public juce::AudioProcessor,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    TranswaveAudioProcessor();
    ~TranswaveAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TranswaveEngine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& paramID, float newValue) override;

    void loadWavetable(const juce::File& file, int singleCycleSamples);
    bool isWavetableLoaded() const { return wavetableLoaded; }
    int  getNumFrames() const { return numFrames; }
    int  getCycleSamples() const { return cycleSamples; }
    juce::String getWavetableName() const { return wavetableName; }

    float getCurrentFramePos() const;
    bool getFrameSamples(int frameIndex, std::vector<float>& outBuf) const;
    bool getWavetableOverview(int displayWidth, int displayHeight, std::vector<float>& outBuf) const;

    int requestedCycleSamples = 2048;
    float sampleFrameNearest(float frameIndex, double phase);

private:
    std::vector<std::vector<float>> frames;
    int   numFrames = 0;
    int   cycleSamples = 0;
    bool  wavetableLoaded = false;
    juce::String wavetableName;
    mutable juce::CriticalSection wavetableLock;

    static constexpr int MAX_VOICES = 16;
    TranswaveVoice voices[MAX_VOICES];
    double currentSampleRate = 44100.0;

    juce::SmoothedValue<float> gainSmooth;

    // Filter state (Moved out of process block so it doesn't break!)
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    std::atomic<float>* pPosition = nullptr;
    std::atomic<float>* pEvolution = nullptr;
    std::atomic<float>* pEvoLFORate = nullptr;
    std::atomic<float>* pEvoLFODepth = nullptr;
    std::atomic<float>* pPosLFORate = nullptr;
    std::atomic<float>* pPosLFODepth = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pBitCrush = nullptr;
    std::atomic<float>* pGrit = nullptr;
    std::atomic<float>* pDetune = nullptr;
    std::atomic<float>* pPitchLFO = nullptr;
    std::atomic<float>* pPitchLFORate = nullptr;
    std::atomic<float>* pScanStyle = nullptr;
    std::atomic<float>* pJumpProb = nullptr;
    std::atomic<float>* pFilterFreq = nullptr;
    std::atomic<float>* pFilterQ = nullptr;

    double evoLFOPhase = 0.0;
    double posLFOPhase = 0.0;
    double pitchLFOPhase = 0.0;
    int scanDirection = 1;
    double globalEvoPos = 0.0;

    float synthesiseVoice(TranswaveVoice& v, float framePosNorm, float posLFO, double pitchMult);
    float applyBitCrush(float s, float bits);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranswaveAudioProcessor)
};