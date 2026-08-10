#pragma once
#include <JuceHeader.h>

static constexpr int kMaxHarmonics = 128;
static constexpr int kMaxOscs = kMaxHarmonics * 2 + 1;   // bidir + fundamental
static constexpr int kNumVoices = 5;
static constexpr int kTableSize = 4096;                     // power-of-2 for fast wrap

class CombWeaveAudioProcessor : public juce::AudioProcessor
{
public:
    CombWeaveAudioProcessor();
    ~CombWeaveAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi()  const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ----------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    // Shared sine wavetable (size + 1 so index kTableSize is valid for lerp)
    float sineTable[kTableSize + 1];

    // Called from both audio thread (updateVoice) and message thread (visualiser).
    // Audio thread version writes into a fixed-size caller-supplied buffer.
    // Message thread version returns a vector (heap alloc OK on message thread).
    std::vector<float> computeHarmonicFreqsVec(float fundamental) const;

    // Fundamental of the most recently triggered note (for display)
    std::atomic<float> displayFundamental{ 440.0f };

private:
    // ------------------------------------------------------------------
    // Wavetable oscillator – no heap alloc, lerp interpolation
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

    // ------------------------------------------------------------------
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

        // Returns current envelope level; calls kill() when release finishes.
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

    // ------------------------------------------------------------------
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Fills caller-supplied arrays; returns count. No heap alloc – audio-thread safe.
    int computeHarmonicFreqsFixed(float fundamental,
        float* outFreqs,
        float* outGains,
        int    maxSize) const noexcept;

    void handleNoteOn(int note, float vel) noexcept;
    void handleNoteOff(int note)            noexcept;
    void updateVoice(Voice& v)            noexcept;
    void rebuildHpFilter();

    float wrapFreq(float f, float lo, float hi) const noexcept;
    float mirrorFreq(float f, float lo, float hi) const noexcept;

    Voice voices[kNumVoices];
    int   ageCounter = 0;

    juce::dsp::IIR::Filter<float> hpL, hpR;
    float  lastHpFreq = -1.0f;
    double lastSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombWeaveAudioProcessor)
};