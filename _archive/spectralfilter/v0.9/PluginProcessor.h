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

    // FFT parameters (now dynamic)
    int fftOrder = 11;
    int fftSize  = 1 << fftOrder;  // 2048
    int hopSize  = fftSize / 4;     // 75% overlap
    int numBins  = fftSize / 2 + 1; // 1025
    
    // Maximum bins for any FFT size (8192 = order 13)
    static constexpr int maxBins = (1 << 13) / 2 + 1;  // 4097

    // Get smoothed FFT magnitude data for visualisation (message-thread safe)
    void getFFTData(float* fftDataOut, int numBinsOut);

    // -----------------------------------------------------------------------
    // Filter curve API  (called from the editor / message thread)
    // -----------------------------------------------------------------------

    // Write a linearly-interpolated dB segment into the curve.
    void setFilterCurveRange(int startBin, int endBin, float startDB, float endDB);

    // Reset all bins to 0 dB.
    void resetFilterCurve();

    // Copy the current write-side curve into dest (for the editor to display).
    void getFilterCurve(std::array<float, maxBins>& dest);
    
    // Change FFT size (1024, 2048, 4096, or 8192)
    void setFFTSize(int newSize);
    
    // Color customization
    void setBackgroundColor(juce::Colour color);
    void setCurveColor(juce::Colour color);
    void setGridColor(juce::Colour color);
    void setSpectrumColor(juce::Colour color);
    juce::Colour getBackgroundColor() const { return backgroundColor; }
    juce::Colour getCurveColor() const { return curveColor; }
    juce::Colour getGridColor() const { return gridColor; }
    juce::Colour getSpectrumColor() const { return spectrumColor; }
    
    // Randomize filter curve
    void randomizeFilterCurve();

    // Actual sample rate — updated in prepareToPlay, read by the editor
    double currentSampleRate = 44100.0;

private:
    // -----------------------------------------------------------------------
    // FFT objects
    // -----------------------------------------------------------------------
    std::unique_ptr<juce::dsp::FFT> fftAnalysis;
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;

    // -----------------------------------------------------------------------
    // Per-channel processing buffers
    // -----------------------------------------------------------------------
    std::vector<std::vector<float>> inputFifo;     // ring buffer of input samples
    std::vector<std::vector<float>> analysisFrame; // windowed frame
    std::vector<std::vector<float>> fftBuffer;     // FFT work buffer (interleaved Re/Im)
    std::vector<std::vector<float>> outputAccum;   // overlap-add accumulator

    std::vector<float> window;

    std::vector<int> inputFifoIndex;
    std::vector<int> outputWritePos;

    // -----------------------------------------------------------------------
    // Filter curve — double-buffer with atomic dirty flag to keep the audio
    // thread lock-free during per-bin processing.
    //
    //   Message thread  : writes into filterCurveDB_write under filterCurveLock,
    //                     then sets curveIsDirty = true.
    //   Audio thread    : at the top of each FFT frame checks curveIsDirty;
    //                     if set, acquires the lock briefly to copy the write
    //                     buffer into filterCurveDB_read, then rebuilds
    //                     linearGainCache[] (no more lock needed for the loop).
    // -----------------------------------------------------------------------
    std::array<float, maxBins> filterCurveDB_write; // owned by message thread
    std::array<float, maxBins> filterCurveDB_read;  // owned by audio thread
    std::array<float, maxBins> linearGainCache;     // pre-computed linear gains

    juce::CriticalSection filterCurveLock;  // guards filterCurveDB_write only
    std::atomic<bool>     curveIsDirty { false };

    // Recompute linearGainCache from filterCurveDB_read (audio thread, no lock needed)
    void rebuildLinearGainCache();
    
    // Allocate/reallocate all FFT buffers for current fftSize
    void allocateBuffers();

    // -----------------------------------------------------------------------
    // Display data
    // -----------------------------------------------------------------------
    std::vector<float> smoothedSpectrumMagnitudes;
    juce::CriticalSection fftDisplayLock;

    // -----------------------------------------------------------------------
    // Core DSP
    // -----------------------------------------------------------------------
    void performSpectralFilter(int channel);
    void createWindow();
    
    // -----------------------------------------------------------------------
    // UI Colors
    // -----------------------------------------------------------------------
    juce::Colour backgroundColor{ 0xffffffff };
    juce::Colour curveColor{ 0xff55eedd };
    juce::Colour gridColor{ 0xffaaaaaa };
    juce::Colour spectrumColor { 0xff669999 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFilterAudioProcessor)
};
