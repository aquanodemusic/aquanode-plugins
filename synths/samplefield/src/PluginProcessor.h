#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <random>
#include <atomic>

//==============================================================================
class SampleFieldAudioProcessorEditor;

//==============================================================================
struct SampleBuffer
{
    juce::AudioBuffer<float> buffer;
    double originalSampleRate = 44100.0;
    juce::String filePath;
};

//==============================================================================
struct Voice
{
    bool   active = false;
    bool   dryActive = false;      // Track if the sample playback is running
    bool   noteOffReceived = false; // Track if MIDI note off was sent
    bool   feedDelay = false;       // Determined per-sample instance via probability
    int    midiNote = -1;
    int    sampleIndex = -1;
    double readPos = 0.0;
    double readSpeed = 1.0;
    float  gainL = 1.0f;
    float  gainR = 1.0f;

    int    skipSamplesRemaining = 0;
    int    timeLimitSamples = -1;

    int    fadeoutSamplesTotal = 0;
    int    fadeoutSamplesLeft = 0;

    // Per-voice stereo delay lines
    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;
    int    delayWritePos = 0;
    int    delayTailSamples = 0;    // Countdown for the tail ringout

    void reset()
    {
        active = false;
        dryActive = false;
        noteOffReceived = false;
        feedDelay = false;
        sampleIndex = -1;
        readPos = 0.0;
        readSpeed = 1.0;
        gainL = gainR = 1.0f;
        skipSamplesRemaining = 0;
        timeLimitSamples = -1;
        fadeoutSamplesTotal = 0;
        fadeoutSamplesLeft = 0;
        delayWritePos = 0;
        delayTailSamples = 0;
        if (!delayBufferL.empty()) std::fill(delayBufferL.begin(), delayBufferL.end(), 0.0f);
        if (!delayBufferR.empty()) std::fill(delayBufferR.begin(), delayBufferR.end(), 0.0f);
    }
};

//==============================================================================
class SampleFieldAudioProcessor : public juce::AudioProcessor,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int kMaxSamples = 128;
    static constexpr int kNumVoices = 24;

    SampleFieldAudioProcessor();
    ~SampleFieldAudioProcessor() override;

    //==========================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==========================================================================
    void loadSamples(const juce::Array<juce::File>& files);
    void unloadAllSamples();
    int  getNumLoadedSamples() const { return numLoadedSamples.load(); }

    juce::AudioProcessorValueTreeState apvts;

    void parameterChanged(const juce::String& paramID, float newValue) override;

    void setTempoLock(int tempoLockSteps);
    int  getTempoLock() const { return tempoLockStepsValue.load(); }

    // Delay Tempo Lock API
    void setDelayTempoLock(int steps);
    int  getDelayTempoLock() const { return delayTempoLockStepsValue.load(); }

private:
    //==========================================================================
    std::array<std::unique_ptr<SampleBuffer>, kMaxSamples> samples;
    std::atomic<int> numLoadedSamples{ 0 };

    juce::StringArray loadedFilePaths;
    juce::CriticalSection pathLock;

    //==========================================================================
    std::array<Voice, kNumVoices> voices;
    juce::CriticalSection voiceLock;

    //==========================================================================
    std::mt19937 rng{ std::random_device{}() };

    //==========================================================================
    double hostSampleRate = 44100.0;

    // Cached param values
    std::atomic<float> p_pan{ 0.0f };
    std::atomic<float> p_rate{ 1.0f };
    std::atomic<float> p_vol{ 1.0f };
    std::atomic<float> p_panRnd{ 0.0f };
    std::atomic<float> p_rateRnd{ 0.0f };
    std::atomic<float> p_volRnd{ 0.0f };
    std::atomic<float> p_skip{ 0.0f };
    std::atomic<float> p_time{ 10.0f };

    // New Delay Params
    std::atomic<float> p_delayTime{ 0.5f };
    std::atomic<float> p_delayVol{ 0.5f };
    std::atomic<float> p_delayProb{ 0.0f };
    std::atomic<int>   delayTempoLockStepsValue{ 0 };

    std::atomic<int> tempoLockStepsValue{ 0 };
    std::atomic<double> hostBpm{ 120.0 };

    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();

    int  computeTimeLimitSamples();
    int  computeDelayTimeSamples();

    int  pickRandomSample();
    void assignSampleToVoice(Voice& v);
    void handleNoteOn(int note, float velocity);
    void handleNoteOff(int note);

    // Split into dry voice rendering and overall voice rendering (dry + delay loop)
    void renderVoiceDry(Voice& v, juce::AudioBuffer<float>& out, int startSample, int numSamples);
    void renderVoice(Voice& v, juce::AudioBuffer<float>& out, int startSample, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleFieldAudioProcessor)
};