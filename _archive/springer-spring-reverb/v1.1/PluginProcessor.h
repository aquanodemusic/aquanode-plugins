#pragma once
#include <JuceHeader.h>

//==============================================================================
class SpringerAudioProcessor : public juce::AudioProcessor {
public:
    SpringerAudioProcessor();
    ~SpringerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "Springer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorValueTreeState apvts;
    void randomizeSprings();

private:
    //==============================================================================
    // NESTED STRUCTS
    //==============================================================================

    struct SpringLine
    {
        // 1. Optimized Allpass Filter
        struct Allpass
        {
            float mem = 0.0f;

            // Transposed Direct Form II - Now accepts 'a' per-sample
            inline float process(float in, float a)
            {
                float out = -a * in + mem;
                mem = in + a * out;
                return out;
            }
        };

        // 2. Density Filter (Schroeder Allpass)
        struct Density
        {
            juce::dsp::DelayLine<float> delayLine{ 2000 };
            float g = 0.35f;

            void prepare(const juce::dsp::ProcessSpec& spec)
            {
                delayLine.prepare(spec);
                delayLine.reset();
            }

            float process(float x)
            {
                float delayed = delayLine.popSample(0, -1, true);
                float y = x + g * delayed;
                delayLine.pushSample(0, y);
                return -g * x + delayed;
            }

            void setDelay(float d) { delayLine.setDelay(d); }
        };

        // --- SpringLine Internal Methods ---
        void prepare(double sampleRate, int maxDelay);

        // Optimized process: accepts pre-calculated values
        float process(float in, float feedback, float lfoRate, double sr);
        void setDelay(float samples);

        // --- SpringLine Member Variables ---
        juce::dsp::DelayLine<float> delay;
        Density densityFilter;
        Allpass allpasses[256];
        int numDispersionStages = 32;

        float lfoPhase = 0.0f;
        float dampingMem = 0.0f;
        float hpMem = 0.0f;
        float lastOut = 0.0f;

        // --- Coefficient Cache (Hoisted Parameters) ---
        float cachedCoeffs[256];
        float effectiveDamping = 0.0f;
        float targetDelayScaled = 0.0f;
        float targetDepthScaled = 0.0f;
    };

    //==============================================================================
    // PROCESSOR MEMBERS
    //==============================================================================

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SpringLine springA, springB;
    double sr = 44100.0;

    // These hold the "base" randomized state for saving/loading
    std::vector<float> springACoeffs;
    std::vector<float> springBCoeffs;

    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpringerAudioProcessor)
};