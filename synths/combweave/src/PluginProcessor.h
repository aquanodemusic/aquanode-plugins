#pragma once
#include <JuceHeader.h>

static constexpr int kMaxHarmonics = 128;
static constexpr int kMaxOscs = kMaxHarmonics * 2 + 1;   // bidir + fundamental
static constexpr int kNumVoices = 5;
static constexpr int kTableSize = 4096;                      // power-of-2 for fast wrap

class CombWeaveAudioProcessor : public juce::AudioProcessor
{
public:
    // ── Public POD snapshot of one oscillator's parameters ───────────────
    // Filled once per processBlock via loadOscParams(); no per-sample APVTS reads.
    struct OscParams
    {
        int   amount;
        float hpFreq;
        int   spreadMode;      // 0=Linear, 1=Exp, 2=Harmonics
        float spread;          // Hz (linear) or ratio (exp) — pre-selected
        float volumeLinear;
        bool  bidirectional;
        int   freqMode;        // 0=Regular, 1=Wrap, 2=Mirror
        float attackMs;
        float releaseMs;
        float rolloff;
        float tuneCents;
        bool  noteLock;
        int   harmonicFilter;  // 0=All, 1=Evens Only, 2=Odds Only
    };

    CombWeaveAudioProcessor();
    ~CombWeaveAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName()    const override;
    bool acceptsMidi()   const override;
    bool producesMidi()  const override;
    bool isMidiEffect()  const override;
    double getTailLengthSeconds() const override;

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ── Public API ───────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;

    // Shared sine wavetable (size+1 so index kTableSize is valid for lerp)
    float sineTable[kTableSize + 1];

    // Load all parameters for one oscillator (0 = osc1, 1 = osc2).
    // Safe to call from both audio thread and message thread (atomic loads only).
    OscParams loadOscParams(int oscIndex) const noexcept;

    // Message-thread versions (heap alloc OK).
    // oscIndex 0 → teal bars, 1 → white bars in display.
    std::vector<float> computeHarmonicFreqsVec(float fundamental, int oscIndex = 0) const;

    // Fundamental of the most recently triggered note (for display)
    std::atomic<float> displayFundamental{ 440.0f };

private:
    // ── Wavetable oscillator — no heap alloc, lerp interpolation ─────────
    struct SineOsc
    {
        double phase = 0.0;
        double phaseInc = 0.0;

        void setFreq(double freq, double sr) noexcept
        {
            phaseInc = freq / sr * kTableSize;
        }

        float tick(const float* tbl) noexcept
        {
            int   i0 = (int)phase & (kTableSize - 1);
            float frac = (float)(phase - (int)phase);
            float s = tbl[i0] + frac * (tbl[i0 + 1] - tbl[i0]);
            phase += phaseInc;
            if (phase >= kTableSize) phase -= kTableSize;
            return s;
        }

        void reset() noexcept { phase = 0.0; phaseInc = 0.0; }
    };

    // ── Voice ─────────────────────────────────────────────────────────────
    struct Voice
    {
        int   midiNote = -1;
        float velocity = 0.0f;
        bool  active = false;
        bool  releasing = false;
        int   age = 0;

        SineOsc oscs[kMaxOscs];
        float   oscGains[kMaxOscs];
        int     numOscs = 0;

        double envLevel = 0.0;
        double attackRate = 0.0;
        double releaseRate = 0.0;

        void noteOn(int note, float vel, double sr, float atkMs, float relMs) noexcept
        {
            midiNote = note;
            velocity = vel;
            active = true;
            releasing = false;
            envLevel = 0.0;
            attackRate = 1.0 / juce::jmax(1.0, (double)atkMs * 0.001 * sr);
            releaseRate = 1.0 / juce::jmax(1.0, (double)relMs * 0.001 * sr);
        }

        void noteOff(double sr, float relMs) noexcept
        {
            releasing = true;
            releaseRate = 1.0 / juce::jmax(1.0, (double)relMs * 0.001 * sr);
        }

        void kill() noexcept
        {
            active = false; releasing = false;
            envLevel = 0.0; midiNote = -1; numOscs = 0;
        }

        float tickEnv() noexcept
        {
            if (!active) return 0.0f;
            if (releasing)
            {
                envLevel -= releaseRate;
                if (envLevel <= 0.0) { kill(); return 0.0f; }
            }
            else
            {
                envLevel = juce::jmin(1.0, envLevel + attackRate);
            }
            return (float)envLevel;
        }
    };

    // ── Private methods ───────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Core harmonic computation — takes a pre-loaded OscParams, no APVTS reads.
    // Audio-thread safe, no heap alloc.
    int computeHarmonicFreqsFixed(const OscParams& p,
        float fundamental,
        float* outFreqs,
        float* outGains,
        int    maxSize) const noexcept;

    void handleNoteOn(int note, float vel,
        const OscParams& p1, const OscParams& p2) noexcept;
    void handleNoteOff(int note,
        const OscParams& p1, const OscParams& p2) noexcept;
    void updateVoice(Voice& v, const OscParams& p) noexcept;
    void rebuildHpFilter(float hpFreq);
    void rebuildHpFilter2(float hpFreq);

    float wrapFreq(float f, float lo, float hi) const noexcept;
    float mirrorFreq(float f, float lo, float hi) const noexcept;

    // ── State ─────────────────────────────────────────────────────────────
    Voice voices[kNumVoices];   // oscillator 1
    Voice voices2[kNumVoices];   // oscillator 2 (triggered in lockstep)
    int   ageCounter = 0;

    juce::dsp::IIR::Filter<float> hpL, hpR;   // HP for osc1
    juce::dsp::IIR::Filter<float> hpL2, hpR2;  // HP for osc2
    float  lastHpFreq = -1.0f;
    float  lastHpFreq2 = -1.0f;
    double lastSampleRate = 44100.0;

    juce::AudioBuffer<float> scratchBuffer;  // osc2 synthesis before mixing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombWeaveAudioProcessor)
};