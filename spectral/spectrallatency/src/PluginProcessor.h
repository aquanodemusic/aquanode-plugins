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
 * APVTS exposes two automatable parameters:
 *   "maxLatency"   — float [0.01 .. 10.0], skewed so 0.5 s sits at centre travel
 *   "fftSizeIndex" — choice [0..7] → {32,128,256,512,1024,2048,4096,8192}
 *
 * State saving (getStateInformation / setStateInformation):
 *   New format  : APVTS ValueTree as XML, with the v2 binary curve blob
 *                 base64-encoded in a "curvesBinary" property.
 *   Legacy v1/v2: detected automatically; APVTS is synced afterwards.
 *
 * Curve arrays cannot be APVTS parameters (4097 values per FFT size × 8 sizes
 * would be >32 k parameters), so they continue to live in the binary blob.
 */
class SpectralLatencyAudioProcessor  : public juce::AudioProcessor,
                                        public juce::AudioProcessorValueTreeState::Listener
{
public:
    SpectralLatencyAudioProcessor();
    ~SpectralLatencyAudioProcessor() override;

    //==========================================================================
    // AudioProcessor overrides
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName()               const override;
    bool  acceptsMidi()                        const override;
    bool  producesMidi()                       const override;
    bool  isMidiEffect()                       const override;
    double getTailLengthSeconds()              const override;
    int   getNumPrograms()                           override;
    int   getCurrentProgram()                        override;
    void  setCurrentProgram (int index)              override;
    const juce::String getProgramName (int)          override;
    void  changeProgramName (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock& destData)     override;
    void setStateInformation (const void* data, int sizeBytes) override;

    //==========================================================================
    // APVTS — public so the editor can create SliderAttachment / ComboBoxAttachment.
    // Parameters:
    //   "maxLatency"   (AudioParameterFloat,  0.01..10.0 s, skewed, default 1.0)
    //   "fftSizeIndex" (AudioParameterChoice, 0..7,         default 5 = 2048)
    //   "bin0" .. "bin16" (AudioParameterFloat, -1.0..1.0, default 0.0)
    //                     Delay curve bins for FFT size 32 (17 bins) — DAW automatable
    juce::AudioProcessorValueTreeState apvts;

    // AudioProcessorValueTreeState::Listener — drives setMaxLatency / setFFTSize
    // when parameters change from the DAW host or the editor.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    //==========================================================================
    // FFT parameters (runtime-changeable via setFFTSize)
    int fftOrder = 11;
    int fftSize  = 1 << fftOrder;   // 2048 default
    int hopSize  = fftSize / 4;     // 75 % overlap
    int numBins  = fftSize / 2 + 1; // 1025 default

    // Absolute maximum across all supported FFT orders (5–13)
    static constexpr int maxBins = (1 << 13) / 2 + 1; // 4097

    //==========================================================================
    // Delay curve API — safe to call from the message thread at any time.

    /** Linearly interpolate a delay segment into the curve. */
    void setDelayCurveRange (int startBin, int endBin,
                              float startVal, float endVal);

    /** Reset every bin to 0.0 (no delay / unity). */
    void resetDelayCurve();

    /** Copy the current write-side curve into dest (for the editor to display). */
    void getDelayCurve (std::array<float, maxBins>& dest);

    //==========================================================================
    // Spectrum display data — message-thread safe.

    /** Smoothed linear-magnitude spectrum for display. */
    void getSpectrumData (float* out, int numBinsOut);

    //==========================================================================
    // Latency control

    /** Set the maximum latency in seconds (range 0.01 - 10.0).
     *  Does NOT update the APVTS parameter — use the APVTS for that. */
    void setMaxLatency (float seconds);

    /** Get the current maximum latency setting. */
    float getMaxLatency() const { return maxLatencySeconds; }

    /** Rebuild the FFT engine for a new power-of-two size (32–8192).
     *  Saves the current size's curve + maxLatency before switching and
     *  restores any previously saved state for the new size. */
    void setFFTSize (int newSize);

    /** Updated in prepareToPlay; read by the editor for frequency labelling. */
    double currentSampleRate = 44100.0;

    //==========================================================================
    // Delay curve — double buffer, lock-free for the audio thread.
    std::array<float, maxBins> delayCurve_write;
    std::array<float, maxBins> delayCurve_read;
    juce::CriticalSection      curveLock;
    std::atomic<bool>          curveIsDirty{ false };

    /** Randomises the delay curve and maxLatency, then notifies the APVTS
     *  so the max-latency slider updates automatically. */
    void randomize();

private:
    //==========================================================================
    // Per-FFT-size persistent state
    struct PerSizeState
    {
        std::array<float, maxBins> curve;
        float maxLatency = 1.0f;
    };

    std::unordered_map<int, PerSizeState> allSizeStates;

    /** True while setStateInformation / loadCurvesFromBinary is rebuilding
     *  allSizeStates.  Guards setFFTSize against overwriting fresh restore data,
     *  and guards parameterChanged against re-entrancy in JUCE versions that
     *  fire listeners synchronously from apvts.replaceState(). */
    bool isRestoringState = false;

    //==========================================================================
    // Latency parameters
    float maxLatencySeconds = 1.0f;
    std::atomic<int> reportedLatencySamples{ 0 };

    //==========================================================================
    // FFT engine
    std::unique_ptr<juce::dsp::FFT> fftProcessor;

    //==========================================================================
    // OLA buffers for stereo processing
    std::vector<float> inputFifoL;
    std::vector<float> inputFifoR;
    std::vector<float> fftBufL;
    std::vector<float> fftBufR;
    std::vector<float> outputAccumL;
    std::vector<float> outputAccumR;

    std::vector<float> window;
    float windowNormFactor = 1.0f;

    int fifoWritePos  = 0;
    int accumReadPosL = 0;
    int accumReadPosR = 0;

    //==========================================================================
    // Per-bin delay buffers
    struct BinDelay
    {
        std::deque<std::complex<float>> bufferL;
        std::deque<std::complex<float>> bufferR;
        int currentDelaySamples = 0;
    };
    std::vector<BinDelay> binDelays;
    int maxDelayFrames = 0;

    //==========================================================================
    // Smoothed display data
    std::vector<float>    smoothedSpectrum;
    juce::CriticalSection displayLock;

    //==========================================================================
    // APVTS helpers

    /** Build the parameter layout (called once in the constructor initialiser). */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Serialise allSizeStates (+ flush current size) to v2 binary format. */
    void saveCurvesToBinary   (juce::MemoryBlock& block);

    /** Deserialise v1 or v2 binary, populate allSizeStates, call setFFTSize.
     *  Sets isRestoringState = true/false around setFFTSize internally. */
    void loadCurvesFromBinary (const void* data, int sizeBytes);

    /** Push current maxLatencySeconds / fftSize into the APVTS parameters so
     *  the host and editor reflect the restored state (used after legacy load). */
    void syncAPVTSFromState();

    /** Push curve values (bins 0-16) into APVTS bin parameters when FFT size is 32. */
    void syncBinsToAPVTS();

    //==========================================================================
    // DSP helpers
    void allocateBuffers();
    void createWindow();
    void processFFTFrame();
    void updateLatencyCompensation();
    int  calculateRequiredLatencyFrames() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralLatencyAudioProcessor)
};