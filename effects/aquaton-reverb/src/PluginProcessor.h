#pragma once
#include <JuceHeader.h>
#include <array>

//==============================================================================
// Aquaton — FDN Reverb
//
// Architecture overview:
//   1. Pre-delay      – positive: wet-path delay (no DAW latency)
//                       negative: dry-path delay + wet fires early (DAW latency reported)
//   2. Input diffusion  – variable-tap allpass chain per channel
//   3. FDN core         – up to 64 delay lines mixed via a signed Hadamard matrix
//   4. Per-line DSP     – delay modulation, allpass tank diffusion, two-pole LP/HP
//                         feedback filtering, optional HF wash chorus
//   5. Saturation       – soft tanh clip stabilises feedback above unity gain
//   6. Stereo output    – bloom-weighted panning across even/odd line pairs
//   7. Freeze           – locks feedback at unity and gates the input signal
//==============================================================================

class AquatonAudioProcessor : public juce::AudioProcessor
{
public:
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

    // Randomise the Hadamard sign pattern, per-line polarity, and optionally
    // switch to log-spaced delay times.  Called from the UI button.
    // Thread-safe: builds into temporaries then commits under matrixLock.
    void randomizeMatrix();

    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // DSP constants
    //==========================================================================
    static constexpr int NUM_LINES   = 64;
    static constexpr int MAX_AP_TANK = 256;
    static constexpr int MAX_INPUT_AP = 64;

    //--------------------------------------------------------------------------
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
    struct FDNLine
    {
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay;

        std::array<Allpass, MAX_AP_TANK> tankAP;
        OnePole  lpFilter;
        OnePole  lpFilter2;
        OnePole  hpFilter;
        OnePole  hfExtractLP;
        Allpass  hfWashAP;

        float lfoPhase    = 0.f;
        float hfLfoPhase  = 0.f;
        float lastOut     = 0.f;
        float baseDelay   = 0.f;
        float maxSafeDly  = 0.f;

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
            lpFilter.reset(); lpFilter2.reset();
            hpFilter.reset(); hfExtractLP.reset();
            hfWashAP.reset();
            lfoPhase = hfLfoPhase = lastOut = 0.f;
        }

