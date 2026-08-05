#pragma once

#include <JuceHeader.h>

class SpectralFilterAudioProcessor : public juce::AudioProcessor
{
public:
    SpectralFilterAudioProcessor();
    ~SpectralFilterAudioProcessor() override;

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
    // FFT parameters (dynamic)
    // -----------------------------------------------------------------------
    int fftOrder = 11;
    int fftSize = 1 << fftOrder;   // 2048 default
    int hopSize = fftSize / 4;
    int numBins = fftSize / 2 + 1;
    static constexpr int maxBins = (1 << 13) / 2 + 1;  // 4097 (for FFT size 8192)

    void setFFTSize(int newSize);

    // Get smoothed FFT magnitude data for visualisation (message-thread safe)
    void getFFTData(float* out, int numBinsOut);

    // -----------------------------------------------------------------------
    // Filter curve  (dB, range -144..+24)
    // -----------------------------------------------------------------------
    void setFilterCurveRange(int startBin, int endBin, float startDB, float endDB);
    void resetFilterCurve();
    void getFilterCurve(std::array<float, maxBins>& dest);
    void setFilterCurveShifted(const std::array<float, maxBins>& baseline, int shift);
    void randomizeFilterCurve();

    // -----------------------------------------------------------------------
    // Phase curve  (radians, range -pi..+pi)
    // -----------------------------------------------------------------------
    void setPhaseCurveRange(int startBin, int endBin, float startRad, float endRad);
    void resetPhaseCurve();
    void getPhaseCurve(std::array<float, maxBins>& dest);
    void setPhaseCurveShifted(const std::array<float, maxBins>& baseline, int shift);
    void randomizePhaseCurve();

    // -----------------------------------------------------------------------
    // Freq-shift curve  (per-bin offset in bins, range -numBins/2..+numBins/2)
    // -----------------------------------------------------------------------
    void setFreqShiftCurveRange(int startBin, int endBin, float startOff, float endOff);
    void resetFreqShiftCurve();
    void getFreqShiftCurve(std::array<float, maxBins>& dest);
    void setFreqShiftCurveShifted(const std::array<float, maxBins>& baseline, int shift);
    void randomizeFreqShiftCurve();

    // -----------------------------------------------------------------------
    // Pan curve  (stereo pan per bin, range -1..+1; 0 = centre)
    // -----------------------------------------------------------------------
    void setPanCurveRange(int startBin, int endBin, float startPan, float endPan);
    void resetPanCurve();
    void getPanCurve(std::array<float, maxBins>& dest);
    void setPanCurveShifted(const std::array<float, maxBins>& baseline, int shift);
    void randomizePanCurve();

    // -----------------------------------------------------------------------
    // Color API
    // -----------------------------------------------------------------------
    void setBackgroundColor(juce::Colour c);
    void setCurveColor(juce::Colour c);
    void setGridColor(juce::Colour c);
    void setSpectrumColor(juce::Colour c);
    void setPhaseColor(juce::Colour c);
    void setFreqShiftColor(juce::Colour c);
    void setPanColor(juce::Colour c);
    juce::Colour getBackgroundColor()  const { return backgroundColor; }
    juce::Colour getCurveColor()       const { return curveColor; }
    juce::Colour getGridColor()        const { return gridColor; }
    juce::Colour getSpectrumColor()    const { return spectrumColor; }
    juce::Colour getPhaseColor()       const { return phaseColor; }
    juce::Colour getFreqShiftColor()   const { return freqShiftColor; }
    juce::Colour getPanColor()         const { return panColor; }
    void resetColors();

    // -----------------------------------------------------------------------
    // Export
    // -----------------------------------------------------------------------
    void exportImpulseResponse(const juce::File& outputFile);

    // -----------------------------------------------------------------------
    // Global bin shift  (shifts all bins by same amount — the original effect)
    // -----------------------------------------------------------------------
    std::atomic<int>  binShiftAmount{ 0 };
    std::atomic<bool> binShiftWrap{ true };
    std::atomic<bool> wetOnly{ false };

    // -----------------------------------------------------------------------
    // APVTS — exposes the four strength knobs to DAW automation
    // -----------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Raw pointers for lock-free audio-thread reads (set once after construction)
    std::atomic<float>* pFilterStrength{ nullptr };
    std::atomic<float>* pPhaseStrength { nullptr };
    std::atomic<float>* pFreqStrength  { nullptr };
    std::atomic<float>* pPanStrength   { nullptr };

