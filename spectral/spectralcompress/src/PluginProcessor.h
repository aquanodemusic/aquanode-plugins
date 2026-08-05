#pragma once

#include <JuceHeader.h>

// ============================================================================
//  SpectralCompress  —  aquanode
//
//  An FFT based per-bin compressor in the spirit of Robbert van der Helm's
//  Spectral Compressor, but with the global threshold polynomial (threshold /
//  center / slope / curve) replaced by a hand-drawn per-bin target curve, the
//  way the other aquanode spectral plugins work.
//
//  Signal flow per STFT hop:
//     window -> FFT -> per-bin envelope follower (attack/release)
//            -> per-bin transfer curve (downwards + upwards, each with its own
//               offset / ratio / knee / amount, all relative to the drawn curve)
//            -> per-bin gain -> IFFT -> overlap-add -> dry/wet -> output gain
// ============================================================================

class SpectralCompressAudioProcessor : public juce::AudioProcessor,
    private juce::AudioProcessorValueTreeState::Listener,
    private juce::AsyncUpdater
{
public:
    SpectralCompressAudioProcessor();
    ~SpectralCompressAudioProcessor() override;

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
    // Modes  (mirrors ThresholdMode in the original)
    // -----------------------------------------------------------------------
    enum Mode
    {
        ModeLevel = 0,      // per-bin compression towards the drawn curve
        ModeMatch,          // target curve morphs towards the sidechain spectrum
        ModeSidechain       // envelopes follow the sidechain (spectral ducking)
    };

    // -----------------------------------------------------------------------
    // FFT configuration
    // -----------------------------------------------------------------------
    static constexpr int maxFFTSize = 8192;
    static constexpr int maxBins = maxFFTSize / 2 + 1;   // 4097

    int fftOrder = 11;
    int fftSize = 1 << 11;          // 2048
    int overlapTimes = 4;           // 2 / 4 / 8 / 16
    int hopSize = fftSize / 4;
    int numBins = fftSize / 2 + 1;

    double currentSampleRate = 44100.0;

    float binToHz(int bin) const
    {
        return (numBins > 1) ? bin * (float)(currentSampleRate * 0.5) / (float)(numBins - 1) : 0.0f;
    }

    // -----------------------------------------------------------------------
    // Drawn target curve (dBFS per bin, -100 .. 0)
    // -----------------------------------------------------------------------
    static constexpr float kCurveMinDB = -100.0f;
    static constexpr float kCurveMaxDB = 0.0f;
    static constexpr float kCurveDefaultDB = -45.0f;

    void  setCurveRange(int startBin, int endBin, float startDB, float endDB);
    void  resetCurve(float dB = kCurveDefaultDB);
    void  learnCurveFromInput(float offsetDB);       // snap curve to current spectrum
    void  tiltCurve(float dbPerOctave);              // handy pink-noise style tilt
    void  getCurveData(float* dest, int numToCopy);

    // How far the drawn curve is rotated across the bins, in bins (wrapping).
    // The editor needs this to draw and to map mouse positions back to the
    // unshifted buffer.
    int   getCurveShiftBins() const;

    // -----------------------------------------------------------------------
    // Display data (message thread)
    // -----------------------------------------------------------------------
    void getInputSpectrum(float* dest, int numToCopy);       // normalised linear mag
    void getSidechainSpectrum(float* dest, int numToCopy);   // normalised linear mag
    void getGainReduction(float* dest, int numToCopy);       // dB, negative = duck
    bool sidechainIsConnected() const { return scConnected.load(std::memory_order_relaxed); }

    // -----------------------------------------------------------------------
    // Parameters
    // -----------------------------------------------------------------------
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    int  getModeValue() const { return (int)pMode->load(std::memory_order_relaxed); }

    // -----------------------------------------------------------------------
    // Colours (kept on the processor like in the other aquanode plugins)
    // -----------------------------------------------------------------------
    // Light mode swaps the whole palette; the accent stays blue in both
    void setLightMode(bool shouldBeLight);
    bool isLightMode() const { return lightMode; }
    void applyTheme();

    juce::Colour backgroundColor{ 0xff141618 };
    juce::Colour sidebarColor{ 0xff1b1e21 };
    juce::Colour gridColor{ 0xff3a4046 };
    juce::Colour textColor{ 0xffc8d0d6 };
    juce::Colour accentColor{ 0xff55eedd };
    juce::Colour spectrumColor{ 0xff2f7f74 };
    juce::Colour outputColor{ 0xff8fd6ff };
    juce::Colour sidechainColor{ 0xffdd55bb };
    juce::Colour curveColor{ 0xffffcc33 };
    juce::Colour downColor{ 0xffff6b5c };
    juce::Colour upColor{ 0xff7dff8f };

private:
    bool lightMode = false;

public:

private:
    // -----------------------------------------------------------------------
    // Cached parameter pointers
    // -----------------------------------------------------------------------
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pFFTSize = nullptr;
    std::atomic<float>* pOverlap = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pDelta = nullptr;

    std::atomic<float>* pDownOffset = nullptr;
    std::atomic<float>* pDownRatio = nullptr;
    std::atomic<float>* pDownKnee = nullptr;
    std::atomic<float>* pDownAmount = nullptr;

    std::atomic<float>* pUpOffset = nullptr;
    std::atomic<float>* pUpRatio = nullptr;
    std::atomic<float>* pUpKnee = nullptr;
    std::atomic<float>* pUpAmount = nullptr;

    std::atomic<float>* pScMorph = nullptr;
    std::atomic<float>* pMorphAmount = nullptr;
    std::atomic<float>* pMorphClarity = nullptr;
    std::atomic<float>* pCurveShift = nullptr;
    std::atomic<float>* pVisSmooth = nullptr;
    std::atomic<float>* pScLink = nullptr;
    std::atomic<float>* pStereoLink = nullptr;

    void parameterChanged(const juce::String& paramID, float newValue) override;
    void handleAsyncUpdate() override;
    std::atomic<int> pendingFFTSize{ 0 };
    std::atomic<int> pendingOverlap{ 0 };

    void applyFFTConfig(int newFFTSize, int newOverlap);

    // -----------------------------------------------------------------------
    // STFT machinery
    // -----------------------------------------------------------------------
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<std::vector<float>> inputFifo;    // [ch][fftSize]
    std::vector<std::vector<float>> scFifo;       // [ch][fftSize]
    std::vector<std::vector<float>> fftBuffer;    // [ch][2*fftSize]
    std::vector<std::vector<float>> scBuffer;     // [ch][2*fftSize]
    std::vector<std::vector<float>> outputAccum;  // [ch][4*fftSize]

    std::vector<int> fifoIndex;
    std::vector<int> outputWritePos;

    std::vector<float> window;
    float olaNorm = 1.0f;

    // Block-local copy of the sidechain input, taken before we write to the
    // main bus (the two buses may alias in some hosts)
    juce::AudioBuffer<float> scScratch;

    // Delayed dry path so the mix knob stays phase-aligned with the wet path
    std::vector<std::vector<float>> dryDelay;
    int dryWritePos = 0;
    int dryDelaySize = 0;

    // -----------------------------------------------------------------------
    // Compressor bank state
    // -----------------------------------------------------------------------
    std::vector<std::vector<float>> envelopes;    // [ch][bin], normalised linear mag
    std::vector<std::vector<float>> mainMags;     // [ch][bin], normalised linear mag
    std::vector<std::vector<float>> scMagnitudes; // [ch][bin], normalised linear mag
    std::vector<float> grAccum;                   // [bin] gain reduction accumulator (dB)

    // -----------------------------------------------------------------------
    // Spectral morph (envelope transfer, ported from SpectralCompare)
    // -----------------------------------------------------------------------
    std::vector<std::vector<float>> morphMainEnv;  // [ch][bin] main log envelope
    std::vector<float> morphSideEnv;               // [bin] smoothed sidechain envelope
    std::vector<float> morphEnvTmp;                // [bin] scratch
    std::vector<float> morphMonoSide;              // [bin] channel-averaged sidechain mags
    std::vector<double> morphPrefixSum;            // [bin+1] prefix sum for the envelope
    float envelopeTimingScale = 0.0f;             // fades 0 -> 1 after a reset

    void allocateBuffers();
    void createWindow();
    void resetState();
    void processFrame(int numChannels, bool scActive);

    std::atomic<bool> scConnected{ false };

    // -----------------------------------------------------------------------
    // Curve  (message thread writes, audio thread reads)
    // -----------------------------------------------------------------------
    std::array<float, maxBins> curveDB_write;
    std::array<float, maxBins> curveDB_read;
    juce::CriticalSection curveLock;
    std::atomic<bool> curveDirty{ true };

    // -----------------------------------------------------------------------
    // Display buffers
    // -----------------------------------------------------------------------
    std::vector<float> displayInput;      // smoothed normalised magnitude
    std::vector<float> displaySidechain;
    std::vector<float> displayGR;         // smoothed gain reduction in dB
    juce::CriticalSection displayLock;

    // Smoothed global values
    juce::SmoothedValue<float> gainSmoothed;
    juce::SmoothedValue<float> mixSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompressAudioProcessor)
};
