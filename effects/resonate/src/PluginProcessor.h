/*
  ==============================================================================
    Ableton-Style Resonator Plugin
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class ResonateAudioProcessor : public juce::AudioProcessor
{
public:
    ResonateAudioProcessor();
    ~ResonateAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    enum FilterType { Lowpass = 0, Highpass, Bandpass, Notch };
    enum ProcessingMode { ModeA = 0, ModeB = 1 };

private:
    // =========================================================================
    struct Resonate
    {
        static constexpr int MAX_DELAY_SAMPLES = 8192;
        std::vector<float> delayBuffer[2];
        int writeIndex[2] = { 0, 0 };

        // Core comb-filter params
        double targetFrequency = 440.0;
        double delayInSamples = 100.0;
        double feedback = 0.9;
        int    pitchSemitones = 0;
        double fineDetune = 0.0;
        double gain = 0.0;
        bool   enabled = true;

        // Color/damping LPF
        float  lpfState[2] = { 0.0f, 0.0f };
        float  lpfCoeff = 1.0f;

        // Chorus / LFO
        double lfoPhase[2] = { 0.0, 0.0 };
        double lfoRate = 0.0;
        double lfoDepthCents = 0.0;
        double currentSampleRate = 44100.0;

        // Membrane modulator
        uint32_t noiseSeed[2] = { 12345u, 67890u };
        float    membraneFilter[2] = { 0.0f, 0.0f };
        double   membraneSamples = 0.0;

        // DC-offset accumulator
        float  dcEnv[2] = { 0.0f, 0.0f };
        bool   centerMode = true;

        // Exponential decay envelope (output-side shaping)
        float  expEnv[2] = { 0.0f, 0.0f };
        float  expCoeff = 0.9f;   // per-sample decay coeff, freq-scaled
        bool   expDecay = false;

        // Per-resonator decay/color/const
        double localDecay = 85.0;
        double localColor = 50.0;
        bool   localConst = false;
        bool   useLocalParams = false;

        void  prepare(double sampleRate);
        void  updateParameters(double sampleRate,
            double globalDecay, double globalNote,
            double color,
            ProcessingMode mode, bool constMode,
            double chorusAmount, int resonatorIndex,
            double userLfoRate, double userLfoDepth,
            bool   center,
            bool   useLocal,
            double lDecay, double lColor, bool lConst);
        float processA(float input, int channel);
        float processB(float input, int channel);
        void  reset();

    private:
        float readDelayLinear(int channel, double delaySamples);

        float fastNoise(int channel)
        {
            noiseSeed[channel] = noiseSeed[channel] * 1664525u + 1013904223u;
            return static_cast<float>(static_cast<int32_t>(noiseSeed[channel]))
                / static_cast<float>(0x80000000);
        }
    };

    Resonate resonators[5];

    struct StateVariableFilter
    {
        float low[2] = { 0.0f, 0.0f };
        float band[2] = { 0.0f, 0.0f };
        float freq = 0.5f;
        float q = 0.7f;

        void  setFrequency(float normalizedFreq, float resonance);
        float process(float input, int channel, FilterType type);
        void  reset();
    };

    StateVariableFilter inputFilter;

    juce::AudioProcessorValueTreeState parameters;
    double currentSampleRate = 44100.0;

    double globalNote = 60.0;
    double globalDecay = 85.0;
    double globalColor = 50.0;
    double globalSmooth = 0.0;
    double globalChorus = 0.0;
    bool   filterEnabled = false;
    double filterFrequency = 1000.0;
    FilterType     filterType = Lowpass;
    ProcessingMode processingMode = ModeA;
    bool   constMode = false;
    bool   wetOnly = false;
    bool   centerMode = true;
    bool   perResMode = false;

    float inputSmoothing[2] = { 0.0f, 0.0f };
    float outputSmoothing[2] = { 0.0f, 0.0f };

    void updateResonateParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResonateAudioProcessor)
};