#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * SpectralStereoize — per-bin stereo width multiplier using M/S + OLA-FFT.
 *
 * Three internal "channels" flow through the OLA engine:
 *   0 = main Mid   (synthesised → L+R output)
 *   1 = main Side  (synthesised, multiplied by width curve → L-R output)
 *   2 = sc Mid     (analysis only, for the sidechain spectrum display)
 */
class SpectralStereoizeAudioProcessor : public juce::AudioProcessor
{
public:
    SpectralStereoizeAudioProcessor();
    ~SpectralStereoizeAudioProcessor() override;

    //==========================================================================
    // AudioProcessor overrides
    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName()          const override;
    bool  acceptsMidi()                   const override;
    bool  producesMidi()                  const override;
    bool  isMidiEffect()                  const override;
    double getTailLengthSeconds()         const override;
    int   getNumPrograms()                      override;
    int   getCurrentProgram()                   override;
    void  setCurrentProgram  (int index)        override;
    const juce::String getProgramName    (int)  override;
    void  changeProgramName  (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock& destData)     override;
    void setStateInformation (const void* data, int sizeBytes) override;

    //==========================================================================
    // FFT parameters (runtime-changeable via setFFTSize)
    int fftOrder = 11;
    int fftSize  = 1 << fftOrder;   // 2048 default
    int hopSize  = fftSize / 4;      // 75 % overlap
    int numBins  = fftSize / 2 + 1;  // 1025 default

    // Absolute maximum across all supported FFT orders (10–13)
    static constexpr int maxBins = (1 << 13) / 2 + 1; // 4097

    //==========================================================================
    // Width curve API  — safe to call from the message thread at any time.
    // width = 0 → full mono, 1 → unchanged, 2 → double stereo width.

    /** Linearly interpolate a width segment into the curve. */
    void setWidthCurveRange (int startBin, int endBin,
                             float startVal, float endVal);

    /** Reset every bin to 1.0 (unity / no processing). */
    void resetWidthCurve();

    /** Copy the current write-side curve into dest (for the editor to display). */
    void getWidthCurve (std::array<float, maxBins>& dest);

    //==========================================================================
    // Spectrum display data — message-thread safe.

    /** Smoothed linear-magnitude spectrum for the main (M/S mid) signal. */
    void getMainSpectrumData      (float* out, int numBinsOut);

    /** Smoothed linear-magnitude spectrum for the sidechain mid signal. */
    void getSidechainSpectrumData (float* out, int numBinsOut);

    //==========================================================================
    // Misc

    /** Rebuild the FFT engine for a new power-of-two size (1024–8192). */
    void setFFTSize (int newSize);

    /** Updated in prepareToPlay; read by the editor for frequency labelling. */
    double currentSampleRate = 44100.0;

    float visualScale = 1.0f; // Standard-Skalierung

    //==========================================================================
    // Width curve — double buffer, lock-free for the audio thread.
    //
    //  Message thread : writes widthCurve_write under curveLock, sets curveIsDirty.
    //  Audio thread   : checks curveIsDirty; if set, grabs lock briefly to copy
    //                   write→read, then uses read-side for the rest of the frame.
    std::array<float, maxBins> widthCurve_write;
    std::array<float, maxBins> widthCurve_read;
    juce::CriticalSection      curveLock;
    std::atomic<bool>          curveIsDirty{ false };

private:
    //==========================================================================
    // FFT engine (one instance handles both forward and inverse)
    std::unique_ptr<juce::dsp::FFT> fftProcessor;

    //==========================================================================
    // OLA buffers — indexed by internal channel
    static constexpr int kNumCh = 3; // 0=mainMid  1=mainSide  2=scMid

    std::vector<std::vector<float>> inputFifo;    // ring buffers, size = fftSize
    std::vector<std::vector<float>> fftBuf;       // complex interleaved, size = fftSize*2
    std::vector<std::vector<float>> outputAccum;  // OLA accumulators, size = fftSize*2
                                                  // (only [0]=mid and [1]=side produce output)
    std::vector<float> window;
    float windowNormFactor = 1.0f;               // pre-computed per-sample OLA scale

    int              fifoWritePos = 0; // shared write position across all channels
    int              hopCounter   = 0; // counts up to hopSize then triggers a frame
    std::vector<int> accumReadPos;     // per output-channel read/write pivot in outputAccum

    //==========================================================================
    // Smoothed display data
    std::vector<float>    smoothedMain; // magnitude, size = maxBins
    std::vector<float>    smoothedSC;   // magnitude, size = maxBins
    juce::CriticalSection displayLock;

    //==========================================================================
    // Private helpers
    void allocateBuffers();
    void createWindow();
    void processFFTFrame();   // called every hopSize samples from processBlock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralStereoizeAudioProcessor)
};