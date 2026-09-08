#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <memory>

// Cepstrum coefficients are shown for the full fftSize/2 quefrency range,
// which changes with the FFT Size analysis parameter (512..8192 -> 256..4096
// coefficients). kMaxCepstrumSize is just the fixed-size backing storage for
// the largest possible case, sized so no allocation is ever needed on the
// audio thread; the actual number of valid entries in any given frame is
// SpectralFrame::cepstrumCount (<= kMaxCepstrumSize).
static constexpr int kMaxCepstrumSize = 4096; // = largest FFT size (8192) / 2

// One analysed frame of spectral data.
struct SpectralFrame
{
    float centroidHz   = 0.0f;
    float spreadHz     = 0.0f;
    float entropy01    = 0.0f;
    float flatness01   = 0.0f;
    float amplitude01  = 0.0f;
    float levelDb      = -120.0f; // RMS level in dB, pre-gain (independent of RG_AMP_GAIN), for silence gating
    float skewness01   = 0.0f; // normalised 0..1, 0.5 = symmetric spectrum
    float crest01      = 0.0f; // normalised 0..1 spectral crest (peak / mean)
    float slope01      = 0.0f; // normalised 0..1 spectral tilt, 0.5 = flat
    std::array<float, kMaxCepstrumSize> cepstrum {}; // normalised 0..1 magnitude per quefrency bin
    int   cepstrumCount = 0; // number of valid entries in `cepstrum` for this frame (= current fftSize / 2)
};

class EcoSeeAudioProcessor : public juce::AudioProcessor
{
public:
    EcoSeeAudioProcessor();
    ~EcoSeeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "EcoSee"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static constexpr int kHistorySize = 96;

    // FFT sizes selectable via the FFT_SIZE choice parameter (index -> order, 512..8192).
    static constexpr int FFT_ORDERS[5] = { 9, 10, 11, 12, 13 };

    // Fills `out` in place (out must already be sized kHistorySize).
    // Deliberately NOT returned by value: SpectralFrame is ~16KB (due to the
    // fixed kMaxCepstrumSize cepstrum buffer), so a kHistorySize array of
    // them is ~1.5MB -- returning/copying that by value risks a stack
    // overflow, especially on threads with small default stack sizes (e.g.
    // the Windows message thread). Callers should keep the destination
    // array as a member (heap/data segment), never a stack-local.
    void getHistorySnapshot (std::array<SpectralFrame, kHistorySize>& out);

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();

private:
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    int currentFftOrderIndex = -1;
    int currentWindowIndex   = -1;
    int currentFftSize       = 0;

    std::vector<float> fifo;
    int fifoIndex = 0;
    int hopSize = 0;
    int hopCounter = 0;

    std::vector<float> fftData;

    // Scratch buffers for cepstral analysis (log-magnitude -> inverse FFT -> real cepstrum).
    std::vector<juce::dsp::Complex<float>> cepBufIn, cepBufOut;

    double currentSampleRate = 44100.0;

    std::array<SpectralFrame, kHistorySize> history;
    int historyWritePos = 0;
    juce::CriticalSection historyLock;

    void rebuildAnalysisIfNeeded();
    void pushSample (float sample);
    void computeSpectralFrame();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EcoSeeAudioProcessor)
};
