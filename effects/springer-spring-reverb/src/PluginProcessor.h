#pragma once

#include <JuceHeader.h>

//==============================================================================
class SpringerAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    SpringerAudioProcessor();
    ~SpringerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override { return "Springer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void randomizeSprings();
    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // INTERNAL SPRING STRUCTS
    //==============================================================================
    struct SpringLine
    {
        // 1. Optimized Allpass (The Fixed Version)
        struct Allpass
        {
            float mem = 0.0f;

            // Clean Transposed Direct Form II implementation
            inline float process(float in, float a)
            {
                float out = mem - (a * in);
                mem = in + (a * out);
                return out;
            }
        };

        // 2. Density Filter (Wrapper for Pre-Diffusion)
        struct Density
        {
            juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
            float b1 = -0.6f; // Fixed coeff for density smearing

            void prepare(const juce::dsp::ProcessSpec& spec) {
                delayLine.prepare(spec);
                delayLine.reset();
            }

            void setDelay(float samples) {
                delayLine.setDelay(samples);
            }

            inline float process(float x) {
                float delayOut = delayLine.popSample(0);
                float y = x + (b1 * delayOut);
                delayLine.pushSample(0, y);
                return delayOut + ((-b1) * y);
            }
        };

        // --- SpringLine Methods ---
        void prepare(double sampleRate, int maxDelay);
        void reset();
        float process(float in, float feedback, float lfoRate, double sr);

        // --- Members ---
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay;
        Density densityFilter;

        // Arrays for the dispersion engine
        std::array<Allpass, 256> allpasses;
        std::array<float, 256> cachedCoeffs;

        // State variables
        float lfoPhase = 0.0f;
        float dampingMem = 0.0f;
        float hpMem = 0.0f;
        float lastOut = 0.0f;

        // Parameters (Updated by Processor)
        int numDispersionStages = 32;
        float targetDelayScaled = 500.0f;
        float targetDepthScaled = 0.0f;
        
        // Optimizations: Cached values to avoid recalculation per sample
        float maxSafeDelay = 0.0f;      // Max safe delay value
        float dampingCoeff = 0.6f;      // Pre-calculated (1.0 - effectiveDamping)
    };

    // Vector for 7 coils
    std::vector<SpringLine> springs;
    // Vector of Vectors for coefficients: [CoilIndex][StageIndex]
    std::vector<std::vector<float>> allCoilCoeffs;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    double sr = 44100.0;
    double invSampleRate = 1.0 / 44100.0; // Cached inverse for faster division
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpringerAudioProcessor)
};