        float process(float in,
            float lpCoeff, float hpCoeff,
            float lfoRate, float lfoDepth,
            float tankDiffCoeff, int   tankDiffStages,
            float apfModAmt,
            float hfCrossCoeff, float hfWashAmt,
            double sr) noexcept
        {
            lfoPhase += (float)(lfoRate / sr);
            lfoPhase -= std::floor(lfoPhase);
            const float lfo = juce::dsp::FastMathApproximations::sin(
                lfoPhase * juce::MathConstants<float>::twoPi);

            const float modDly = juce::jlimit(1.f, maxSafeDly, baseDelay + lfo * lfoDepth);
            delay.pushSample(0, in);
            float out = delay.popSample(0, modDly);

            const int stages = juce::jmin(tankDiffStages, (int)MAX_AP_TANK);
            for (int i = 0; i < stages; ++i)
            {
                float coeff = tankDiffCoeff;
                if (apfModAmt > 0.001f)
                {
                    const float stageOffset = (float)i * 0.618034f;
                    const float stageLfo = juce::dsp::FastMathApproximations::sin(
                        (lfoPhase + stageOffset) * juce::MathConstants<float>::twoPi);
                    coeff = juce::jlimit(-0.95f, 0.95f,
                        tankDiffCoeff + stageLfo * apfModAmt * 0.35f);
                }
                out = tankAP[i].process(out, coeff);
            }

            out = lpFilter.processLP(out, lpCoeff);
            out = lpFilter2.processLP(out, lpCoeff);
            out = hpFilter.processHP(out, hpCoeff);

            if (hfWashAmt > 0.001f)
            {
                hfLfoPhase += (float)(lfoRate * 2.7f / sr);
                hfLfoPhase -= std::floor(hfLfoPhase);
                const float hfLfo = juce::dsp::FastMathApproximations::sin(
                    hfLfoPhase * juce::MathConstants<float>::twoPi);

                float lf = hfExtractLP.processLP(out, hfCrossCoeff);
                float hf = out - lf;
                float hfProcessed = hfWashAP.process(hf, hfLfo * 0.75f);
                out = lf + hf * (1.f - hfWashAmt) + hfProcessed * hfWashAmt;
            }

            out = std::tanh(out * 0.9f) * 1.111f;
            lastOut = out;
            return out;
        }
    };

    //==========================================================================
    // FDN state arrays (public so editor can inspect if needed)
    //==========================================================================
    std::array<FDNLine, NUM_LINES>          fdnLines;
    std::array<Allpass, MAX_INPUT_AP>       inputDiffL;
    std::array<Allpass, MAX_INPUT_AP>       inputDiffR;
    std::array<float, NUM_LINES*NUM_LINES>  mixMatrix;
    std::array<float, NUM_LINES>            customDelays;
    bool                                    useCustomDelays = false;
    std::array<float, NUM_LINES>            linePolarity;

    static constexpr std::array<int, NUM_LINES> BASE_DELAYS = {
        1013, 1103, 1201, 1303, 1409, 1511, 1619, 1733,
        1861, 2003, 2153, 2311, 2477, 2657, 2851, 3061,
        3271, 3499, 3739, 4001, 4273, 4567, 4877, 5209,
        5563, 5939, 6343, 6779, 7237, 7481, 7727, 7963,
        8221, 8513, 8819, 9157, 9521, 9907,10313,10753,
       11213,11677,12163,12671,13177,13729,14293,14869,
       15461,16067,16699,17351,18013,18691,19391,20101,
       20873,21661,22481,23321,24181,25087,26017,26993
    };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void buildMatrix(const std::array<float, NUM_LINES>& rowSigns,
                     const std::array<float, NUM_LINES>& colSigns);

    double sr = 44100.0;
    juce::Random rng;

    //==========================================================================
    // Pre-delay lines
    //   Positive preDelay: wet path is delayed (no DAW latency)
    //   Negative preDelay: dry path is delayed, wet fires immediately
    //                      → report |preDelay| samples latency to host
    //==========================================================================
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayLineL;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayLineR;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dryDelayLineL;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dryDelayLineR;

    //==========================================================================
    // Thread-safe matrix double-buffer
    //
    // randomizeMatrix() (message thread) builds new state into the shared
    // mixMatrix/linePolarity/customDelays arrays under matrixLock, then sets
    // matrixDirty.  processBlock (audio thread) copies to local* arrays once
    // per block when dirty — the SpinLock is only contested on the rare button
    // press, never in the steady state.
    //==========================================================================
    juce::SpinLock           matrixLock;
    std::atomic<bool>        matrixDirty { false };

    // Audio-thread-local copies — the sample loop reads these exclusively.
    std::array<float, NUM_LINES*NUM_LINES> localMatrix;
    std::array<float, NUM_LINES>           localPolarity;
    std::array<float, NUM_LINES>           localDelays;
    bool                                   localUseCustom = false;

    //==========================================================================
    // FDN order change tracking
    // When fdnOrder increases, newly-entered lines may have a stale lastOut
    // from when they were last active.  We zero them before they re-enter.
    //==========================================================================
    int prevFdnOrder = NUM_LINES;   // initialised high so first block does no zeroing

    //==========================================================================
    // Per-parameter smoothers
    //==========================================================================
    static constexpr float kSmoothTimeSec = 0.020f;

    juce::SmoothedValue<float> smSize;
    juce::SmoothedValue<float> smCompFeedback;
    juce::SmoothedValue<float> smWetNorm;
    juce::SmoothedValue<float> smLpCoeff;
    juce::SmoothedValue<float> smHpCoeff;
    juce::SmoothedValue<float> smHfCrossCoeff;
    juce::SmoothedValue<float> smPreDiff;
    juce::SmoothedValue<float> smTankDiff;
    juce::SmoothedValue<float> smModRate;
    juce::SmoothedValue<float> smModDepth;
    juce::SmoothedValue<float> smSpread;
    juce::SmoothedValue<float> smMix;
    juce::SmoothedValue<float> smBloomAmount;
    juce::SmoothedValue<float> smBloomTime;
    juce::SmoothedValue<float> smHfWashAmt;
    juce::SmoothedValue<float> smPolarityAmt;
    juce::SmoothedValue<float> smApfMod;
    juce::SmoothedValue<float> smPreDelay;   // smoothed ms value

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AquatonAudioProcessor)
};
