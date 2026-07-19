#pragma once

#include <JuceHeader.h>

class SpectralCompareAudioProcessor : public juce::AudioProcessor
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
    // FFT parameters
    // -----------------------------------------------------------------------
    int fftOrder = 11;
    int fftSize  = 1 << fftOrder;   // 2048 default
    int hopSize  = fftSize / 4;
    int numBins  = fftSize / 2 + 1;
    static constexpr int maxBins = (1 << 13) / 2 + 1;  // 4097 (FFT 8192)

    void setFFTSize(int newSize);

    // Smoothed magnitude data for display — call from message thread
    void getMainFFTData    (float* out, int numBinsOut);
    void getSidechainFFTData(float* out, int numBinsOut);

    // Freeze: when true the editor stops updating its display snapshot
    std::atomic<bool> freezeMain{ false };

    // -----------------------------------------------------------------------
    // Colors
    // -----------------------------------------------------------------------
    void   setBackgroundColor(juce::Colour c);
    void   setGridColor      (juce::Colour c);
    void   setMainSpectrumColor    (juce::Colour c);
    void   setSidechainSpectrumColor(juce::Colour c);
    void   setSidebarColor   (juce::Colour c);
    juce::Colour getBackgroundColor()       const { return backgroundColor; }
    juce::Colour getGridColor()             const { return gridColor; }
    juce::Colour getMainSpectrumColor()     const { return mainSpectrumColor; }
    juce::Colour getSidechainSpectrumColor()const { return sidechainSpectrumColor; }
    juce::Colour getSidebarColor()          const { return sidebarColor; }
    void resetColors();

    double currentSampleRate = 44100.0;

private:
    // -----------------------------------------------------------------------
    // FFT objects (analysis only — no synthesis; audio passes through dry)
    // -----------------------------------------------------------------------
    std::unique_ptr<juce::dsp::FFT> fftMain;
    std::unique_ptr<juce::dsp::FFT> fftSidechain;

    // Per-channel input FIFOs and analysis frames for two signal paths
    // Index 0 = main L+R averaged, Index 1 = sidechain L+R averaged
    std::vector<float> mainFifo;
    std::vector<float> sidechainFifo;
    std::vector<float> analysisFrame;

    std::vector<float> window;
    int mainFifoIndex      = 0;
    int sidechainFifoIndex = 0;

    void allocateBuffers();
    void createWindow();

    // -----------------------------------------------------------------------
    // Display double-buffers
    // -----------------------------------------------------------------------
    std::vector<float> smoothedMain;
    std::vector<float> smoothedSidechain;
    juce::CriticalSection displayLock;

    // -----------------------------------------------------------------------
    // Colors
    // -----------------------------------------------------------------------
    juce::Colour backgroundColor      { 0xffffffff };
    juce::Colour gridColor            { 0xff444444 };
    juce::Colour mainSpectrumColor    { 0xff55eedd };
    juce::Colour sidechainSpectrumColor{ 0xffff6644 };
    juce::Colour sidebarColor         { 0xff1a1a1a };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompareAudioProcessor)
};
