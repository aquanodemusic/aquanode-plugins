#pragma once

#include <JuceHeader.h>

class VocodeAudioProcessor : public juce::AudioProcessor,
    private juce::AudioProcessorValueTreeState::Listener,
    private juce::AsyncUpdater
{
public:
    VocodeAudioProcessor();
    ~VocodeAudioProcessor() override;

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

    // -----------------------------------------------------------------------
    // APVTS
    // -----------------------------------------------------------------------
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // -----------------------------------------------------------------------
    // FFT state  (read by editor, audio-thread writes)
    // -----------------------------------------------------------------------
    int fftOrder = 11;
    int fftSize = 1 << fftOrder;
    int hopSize = fftSize / 4;
    int numBins = fftSize / 2 + 1;

    // Absolute maximum bins (8192 / 2 + 1)
    static constexpr int maxBins = (1 << 13) / 2 + 1;   // 4097

    void setFFTSize(int newSize);

    // Display data accessors — call from message thread only
    void getMainFFTData(float* out, int numBinsOut);
    void getSidechainFFTData(float* out, int numBinsOut);
    void getMorphedFFTData(float* out, int numBinsOut);

    double currentSampleRate = 44100.0;

    // -----------------------------------------------------------------------
    // Analog band-count choices  (shared with editor for combo population)
    // -----------------------------------------------------------------------
    static constexpr int kNumBandChoices = 16;
    static const int     kBandCounts[kNumBandChoices];
    // = {1,2,4,8,12,16,20,24,32,40,48,56,64,80,96,128}

private:
    // -----------------------------------------------------------------------
    // Raw parameter pointers
    // -----------------------------------------------------------------------
    std::atomic<float>* morphParam = nullptr;
    std::atomic<float>* clarityParam = nullptr;
    std::atomic<float>* smoothingParam = nullptr;
    std::atomic<float>* gateParam = nullptr;   // dBFS threshold
    std::atomic<float>* formantParam = nullptr;   // semitone shift
    std::atomic<float>* analogModeParam = nullptr;   // 0/1 bool
    std::atomic<float>* numBandsParam = nullptr;   // choice index

    // Pending size changes → async → rebuild under suspendProcessing
    std::atomic<int> pendingFFTSize{ 0 };
    std::atomic<int> pendingNumBands{ 0 };   // 1 = dirty flag; rebuild reads param

    void parameterChanged(const juce::String& id, float v) override;
    void handleAsyncUpdate() override;

    // -----------------------------------------------------------------------
    // FFT object
    // -----------------------------------------------------------------------
    std::unique_ptr<juce::dsp::FFT> fftObj;

    // -----------------------------------------------------------------------
    // Analysis FIFOs and scratch frames
    // -----------------------------------------------------------------------
    std::vector<float> mainFifo, sidechainFifo;
    std::vector<float> analysisFrame;   // 2 * fftSize
    std::vector<float> morphFrame;      // 2 * fftSize
    std::vector<float> window;          // Hann, length fftSize

    int mainFifoIndex = 0;
    int sidechainFifoIndex = 0;

    void allocateBuffers();
    void createWindow();

    // -----------------------------------------------------------------------
    // Stereo overlap-add synthesis  (FFT mode only)
    // -----------------------------------------------------------------------
    std::vector<float> outputOLAL, outputOLAR;
    std::vector<float> olaNorm;
    int64_t outputReadPos = 0;

    std::vector<float> synthFifoL, synthFifoR;
    std::vector<float> synthFrameL, synthFrameR;
    std::vector<float> scaleBuf;

    // -----------------------------------------------------------------------
    // Sidechain envelope state  (FFT mode)
    // -----------------------------------------------------------------------
    std::vector<float> instantaneousSidechain;
    std::vector<float> morphEnvelopeSidechain;

    // -----------------------------------------------------------------------
    // Scratch  (pre-allocated)
    // -----------------------------------------------------------------------
    std::vector<float>  scratchSideMags;
    std::vector<float>  scratchMainMags;
    std::vector<float>  scratchMainEnv;
    std::vector<float>  scratchEnvTmp;
    std::vector<double> scratchPrefixSum;

    // -----------------------------------------------------------------------
    // Display double-buffers
    // -----------------------------------------------------------------------
    std::vector<float> smoothedMain;
    std::vector<float> smoothedSidechain;
    std::vector<float> smoothedMorphed;
    juce::CriticalSection displayLock;

    // -----------------------------------------------------------------------
    // Analog filterbank vocoder
    // -----------------------------------------------------------------------

    // Per-band biquad state (DF2T) + envelope follower
    struct AnalogBand
    {
        // Coefficients (b1 is always 0 for a constant-0dB-peak bandpass,
        // stored implicitly; a0 is normalised out)
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0;

        // DF2T state: carrier L, carrier R, modulator
        float cLs1 = 0, cLs2 = 0;
        float cRs1 = 0, cRs2 = 0;
        float ms1 = 0, ms2 = 0;

        float envelope = 0.0f;   // smoothed per-band modulator envelope
        float fc = 0.0f;   // centre frequency (Hz) — for display / formant shift
    };

    static constexpr int kMaxAnalogBands = 128;
    std::array<AnalogBand, kMaxAnalogBands> analogBands{};

    int   currentNumBands = 32;
    float analogFLow = 80.0f;
    float analogFHigh = 18000.0f;
    float analogAttCoeff = 0.0f;   // ~1 ms attack coefficient

    // Pre-allocated per-band scratch (avoids VLAs in hot path)
    std::vector<float> analogCLBuf;
    std::vector<float> analogCRBuf;
    std::vector<float> analogEnvBuf;

    // Output FIFO for analog mode display (feeds smoothedMorphed via FFT)
    std::vector<float> analogOutFifo;
    int                analogOutFifoIdx = 0;
    std::vector<float> analogOutFrame;  // 2 * fftSize scratch

    // Mode-transition bookkeeping (audio thread only)
    bool prevAnalogMode = false;

    void rebuildAnalogBands();
    void processAnalogBlock(juce::AudioBuffer<float>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocodeAudioProcessor)
};