#pragma once
#include <JuceHeader.h>
#include <array>

//==============================================================================
// Aquaton Reverb - FDN Reverb inspired by large-space algorithmic reverbs
// Architecture: 8-line Feedback Delay Network with Hadamard mixing matrix,
//               allpass diffusion chain, one-pole LP/HP in feedback path,
//               and gravity feedback that can exceed 1.0 for infinite growth.
//==============================================================================

class AquatonAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    AquatonAudioProcessor();
    ~AquatonAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "Aquaton"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Randomizes the Hadamard sign pattern (keeps orthogonality)
    void randomizeMatrix();

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // DSP STRUCTURES
    //==============================================================================
    static constexpr int NUM_LINES   = 8;   // FDN order
    static constexpr int MAX_AP_TANK = 8;   // Max allpass stages per tank line
    static constexpr int INPUT_AP    = 4;   // Input diffuser allpass stages

    //--------------------------------------------------------------------------
    // Transposed Direct Form II allpass - stable, unit delay memory
    struct Allpass
    {
        float mem = 0.0f;

        inline float process(float in, float coeff) noexcept
        {
            float out = -coeff * in + mem;
            mem = in + coeff * out;
            return out;
        }
        void reset() noexcept { mem = 0.0f; }
    };

    //--------------------------------------------------------------------------
    // One-pole filter for LP and HP in feedback path
    struct OnePole
    {
        float z1 = 0.0f;

        // a = smoothing coefficient (0 = no filtering, near 1 = heavy filtering)
        inline float processLP(float in, float a) noexcept
        {
            z1 += a * (in - z1);
            return z1;
        }
        // HP = signal minus LP
        inline float processHP(float in, float a) noexcept
        {
            z1 += a * (in - z1);
            return in - z1;
        }
        void reset() noexcept { z1 = 0.0f; }
    };

    //--------------------------------------------------------------------------
    // A single FDN delay line with modulation, allpass diffusion, and filters
    struct FDNLine
    {
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay;

        std::array<Allpass, MAX_AP_TANK> tankAP;
        OnePole lpFilter;
        OnePole hpFilter;

        float lfoPhase    = 0.0f;
        float lastOut     = 0.0f;
        float baseDelay   = 0.0f;  // In samples (scaled by Size)
        float maxSafeDly  = 0.0f;

        void prepare(const juce::dsp::ProcessSpec& spec, int maxDelaySamples)
        {
            delay.setMaximumDelayInSamples(maxDelaySamples);
            delay.prepare(spec);
            reset();
            maxSafeDly = (float)(maxDelaySamples - 64);
        }

        void reset()
        {
            delay.reset();
            for (auto& ap : tankAP) ap.reset();
            lpFilter.reset();
            hpFilter.reset();
            lfoPhase = 0.0f;
            lastOut  = 0.0f;
        }

        // Process one sample: receives signal already mixed by Hadamard + input
        // Returns the processed output that becomes lastOut for next block
        float process(float in,
                      float lpCoeff, float hpCoeff,
                      float lfoRate, float lfoDepth,
                      float tankDiffCoeff, int tankDiffStages,
                      double sr) noexcept
        {
            // --- LFO (branch-free wrap) ---
            lfoPhase += (float)(lfoRate / sr);
            lfoPhase -= std::floor(lfoPhase);
            const float lfo = juce::dsp::FastMathApproximations::sin(
                lfoPhase * juce::MathConstants<float>::twoPi);

            const float modDly = juce::jlimit(1.0f, maxSafeDly,
                baseDelay + lfo * lfoDepth);

            // --- Delay read/write ---
            delay.pushSample(0, in);
            float out = delay.popSample(0, modDly);

            // --- Tank allpass diffusion ---
            const int stages = juce::jmin(tankDiffStages, (int)MAX_AP_TANK);
            for (int i = 0; i < stages; ++i)
                out = tankAP[i].process(out, tankDiffCoeff);

            // --- LP filter in feedback (damping high freqs) ---
            out = lpFilter.processLP(out, lpCoeff);

            // --- HP filter in feedback (blocks DC / sub rumble) ---
            out = hpFilter.processHP(out, hpCoeff);

            // --- Soft saturation: stabilizes even at gravity > 1 ---
            out = std::tanh(out * 0.9f) * 1.111f;

            lastOut = out;
            return out;
        }
    };

    //==============================================================================
    // FDN state
    std::array<FDNLine, NUM_LINES> fdnLines;

    // Input diffuser (4 allpass stages, stereo - 2 channels each)
    std::array<Allpass, INPUT_AP> inputDiffL;
    std::array<Allpass, INPUT_AP> inputDiffR;

    // Mixing matrix: Hadamard H8, with randomizable sign pattern
    // Stored as flat row-major, normalized by 1/sqrt(8)
    std::array<float, NUM_LINES * NUM_LINES> mixMatrix;

    // Base delay lengths (prime-ish, in samples at 44100 Hz, size=1.0)
    static constexpr std::array<int, NUM_LINES> BASE_DELAYS = {
        1481, 1823, 2267, 2879, 3529, 4339, 5261, 6421
    };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void buildMatrix(const std::array<float, NUM_LINES>& rowSigns,
                     const std::array<float, NUM_LINES>& colSigns);

    double sr = 44100.0;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AquatonAudioProcessor)
};
