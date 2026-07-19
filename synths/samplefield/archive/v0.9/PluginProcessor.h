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
    int    midiNote = -1;
    int    sampleIndex = -1;
    double readPos = 0.0;
    double readSpeed = 1.0;
    float  gainL = 1.0f;
    float  gainR = 1.0f;

    // How many host samples to stay silent before picking the next sample
    // (used to honour skip probability with correct timing)
    int    skipSamplesRemaining = 0;

    // Countdown for the time-limit feature (in host samples). -1 = unlimited.
    int    timeLimitSamples = -1;

    // Fade-out state (10 ms).  fadeoutSamples == 0 means no fade in progress.
    int    fadeoutSamplesTotal = 0;
    int    fadeoutSamplesLeft = 0;

    void reset()
    {
        active = false;
        midiNote = -1;
        sampleIndex = -1;
        readPos = 0.0;
        readSpeed = 1.0;
        gainL = gainR = 1.0f;
        skipSamplesRemaining = 0;
        timeLimitSamples = -1;
        fadeoutSamplesTotal = 0;
        fadeoutSamplesLeft = 0;
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
    // Public API for the editor
    void loadSamples(const juce::Array<juce::File>& files);
    void unloadAllSamples();
    int  getNumLoadedSamples() const { return numLoadedSamples.load(); }

    // APVTS
    juce::AudioProcessorValueTreeState apvts;

    // Listener callback
    void parameterChanged(const juce::String& paramID, float newValue) override;

    // Called by the editor when tempo-lock toggle changes
    // tempoLockSteps: 0 = off, 1..8 = 1/8 .. 8/8 note multiples
    void setTempoLock(int tempoLockSteps);
    int  getTempoLock() const { return tempoLockStepsValue.load(); }

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

    // Cached param values (updated via listener)
    std::atomic<float> p_pan{ 0.0f };
    std::atomic<float> p_rate{ 1.0f };
    std::atomic<float> p_vol{ 1.0f };
    std::atomic<float> p_panRnd{ 0.0f };
    std::atomic<float> p_rateRnd{ 0.0f };
    std::atomic<float> p_volRnd{ 0.0f };
    std::atomic<float> p_skip{ 0.0f };
    std::atomic<float> p_time{ 10.0f }; // seconds; 10 = unlimited (max)

    // Tempo lock: 0 = off, 1..8 = beat fractions
    std::atomic<int> tempoLockStepsValue{ 0 };

    // Last known host BPM (updated from processBlock)
    std::atomic<double> hostBpm{ 120.0 };

    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();

    // Returns the effective play-time in host samples for the current settings.
    // Returns -1 when the time knob is at its maximum (unlimited).
    int computeTimeLimitSamples();

    int  pickRandomSample();
    void assignSampleToVoice(Voice& v);
    void handleNoteOn(int note, float velocity);
    void handleNoteOff(int note);
    void renderVoice(Voice& v, juce::AudioBuffer<float>& out, int startSample, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleFieldAudioProcessor)
};