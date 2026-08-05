#pragma once
#include <JuceHeader.h>

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
    // STRUCT DEFINITIONS
    //==============================================================================
    struct SpringLine
    {
        // 1. Allpass Filter (The Dispersion/Boing)
        // 
        struct Allpass
        {
            float a = 0.7f;
            float mem = 0.0f;

            // --- FIXED MATH ---
            // This is the "Transposed Direct Form II" Allpass.
            // It guarantees Gain = 1.0 for all frequencies.
            inline float process(float in)
            {
                float out = -a * in + mem;
                mem = in + a * out; // Update state
                return out;
            }
        };

        // 2. Density Filter (Reverb Tail/Smearing)
        struct Density
        {
            juce::dsp::DelayLine<float> delayLine{ 2000 };
            float g = 0.35f;

            void prepare(const juce::dsp::ProcessSpec& spec)
            {
                delayLine.prepare(spec);
                delayLine.reset();
            }

            // Simple Schroeder Allpass structure
            float process(float x)
            {
                // Safety check: ensure delay is at least 1 sample to avoid infinite zero-delay feedback
                float d = juce::jmax(1.0f, delayLine.getDelay());

                float delayed = delayLine.popSample(0, -1, true); // Pop oldest
                float y = x + g * delayed;
                delayLine.pushSample(0, y); // Recirculate

                // Output mix (Schroeder allpass form: -g*in + delayed)
                return -g * x + delayed;
            }

            void setDelay(float d) { delayLine.setDelay(d); }
        };

        // --- Member Variables ---
        juce::dsp::DelayLine<float> delay;
        Density densityFilter;
        Allpass allpasses[256];      // Maximum possible stages
        int numDispersionStages = 32;// Current active stages

        float lfoPhase = 0.0f;
        float dampingMem = 0.0f;
        float hpMem = 0.0f;
        float lastOut = 0.0f;

        // --- Main Functions ---
        void prepare(double sampleRate, int maxDelay);
        float process(float in, float baseDelay, float feedback, float damping,
            float lfoDepth, float lfoRate, double sr);
        void setDelay(float samples);
    };

    //==============================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SpringLine springA, springB;
    double sr = 44100.0;

    // Vectors to hold state for saving/loading
    std::vector<float> springACoeffs;
    std::vector<float> springBCoeffs;

    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpringerAudioProcessor)
};