    // -----------------------------------------------------------------------
    // Auto-shift state — all public so editor can reset accumulators
    // -----------------------------------------------------------------------
    std::atomic<bool>  autoShiftFilter{ false };
    std::atomic<float> autoShiftFilterSpeed{ 10.0f };
    float              autoShiftFilterAccum{ 0.0f };

    std::atomic<bool>  autoShiftBin{ false };
    std::atomic<float> autoShiftBinSpeed{ 10.0f };
    float              autoShiftBinAccum{ 0.0f };
    int                autoShiftBinPos{ 0 };

    std::atomic<bool>  autoShiftPhase{ false };
    std::atomic<float> autoShiftPhaseSpeed{ 10.0f };
    float              autoShiftPhaseAccum{ 0.0f };

    std::atomic<bool>  autoShiftFreq{ false };
    std::atomic<float> autoShiftFreqSpeed{ 10.0f };
    float              autoShiftFreqAccum{ 0.0f };

    std::atomic<bool>  autoShiftPan{ false };
    std::atomic<float> autoShiftPanSpeed{ 10.0f };
    float              autoShiftPanAccum{ 0.0f };

    // Actual sample rate
    double currentSampleRate = 44100.0;

private:
    // -----------------------------------------------------------------------
    // FFT objects
    // -----------------------------------------------------------------------
    std::unique_ptr<juce::dsp::FFT> fftAnalysis;
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;

    // -----------------------------------------------------------------------
    // Per-channel buffers
    // -----------------------------------------------------------------------
    std::vector<std::vector<float>> inputFifo;
    std::vector<std::vector<float>> analysisFrame;
    std::vector<std::vector<float>> fftBuffer;   // freq-domain between forward FFT and IFFT
    std::vector<std::vector<float>> outputAccum;
    std::vector<std::vector<float>> drySnapshot; // for wetOnly subtraction

    std::vector<float> window;
    std::vector<int>   inputFifoIndex;
    std::vector<int>   outputWritePos;

    void allocateBuffers();
    void createWindow();

    // -----------------------------------------------------------------------
    // Curve double-buffers  (write = message thread, read = audio thread)
    // -----------------------------------------------------------------------
    std::array<float, maxBins> filterCurveDB_write, filterCurveDB_read, linearGainCache;
    juce::CriticalSection filterCurveLock;
    std::atomic<bool>     curveIsDirty{ false };
    void rebuildLinearGainCache();

    std::array<float, maxBins> phaseCurve_write, phaseCurve_read;
    juce::CriticalSection phaseCurveLock;
    std::atomic<bool>     phaseIsDirty{ false };

    std::array<float, maxBins> freqShiftCurve_write, freqShiftCurve_read;
    juce::CriticalSection freqShiftCurveLock;
    std::atomic<bool>     freqShiftIsDirty{ false };

    std::array<float, maxBins> panCurve_write, panCurve_read;
    juce::CriticalSection panCurveLock;
    std::atomic<bool>     panIsDirty{ false };

    // -----------------------------------------------------------------------
    // Display
    // -----------------------------------------------------------------------
    std::vector<float> smoothedSpectrumMagnitudes;
    juce::CriticalSection fftDisplayLock;

    // -----------------------------------------------------------------------
    // DSP pipeline helpers
    // -----------------------------------------------------------------------
    // Called per-channel: forward FFT, gain, phase, wetOnly snapshot
    void stageForwardProcess(int channel);
    // Called once after all channels: per-bin freq shift + global bin shift + pan
    void stageFreqShiftAndPan(int numActiveChannels);
    // Update display from ch0 fftBuffer (call after stageFreqShiftAndPan)
    void stageUpdateDisplay();
    // Called per-channel: IFFT + overlap-add
    void stageInverseAccum(int channel);

    // -----------------------------------------------------------------------
    // Colors
    // -----------------------------------------------------------------------
    juce::Colour backgroundColor{ 0xffffffff };
    juce::Colour curveColor{ 0xff55eedd };
    juce::Colour gridColor{ 0xffaaaaaa };
    juce::Colour spectrumColor{ 0xff336655 };
    juce::Colour phaseColor{ 0xffdd55bb };
    juce::Colour freqShiftColor{ 0xff44ccff };
    juce::Colour panColor{ 0xffffcc33 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFilterAudioProcessor)
};