/*
  ==============================================================================
    SpectralSmooth — live STFT cepstral smear / freeze
    Continuous phase-vocoder-style analysis/resynthesis on the live input.
    Every hop, the frame's log-magnitude spectrum is cepstrally liftered
    (Character) to smooth/blur it. Freeze locks that behaviour into one of
    six modes: the magnitude behaviour is Static / Evolving / Continuous,
    crossed with whether the auto gain-match keeps Following the live dry
    signal while frozen, or Holds the level it had at the instant freeze
    engaged.
  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <deque>
#include <vector>

namespace pid
{
    static const juce::String fftSize    = "fftSize";     // choice
    static const juce::String character  = "character";   // lifter cutoff, 0..1
    static const juce::String freeze     = "freeze";       // bool
    static const juce::String freezeMode = "freezeMode";   // choice: 7 modes, see FreezeMode
    static const juce::String evolveRate = "evolveRate";   // 0..1
    static const juce::String diffusion  = "diffusion";    // 0..1
    static const juce::String stretchTime = "stretchTime"; // seconds, grain period for Stretch mode
    static const juce::String mix        = "mix";
    static const juce::String gainMatch  = "gainMatch";
}

// Low 2 bits (mod 3) pick the magnitude behaviour; values >= 3 are the
// "Follow" variants of the same three, which keep the auto gain-match
// tracking the live dry signal while frozen instead of holding it steady.
// Stretch (6) sits outside that Hold/Follow x3 encoding entirely -- it's a
// slow granular crossfade between periodically re-snapshotted spectra, not
// a variant of Static/Evolving/Continuous, so it's never passed through
// freezeModeBase() and treats gain-match like Hold (steady).
enum class FreezeMode
{
    StaticHold = 0,
    EvolvingHold = 1,
    ContinuousHold = 2,
    StaticFollow = 3,
    EvolvingFollow = 4,
    ContinuousFollow = 5,
    Stretch = 6
};

// Which of Static/Evolving/Continuous a mode behaves like, ignoring Hold/Follow.
// Not meaningful for Stretch -- callers must branch on FreezeMode::Stretch first.
inline int freezeModeBase(FreezeMode m) { return (int) m % 3; }
// Whether this mode keeps the gain-match tracking the live dry signal while frozen.
inline bool freezeModeFollows(FreezeMode m)
{
    return m == FreezeMode::StaticFollow || m == FreezeMode::EvolvingFollow || m == FreezeMode::ContinuousFollow;
}

class SpectralSmoothAudioProcessor final : public juce::AudioProcessor
{
public:
    SpectralSmoothAudioProcessor();
    ~SpectralSmoothAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==============================================================================
    struct ChannelState
    {
        std::deque<float> inputQueue;
        std::deque<float> outputQueue;

        std::vector<float> analysisFrame;   // sliding window, size = fftSize
        std::vector<float> outAccum;        // overlap-add accumulator, size = fftSize

        std::vector<float> previousPhase;   // half+1, last hop's analysis phase
        std::vector<float> synthesisPhase;  // half+1, running phase used to resynthesise
        std::vector<float> heldMag;         // half+1, magnitude held while frozen
        std::vector<float> holdIncrement;   // half+1, per-bin phase advance captured at freeze-on

        // Stretch mode: two alternating grain snapshots (log-magnitude +
        // per-bin phase increment), continuously crossfaded A -> B across
        // the whole grain period (eased, so period boundaries have zero
        // slope and don't click). A is the period's starting grain, B is
        // the grain it's gliding toward; B is captured live and A takes
        // B's old value right at the period boundary.
        std::vector<float> grainLogMagA, grainLogMagB;
        std::vector<float> grainIncrementA, grainIncrementB;
        int grainHopCounter = 0;            // hops elapsed since the current grain period began

        bool wasFrozen = false;
        bool holdInitialised = false;

        juce::Random rng;
    };

    std::array<ChannelState, 2> channels;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;              // Hann, size = fftSize

    int currentFftSize = 2048;
    int currentHopSize = 512;               // fixed 4x overlap
    double sampleRate = 44100.0;

    void rebuildEngine();                   // called on prepareToPlay + fft size change
    void processHop(ChannelState& cs);      // runs the cepstral lifter + freeze logic for one hop

    static void cepstralSmoothMagnitude(const std::vector<float>& logMag,
                                         std::vector<float>& outSmoothedLogMag,
                                         juce::dsp::FFT& fftEngine, int fftSize,
                                         float characterNorm);

    juce::AudioBuffer<float> dryScratch;
    juce::LinearSmoothedValue<float> mixSmoothed, matchGainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralSmoothAudioProcessor)
};
