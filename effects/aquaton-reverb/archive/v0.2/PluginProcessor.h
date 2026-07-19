#pragma once
#include <JuceHeader.h>
#include <array>

//==============================================================================
// Aquaton Reverb – FDN Reverb
// 8-line Feedback Delay Network, Hadamard mixing, allpass diffusion,
// one-pole LP/HP in feedback, side bloom stereo widening, HF wash,
// and variable-tap input diffusion.
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
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void randomizeMatrix();

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // DSP CONSTANTS
    //==============================================================================
    static constexpr int NUM_LINES    = 8;    // FDN order
    static constexpr int MAX_AP_TANK  = 256;  // Max allpass stages per tank line
    static constexpr int MAX_INPUT_AP = 16;   // Max input diffuser taps

    //--------------------------------------------------------------------------
    // Transposed Direct Form II allpass
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
    struct OnePole
    {
        float z1 = 0.0f;
        inline float processLP(float in, float a) noexcept { z1 += a * (in - z1); return z1; }
        inline float processHP(float in, float a) noexcept { z1 += a * (in - z1); return in - z1; }
        void reset() noexcept { z1 = 0.0f; }
    };

    //--------------------------------------------------------------------------
    // Single FDN delay line with modulation, diffusion, LP/HP, and HF wash
    struct FDNLine
    {
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay;

        std::array<Allpass, MAX_AP_TANK> tankAP;
        OnePole  lpFilter;
        OnePole  hpFilter;
        OnePole  hfExtractLP;   // Crossover LP for HF wash
        Allpass  hfWashAP;      // Modulated allpass → chorus on HF

        float lfoPhase   = 0.f;
        float hfLfoPhase = 0.f;
        float lastOut    = 0.f;
        float baseDelay  = 0.f;
        float maxSafeDly = 0.f;

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
            lpFilter.reset(); hpFilter.reset(); hfExtractLP.reset();
            hfWashAP.reset();
            lfoPhase = hfLfoPhase = lastOut = 0.f;
        }

        // Process one sample through the full line
        float process(float in,
                      float lpCoeff,       float hpCoeff,
                      float lfoRate,       float lfoDepth,
                      float tankDiffCoeff, int   tankDiffStages,
                      float hfCrossCoeff,  float hfWashAmt,
                      double sr) noexcept
        {
            // --- Main LFO ---
            lfoPhase += (float)(lfoRate / sr);
            lfoPhase -= std::floor(lfoPhase);
            const float lfo = juce::dsp::FastMathApproximations::sin(
                lfoPhase * juce::MathConstants<float>::twoPi);

            const float modDly = juce::jlimit(1.f, maxSafeDly, baseDelay + lfo * lfoDepth);
            delay.pushSample(0, in);
            float out = delay.popSample(0, modDly);

            // --- Tank allpass diffusion (up to MAX_AP_TANK stages) ---
            const int stages = juce::jmin(tankDiffStages, (int)MAX_AP_TANK);
            for (int i = 0; i < stages; ++i)
                out = tankAP[i].process(out, tankDiffCoeff);

            // --- Feedback filters ---
            out = lpFilter.processLP(out, lpCoeff);
            out = hpFilter.processHP(out, hpCoeff);

            // --- HF Wash: frequency-dependent phaser/chorus on highs ---
            // Splits signal at hfCrossCoeff, applies a modulated allpass to the HF
            // band only. This makes high frequencies sound "washy"/chorus-like,
            // reducing the metallic comb-filter character typical in large reverbs.
            if (hfWashAmt > 0.001f)
            {
                // Faster, decorrelated LFO for HF (2.7× main rate)
                hfLfoPhase += (float)(lfoRate * 2.7f / sr);
                hfLfoPhase -= std::floor(hfLfoPhase);
                const float hfLfo = juce::dsp::FastMathApproximations::sin(
                    hfLfoPhase * juce::MathConstants<float>::twoPi);

                // Band-split
                float lf = hfExtractLP.processLP(out, hfCrossCoeff);
                float hf = out - lf;

                // Modulated first-order allpass → phaser-like frequency sweeping on HF
                float hfProcessed = hfWashAP.process(hf, hfLfo * 0.75f);

                // Blend: 0 = dry HF, 1 = fully washed HF
                out = lf + hf * (1.f - hfWashAmt) + hfProcessed * hfWashAmt;
            }

            // --- Soft saturation (keeps feedback stable even above unity) ---
            out = std::tanh(out * 0.9f) * 1.111f;

            lastOut = out;
            return out;
        }
    };

    //==============================================================================
    // FDN state
    std::array<FDNLine, NUM_LINES>    fdnLines;
    std::array<Allpass, MAX_INPUT_AP> inputDiffL;
    std::array<Allpass, MAX_INPUT_AP> inputDiffR;
    std::array<float, NUM_LINES * NUM_LINES> mixMatrix;

    // Prime-ish base delays (samples @ 44100 Hz, size = 1.0)
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
