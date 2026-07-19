#pragma once

#include <JuceHeader.h>
#include <deque>
#include <complex>
#include <unordered_map>

//==============================================================================
/**
 * SpectralLatency — per-bin latency control using FFT + OLA.
 *
 * The user draws a delay curve where:
 *   - Center (0.0) = no additional delay
 *   - Upward (+1.0) = maximum positive delay
 *   - Downward (-1.0) = bins sound earlier (requires global latency compensation)
 *
 * Global latency = max(0, -min(curve)) * maxLatencySeconds
 * This ensures bins with negative delay can actually sound before others.
 *
 * State saving stores the delay curve AND maxLatency for every FFT size the
 * user has ever visited, keyed by fftSize.  On recall the last-active size
 * is restored first so the plugin opens exactly as it was left.
 */
class SpectralLatencyAudioProcessor : public juce::AudioProcessor
{
public:
    SpectralLatencyAudioProcessor();
    ~SpectralLatencyAudioProcessor() override;

    //==========================================================================
    // AudioProcessor overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName()          const override;
    bool  acceptsMidi()                   const override;
    bool  producesMidi()                  const override;
    bool  isMidiEffect()                  const override;
    double getTailLengthSeconds()         const override;
    int   getNumPrograms()                      override;
    int   getCurrentProgram()                   override;
    void  setCurrentProgram(int index)        override;
    const juce::String getProgramName(int)  override;
    void  changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock& destData)     override;
    void setStateInformation(const void* data, int sizeBytes) override;

    //==========================================================================
    // FFT parameters (runtime-changeable via setFFTSize)
    int fftOrder = 11;
    int fftSize = 1 << fftOrder;   // 2048 default
    int hopSize = fftSize / 4;      // 75 % overlap
    int numBins = fftSize / 2 + 1;  // 1025 default

    // Absolute maximum across all supported FFT orders (5–13)
    static constexpr int maxBins = (1 << 13) / 2 + 1; // 4097

    //==========================================================================
    // Delay curve API — safe to call from the message thread at any time.
    // delay = -1.0 → sounds earlier by maxLatency
    // delay =  0.0 → no delay
    // delay = +1.0 → delayed by maxLatency

    /** Linearly interpolate a delay segment into the curve. */
    void setDelayCurveRange(int startBin, int endBin,
        float startVal, float endVal);

    /** Reset every bin to 0.0 (no delay / unity). */
    void resetDelayCurve();

    /** Copy the current write-side curve into dest (for the editor to display). */
    void getDelayCurve(std::array<float, maxBins>& dest);

    //==========================================================================
    // Spectrum display data — message-thread safe.

    /** Smoothed linear-magnitude spectrum for display. */
    void getSpectrumData(float* out, int numBinsOut);

    //==========================================================================
    // Latency control

    /** Set the maximum latency in seconds (range 0.01 - 10.0). */
    void setMaxLatency(float seconds);

    /** Get the current maximum latency setting. */
    float getMaxLatency() const { return maxLatencySeconds; }

    /** Rebuild the FFT engine for a new power-of-two size (32–8192).
     *  Saves the current size's curve + maxLatency before switching and
     *  restores any previously saved state for the new size. */
    void setFFTSize(int newSize);

    /** Updated in prepareToPlay; read by the editor for frequency labelling. */
    double currentSampleRate = 44100.0;

    //==========================================================================
    // Delay curve — double buffer, lock-free for the audio thread.
    //
    //  Message thread : writes delayCurve_write under curveLock, sets curveIsDirty.
    //  Audio thread   : checks curveIsDirty; if set, grabs lock briefly to copy
    //                   write→read, then uses read-side for the rest of the frame.
    std::array<float, maxBins> delayCurve_write;
    std::array<float, maxBins> delayCurve_read;
    juce::CriticalSection      curveLock;
    std::atomic<bool>          curveIsDirty{ false };

    /** Randomizes the delay curve values and the max latency setting. */
    void randomize();

private:
    //==========================================================================
    // Per-FFT-size persistent state
    struct PerSizeState
    {
        std::array<float, maxBins> curve;
        float maxLatency = 1.0f;
    };

    /** Curve + maxLatency keyed by fftSize (32 / 128 / 256 / … / 8192).
     *  Populated lazily: an entry is created the first time the user visits
     *  that FFT size, or on setStateInformation restore. */
    std::unordered_map<int, PerSizeState> allSizeStates;

    /** True while setStateInformation is rebuilding allSizeStates so that
     *  setFFTSize does not overwrite freshly-restored entries. */
    bool isRestoringState = false;

    //==========================================================================
    // Latency parameters
    float maxLatencySeconds = 1.0f;  // Default 1 second
    std::atomic<int> reportedLatencySamples{ 0 };

    //==========================================================================
    // FFT engine (one instance handles both forward and inverse)
    std::unique_ptr<juce::dsp::FFT> fftProcessor;

    //==========================================================================
    // OLA buffers for stereo processing
    std::vector<float> inputFifoL;    // size = fftSize * 2
    std::vector<float> inputFifoR;    // size = fftSize * 2
    std::vector<float> fftBufL;       // complex interleaved, size = fftSize * 2
    std::vector<float> fftBufR;       // complex interleaved, size = fftSize * 2
    std::vector<float> outputAccumL;  // OLA accumulator, size = fftSize * 2
    std::vector<float> outputAccumR;  // OLA accumulator, size = fftSize * 2

    std::vector<float> window;
    float windowNormFactor = 1.0f;

    int fifoWritePos = 0;
    int accumReadPosL = 0;
    int accumReadPosR = 0;

    //==========================================================================
    // Per-bin delay buffers
    // Each bin has a queue of complex values (real, imag pairs)
    struct BinDelay
    {
        std::deque<std::complex<float>> bufferL;
        std::deque<std::complex<float>> bufferR;
        int currentDelaySamples = 0;  // in hop units (frames)
    };
    std::vector<BinDelay> binDelays;  // size = maxBins
    int maxDelayFrames = 0;           // Maximum delay in frames

    //==========================================================================
    // Smoothed display data
    std::vector<float>    smoothedSpectrum; // magnitude, size = maxBins
    juce::CriticalSection displayLock;

    //==========================================================================
    // Private helpers
    void allocateBuffers();
    void createWindow();
    void processFFTFrame();
    void updateLatencyCompensation();
    int calculateRequiredLatencyFrames() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralLatencyAudioProcessor)
};