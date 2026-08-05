#pragma once

#include <JuceHeader.h>

class SpectralCompareAudioProcessor : public juce::AudioProcessor,
    private juce::AudioProcessorValueTreeState::Listener,
    private juce::AsyncUpdater
{
public:
    SpectralCompareAudioProcessor();
    ~SpectralCompareAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // -----------------------------------------------------------------------
    // APVTS
    // -----------------------------------------------------------------------
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // -----------------------------------------------------------------------
    // FFT parameters
    // -----------------------------------------------------------------------
    int fftOrder = 11;
    int fftSize = 1 << fftOrder;
    int hopSize = fftSize / 4;
    int numBins = fftSize / 2 + 1;
    static constexpr int maxBins = (1 << 13) / 2 + 1;  // 4097

    void setFFTSize(int newSize);

    // Display data accessors — call from message thread only
    void getMainFFTData(float* out, int numBinsOut);
    void getSidechainFFTData(float* out, int numBinsOut);
    void getMorphedFFTData(float* out, int numBinsOut);
    void getOutputFFTData(float* out, int numBinsOut);

    float getMorphAmount() const { return morphParam->load(std::memory_order_relaxed); }

    // -----------------------------------------------------------------------
    // Spectral filter curves (message-thread write, audio-thread apply)
    // -----------------------------------------------------------------------
    static constexpr float kFilterDBRange = 24.0f;   // ±24 dB draw range

    void setMainFilterCurveRange(int startBin, int endBin, float startDB, float endDB);
    void setSidechainFilterCurveRange(int startBin, int endBin, float startDB, float endDB);
    void setGateFilterCurveRange(int startBin, int endBin, float startDB, float endDB);
    void setEnhanceFilterCurveRange(int startBin, int endBin, float startDB, float endDB);
    void resetMainFilterCurve();
    void resetSidechainFilterCurve();
    void resetGateFilterCurve();
    void resetEnhanceFilterCurve();
    void getMainFilterCurveData(float* out, int numBinsOut);
    void getSidechainFilterCurveData(float* out, int numBinsOut);
    void getGateFilterCurveData(float* out, int numBinsOut);
    void getEnhanceFilterCurveData(float* out, int numBinsOut);

    // -----------------------------------------------------------------------
    // Colors
    // -----------------------------------------------------------------------
    void setBackgroundColor(juce::Colour c);
    void setGridColor(juce::Colour c);
    void setMainSpectrumColor(juce::Colour c);
    void setSidechainSpectrumColor(juce::Colour c);
    void setDeltaColor(juce::Colour c);
    void setMorphColor(juce::Colour c);
    void setSidebarColor(juce::Colour c);
    void setOutputSpectrumColor(juce::Colour c);
    void setTextColor(juce::Colour c);

    juce::Colour getBackgroundColor()        const { return backgroundColor; }
    juce::Colour getGridColor()              const { return gridColor; }
    juce::Colour getMainSpectrumColor()      const { return mainSpectrumColor; }
    juce::Colour getSidechainSpectrumColor() const { return sidechainSpectrumColor; }
    juce::Colour getDeltaColor()             const { return deltaColor; }
    juce::Colour getMorphColor()             const { return morphColor; }
    juce::Colour getSidebarColor()           const { return sidebarColor; }
    juce::Colour getOutputSpectrumColor()    const { return outputSpectrumColor; }
    juce::Colour getTextColor()              const { return textColor; }

    void resetColors();

    double currentSampleRate = 44100.0;

private:
    // -----------------------------------------------------------------------
    // Cached raw parameter pointers
    // -----------------------------------------------------------------------
    std::atomic<float>* morphParam = nullptr;
    std::atomic<float>* envelopeWidthParam = nullptr;

    // Visual-speed sliders (IIR coefficient for display smoothing)
    std::atomic<float>* smoothMainParam = nullptr;
    std::atomic<float>* smoothSidechainParam = nullptr;
    std::atomic<float>* smoothMorphParam = nullptr;
    std::atomic<float>* smoothDeltaParam = nullptr;
    std::atomic<float>* smoothOutputParam = nullptr;

    // Audio-smooth toggles (bool: also use the speed value for audio IIR)
    std::atomic<float>* smoothMainAudioParam = nullptr;  // default false
    std::atomic<float>* smoothSideAudioParam = nullptr;  // default false
    std::atomic<float>* smoothMorphAudioParam = nullptr;  // default true (preserves old behaviour)
    std::atomic<float>* smoothDeltaAudioParam = nullptr;  // default false

    // Hear-delta-only toggle
    std::atomic<float>* hearDeltaParam = nullptr;

    // Monitor sidechain through output
    std::atomic<float>* monitorSideParam = nullptr;

    // Gate params
    std::atomic<float>* gateEnableParam = nullptr;
    std::atomic<float>* gateBinStartParam = nullptr;
    std::atomic<float>* gateBinEndParam = nullptr;

    // Enhance params
    std::atomic<float>* enhanceEnableParam = nullptr;
    std::atomic<float>* enhanceAttenuateParam = nullptr;
    std::atomic<float>* enhanceBinStartParam = nullptr;
    std::atomic<float>* enhanceBinEndParam = nullptr;

    // Pending FFT size (parameter change → async → setFFTSize)
    std::atomic<int> pendingFFTSize{ 0 };
    void parameterChanged(const juce::String& paramID, float newValue) override;
    void handleAsyncUpdate() override;

    // -----------------------------------------------------------------------
    // FFT objects + analysis buffers
    // -----------------------------------------------------------------------
    std::unique_ptr<juce::dsp::FFT> fftMain;
    std::unique_ptr<juce::dsp::FFT> fftSidechain;

    std::vector<float> mainFifo;
    std::vector<float> sidechainFifo;
    std::vector<float> analysisFrame;   // 2*fftSize scratch (forward FFT in-place)
    std::vector<float> preFftFrame;     // fftSize windowed time-domain snapshot
    std::vector<float> morphFrame;      // 2*fftSize: morphed/delta spectrum, then IFFT
    std::vector<float> window;

    int mainFifoIndex = 0;
    int sidechainFifoIndex = 0;

    void allocateBuffers();
    void createWindow();

    // -----------------------------------------------------------------------
    // Spectral FX helpers (operate in-place on packed re/im FFT buffer)
    // -----------------------------------------------------------------------
    void applySpectralGateToFrame(float* fftBuf, int nb);
    void applySpectralEnhanceToFrame(float* fftBuf, int nb);

    // -----------------------------------------------------------------------
    // OLA synthesis — per-channel stereo
    // -----------------------------------------------------------------------
    std::vector<float> outputOLAL;      // left  OLA accumulator
    std::vector<float> outputOLAR;      // right OLA accumulator
    std::vector<float> olaNorm;
    int64_t outputReadPos = 0;

    // Per-channel synthesis FIFOs (filled in sync with mainFifo)
    std::vector<float> synthFifoL;
    std::vector<float> synthFifoR;
    // Scratch frames for per-channel FFT (2*fftSize each)
    std::vector<float> synthFrameL;
    std::vector<float> synthFrameR;
    // Per-bin magnitude scale computed from mono morph/FX path
    std::vector<float> scaleBuf;

    // Pre-allocated scratch buffers for processBlock (avoid large stack VLAs)
    std::vector<float>  scratchSideMags;
    std::vector<float>  scratchMainMags;
    std::vector<float>  scratchMainEnv;
    std::vector<float>  scratchEnvInput;
    std::vector<float>  scratchEnvTmp;
    std::vector<double> scratchPrefixSum;   // for computeLogEnvelope (size numBins+1)

    // -----------------------------------------------------------------------
    // Sidechain OLA synthesis (for monitor-side with spectral filter)
    // -----------------------------------------------------------------------
    std::vector<float> synthSideFifoL;
    std::vector<float> synthSideFifoR;
    std::vector<float> synthSideFrameL;
    std::vector<float> synthSideFrameR;
    std::vector<float> sideOLAL;
    std::vector<float> sideOLAR;

    // Fade gain for the monitor-side mix (0..1).  Ramps over 0.25 ms to avoid
    // clicks when the Monitor Side button is toggled.  D5 is skipped entirely
    // once this reaches 0 and monitorSide is off, saving 4 FFTs per hop.
    float sideMonitorGain = 0.0f;
    float sideMonitorRampRate = 1.0f / (0.00025f * 44100.0f);   // updated in prepareToPlay

    // -----------------------------------------------------------------------
    // Audio-domain smoothing helper buffers (size numBins each)
    // -----------------------------------------------------------------------
    std::vector<float> smoothedMainEnv;       // IIR-smoothed main spectral envelope for morph
    std::vector<float> smoothedSideForAudio;  // IIR-smoothed sidechain instantaneous mags
    std::vector<float> smoothedDeltaSpec;     // IIR-smoothed delta magnitudes

    // -----------------------------------------------------------------------
    // Output-spectrum FIFO (for post-FX display)
    // -----------------------------------------------------------------------
    std::vector<float> outputFifo;
    std::vector<float> outputAnalysisFrame;   // 2*fftSize
    int                outputFifoIndex = 0;

    // -----------------------------------------------------------------------
    // Display double-buffers  (protected by displayLock)
    // -----------------------------------------------------------------------
    std::vector<float> smoothedMain;
    std::vector<float> smoothedSidechain;
    std::vector<float> smoothedMorphed;
    std::vector<float> smoothedOutput;
    std::vector<float> instantaneousSidechain;
    std::vector<float> morphEnvelopeSidechain;
    juce::CriticalSection displayLock;

    // -----------------------------------------------------------------------
    // Colors
    // -----------------------------------------------------------------------
    juce::Colour backgroundColor{ 0xff1a1a1a };
    juce::Colour gridColor{ 0xff444444 };
    juce::Colour mainSpectrumColor{ 0xff00ff00 };
    juce::Colour sidechainSpectrumColor{ 0xffff00ff };
    juce::Colour deltaColor{ 0xffffff00 };
    juce::Colour morphColor{ 0xff00e5ff };
    juce::Colour sidebarColor{ 0xff1a1a1a };
    juce::Colour outputSpectrumColor{ 0xff4477ff };
    juce::Colour textColor{ 0xffcccccc };

    // -----------------------------------------------------------------------
    // Spectral filter curves — double-buffered, locked handoff
    // -----------------------------------------------------------------------
    std::array<float, maxBins> mainFilterCurve_write{};
    std::array<float, maxBins> sidechainFilterCurve_write{};
    std::array<float, maxBins> gateFilterCurve_write{};
    std::array<float, maxBins> enhanceFilterCurve_write{};
    std::array<float, maxBins> mainFilterCurve_read{};
    std::array<float, maxBins> sidechainFilterCurve_read{};
    std::array<float, maxBins> gateFilterCurve_read{};
    std::array<float, maxBins> enhanceFilterCurve_read{};
    std::array<float, maxBins> mainFilterGain{};
    std::array<float, maxBins> sidechainFilterGain{};
    std::array<float, maxBins> gateFilterGain{};      // cached pow(10, gateFilterCurve_read[i]/20)
    std::array<float, maxBins> enhanceFilterGain{};   // cached pow(10, enhanceFilterCurve_read[i]/20)
    juce::CriticalSection filterCurveLock;
    std::atomic<bool> mainFilterDirty{ false };
    std::atomic<bool> sidechainFilterDirty{ false };
    std::atomic<bool> gateFilterDirty{ false };
    std::atomic<bool> enhanceFilterDirty{ false };
    std::atomic<bool> mainFilterIsFlat{ true };  // true when all main filter curve bins == 0 dB

    void rebuildFilterGain(std::array<float, maxBins>& gainCache,
        const std::array<float, maxBins>& curve, int nb);
    void applyFilterGains(float* fftBuf, int nb,
        const std::array<float, maxBins>& gain);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompareAudioProcessor)
};