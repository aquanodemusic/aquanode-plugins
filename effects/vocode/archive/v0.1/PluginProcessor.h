#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

//==============================================================================
/**  Vocode — FFT-band vocoder with sidechain / self-vocode, vocode-reverb
 *   (per-band release tail) and a drawable per-band bandwidth curve.
 *
 *   Bus layout:
 *     Input 0  "Carrier"   – stereo, always enabled
 *     Input 1  "Modulator" – stereo, optional (when absent → self-vocode)
 *     Output 0 "Output"    – stereo
 */
class VocodeAudioProcessor : public juce::AudioProcessor,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==========================================================================
    // Compile-time limits that must be consistent with the .cpp
    static constexpr int maxBands = 2048;   // maximum number of vocoder bands
    static constexpr int maxBins = 4097;   // bins for the largest FFT (8192)

    //==========================================================================
    VocodeAudioProcessor();
    ~VocodeAudioProcessor() override;

    //==========================================================================
    // APVTS — public so the editor can attach sliders
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // AudioProcessor overrides
    void prepareToPlay(double sampleRate, int maxSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    // Report a tail so the vocode-reverb decay is heard after input stops
    double getTailLengthSeconds() const override { return 4.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // APVTS listener – handles fftSize / numBands / attack / release changes
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==========================================================================
    // Bandwidth-curve API  (called from the editor, fully thread-safe)
    //   Value range per point: 0.0 (narrowest) … 1.0 (widest)
    //   This controls how far each band's envelope smears into its neighbours.
    void setBandwidthCurveRange(int startBand, int endBand,
        float startVal, float endVal);
    void resetBandwidthCurve();   // fills curve with 0.5 (medium bandwidth)
    void getBandwidthCurve(std::array<float, maxBands>& dest) const;

    //==========================================================================
    // Read-only display data (called from the editor on the message thread)
    //   Writes up to n magnitudes into each output buffer (nullptr = skip).
    //   carrierOut – carrier spectrum (before vocoding)
    //   modOut     – modulator spectrum
    //   vocodeOut  – output spectrum (after vocoding)
    void getDisplayMagnitudes(float* carrierOut,
        float* modOut,
        float* vocodeOut, int n) const;

    int    getNumBins()  const noexcept { return numBins; }
    int    getNumBands() const noexcept { return numBands; }
    int    getFftSize()  const noexcept { return fftSize; }
    double getSampleRate_() const noexcept { return currentSampleRate; }

    //==========================================================================
    // Colour palette (editor can read / write these; saved with state)
    juce::Colour colBackground{ 0xffffffff };
    juce::Colour colCarrier{ 0xff44aaff };
    juce::Colour colModulator{ 0xffff8844 };
    juce::Colour colOutput{ 0xff55eedd };
    juce::Colour colBandwidth{ 0xffffcc33 };
    juce::Colour colGrid{ 0xffaaaaaa };

private:
    //==========================================================================
    // Cached raw parameter pointers (audio-thread access, lock-free)
    std::atomic<float>* pNumBands = nullptr;  // 1 … 2048
    std::atomic<float>* pAttack = nullptr;  // ms
    std::atomic<float>* pRelease = nullptr;  // ms  ("vocode reverb" tail)
    std::atomic<float>* pMorph = nullptr;  // 0 … 1
    std::atomic<float>* pSelfVocode = nullptr;  // 0/1 bool
    std::atomic<float>* pDryWet = nullptr;  // 0 … 1

    //==========================================================================
    // FFT engine state
    int    fftOrder = 11;     // log2(fftSize)
    int    fftSize = 2048;
    int    hopSize = 512;    // fftSize / 4  (75 % overlap)
    int    numBins = 1025;   // fftSize / 2 + 1
    int    numBands = 32;     // current band count (synced to pNumBands)
    double currentSampleRate = 44100.0;

    std::unique_ptr<juce::dsp::FFT> fftProc;
    std::vector<float> window;          // Hann window (fftSize points)

    //==========================================================================
    // Analysis FIFOs
    std::vector<float> carrierFifoL, carrierFifoR;  // stereo carrier
    std::vector<float> modFifo;                      // mono modulator

    int carrierFifoIdx = 0;
    int modFifoIdx = 0;

    //==========================================================================
    // Synthesis overlap-add (OLA) buffers
    std::vector<float> olaL, olaR;     // accumulation buffers (size = fftSize * 4)
    int olaWritePos = 0;               // next hop write position in olaL / olaR
    int olaReadPos = 0;               // next sample read position

    //==========================================================================
    // Per-hop FFT scratch (size = fftSize * 2 each, complex interleaved)
    std::vector<float> fftBufL, fftBufR;   // carrier channels
    std::vector<float> fftBufMod;          // modulator channel
    std::vector<float> synthBufL, synthBufR; // modified carrier (becomes IFFT input)

    //==========================================================================
    // Vocoder state
    std::array<float, maxBands> envState{};  // per-band envelope value (linear RMS)
    std::array<float, maxBands> attCoef{};  // per-band IIR attack  coefficient
    std::array<float, maxBands> relCoef{};  // per-band IIR release coefficient
    // Note: relCoef is the key "vocode reverb" parameter — long release = shimmering tail.

    //==========================================================================
    // Band layout  (recomputed when numBands or fftSize changes)
    std::array<int, maxBands> bandBinStart{};  // first bin belonging to band b
    std::array<int, maxBands> bandBinEnd{};  // last  bin belonging to band b (inclusive)

    void recomputeBandLayout();
    void recomputeEnvCoefs();

    //==========================================================================
    // Bandwidth curve (draw per band on the editor, applied in processHop)
    mutable juce::CriticalSection bwLock;
    std::array<float, maxBands> bwCurve_write{};  // written by editor thread
    std::array<float, maxBands> bwCurve_read{};  // consumed by audio thread
    std::atomic<bool> bwDirty{ false };

    //==========================================================================
    // Display magnitudes (updated each hop, read by editor via timer)
    mutable juce::CriticalSection displayLock;
    std::vector<float> smoothCarrier, smoothMod, smoothVocode;

    //==========================================================================
    // Internal helpers
    void allocateBuffers();
    void createWindow();
    void setFFTSizeInternal(int newFftSize);
    void processHop(bool selfVocode, float morph);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocodeAudioProcessor)
};