// Polyphonic filterbank resynthesizer / retuner.
// Signal path: log-spaced bandpass filterbank (2 cascaded RBJ biquads/band)
// -> per-band RMS envelope follower -> per-band sine oscillator, retuned
// via a 12x12 pitch-class remap matrix applied independently per band.

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>

class SpectralLockAudioProcessor : public juce::AudioProcessor
{
public:
    SpectralLockAudioProcessor();
    ~SpectralLockAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "SpectralLock"; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 2.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int numDisplayBins = 160;
    std::array<std::atomic<float>, numDisplayBins> display {};   // 0..1 band energy
    std::atomic<bool>  midiIsActive  { false };                  // lights the MIDI icon
    std::atomic<float> outputMeter   { 0.0f };

    // Scale presets, exposed so the editor can stamp them into the matrix.
    static constexpr int numScales = 12;
    static const char* const scaleNames[numScales];
    static const int   scaleTables[numScales][12];               // input pc -> output pc
    static const char* const noteNames[12];

private:
    // 36 bands / octave  ==  33 cent resolution.  ~6.2 octaves of coverage.
    static constexpr int   bandsPerOctave = 36;
    static constexpr float bankLowHz      = 60.0f;
    static constexpr float bankHighHz     = 4800.0f;
    static constexpr int   maxBands       = 240;
    static constexpr float bandQ          = 21.0f;   // per stage; 2 stages cascade

    struct Band
    {
        float fc          = 0.0f;   // analysis centre frequency
        float noteIn      = 0.0f;   // MIDI note number of fc
        float detune      = 0.0f;   // fixed pseudo-random offset, scaled by SPRAY
        float panL        = 0.707f;
        float panR        = 0.707f;

        // biquad coefficients (identical for both cascaded stages)
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        // transposed direct form II state, two stages
        float s1a = 0, s2a = 0, s1b = 0, s2b = 0;

        float envCoef     = 0.0f;   // envelope follower one-pole coefficient
        float envSq       = 0.0f;   // mean square
        float amp         = 0.0f;   // sqrt(envSq) * sqrt2  ->  amplitude estimate
        float gate        = 1.0f;   // ISOLATE per-band gate, smoothed

        float phase       = 0.0f;   // 0..1
        float freq        = 0.0f;   // smoothed (GLIDE)
        float targetFreq  = 0.0f;
        float ampGain     = 0.0f;   // tilt * trim, applied at output
        bool  muted       = false;  // outside MIDI capture window
    };

    std::array<Band, maxBands> bands;
    int numBands = 0;
    int firstBand = 0, lastBand = 0;   // active window from the RANGE slider

    void buildFilterbank();
    void updateBandMapping();          // matrix / root / midi / pitch -> targetFreq
    void updateRangeWindow (float loHz, float hiHz);

    static constexpr int tableSize = 2048;
    std::array<float, tableSize + 1> sineTable;
    inline float sine (float phase01) const noexcept
    {
        const float p = phase01 * (float) tableSize;
        const int   i = (int) p;
        const float f = p - (float) i;
        return sineTable[i] + f * (sineTable[i + 1] - sineTable[i]);
    }
    static inline float mtof (float note) noexcept
    {
        return 440.0f * std::pow (2.0f, (note - 69.0f) * (1.0f / 12.0f));
    }
    static inline float ftom (float f) noexcept
    {
        return 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, f) / 440.0f);
    }

    juce::dsp::LinkwitzRileyFilter<float> loSplit, hiSplit;

    std::array<bool, 128> noteHeld {};
    std::vector<int>      heldSorted;
    void collectMidi (juce::MidiBuffer&);

    std::atomic<float>* pMix = nullptr;   std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pIsolate = nullptr; std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pBend = nullptr;  std::atomic<float>* pBendRange = nullptr;
    std::atomic<float>* pWidth = nullptr; std::atomic<float>* pRangeLo = nullptr;
    std::atomic<float>* pRangeHi = nullptr; std::atomic<float>* pRoot = nullptr;
    std::atomic<float>* pPitch = nullptr; std::atomic<float>* pMuteLo = nullptr;
    std::atomic<float>* pMuteHi = nullptr; std::atomic<float>* pMidiOn = nullptr;
    std::atomic<float>* pFreeze = nullptr; std::atomic<float>* pShimmer = nullptr;
    std::atomic<float>* pSpray = nullptr; std::atomic<float>* pTilt = nullptr;
    std::atomic<float>* pLevel = nullptr;
    std::array<std::atomic<float>*, 12> pMatrix {};

    double sr = 44100.0;
    float  envCoef = 0.0f, gateAttack = 0.0f, gateRelease = 0.0f;
    float  globalPeak = 0.0f;
    float  transFast = 0.0f, transSlow = 0.0f;
    float  audioGate = 1.0f;
    float  displayDecay = 0.0f;

    juce::SmoothedValue<float> smMix, smAmount, smWidth, smLevel, smShimmer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralLockAudioProcessor)
};
