#pragma once

#include <JuceHeader.h>

class SpectralGateAudioProcessor : public juce::AudioProcessor
{
public:
    SpectralGateAudioProcessor();
    ~SpectralGateAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool   acceptsMidi() const override;
    bool   producesMidi() const override;
    bool   isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── FFT size (call from UI thread; suspends processing internally) ────────
    // order: 9 → 512, 10 → 1024, 11 → 2048, 12 → 4096
    void setFFTOrder(int order);
    int  getFFTOrder()       const { return currentFFTOrder; }
    int  getFFTSize()        const { return currentFFTSize; }
    // Number of unique FFT output bins (DC … Nyquist), equals fftSize/2 + 1
    int  getNumBins()        const { return currentFFTSize / 2 + 1; }

    // ── Visualisation data (thread-safe copies) ───────────────────────────────
    void getFFTData(std::vector<float>& out);
    void getGateCurve(std::vector<float>& out);

    // ── Gate-curve editing (call from UI/message thread) ─────────────────────
    // Draws a straight segment from binStart→valStart to binEnd→valEnd.
    // Values are normalised 0 (pass all) … 1 (gate all).
    void setGateCurveSegment(int binStart, float valStart,
        int binEnd, float valEnd);
    void resetGateCurve(float value = 0.0f);

    // ── Parameter helpers ─────────────────────────────────────────────────────
    bool  isIntervalInverted() const { return *invertIntervalParameter >= 0.5f; }
    bool  isGateInverted()     const { return *invertGateParameter >= 0.5f; }
    float getLowerFreqBin()    const { return *lowerFreqParameter; }
    float getUpperFreqBin()    const { return *upperFreqParameter; }

    // ── APVTS ─────────────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* lowerFreqParameter = nullptr;
    std::atomic<float>* upperFreqParameter = nullptr;
    std::atomic<float>* invertIntervalParameter = nullptr;
    std::atomic<float>* invertGateParameter = nullptr;

private:
    // ── Runtime FFT config ────────────────────────────────────────────────────
    int currentFFTOrder = 11;
    int currentFFTSize = 2048;
    int currentHopSize = 512;

    std::unique_ptr<juce::dsp::FFT> fftAnalysis;
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;

    // ── Per-channel processing buffers ────────────────────────────────────────
    std::vector<std::vector<float>> inputFifo;
    std::vector<std::vector<float>> analysisFrame;
    std::vector<std::vector<float>> fftBuffer;
    std::vector<std::vector<float>> outputAccum;

    std::vector<int> inputFifoIndex;
    std::vector<int> outputWritePos;
    std::vector<int> grainCounter;

    // ── Shared buffers (channel 0 feeds display) ──────────────────────────────
    std::vector<float> window;
    std::vector<float> smoothedSpectrumMagnitudes;
    std::vector<float> gateCurve;        // numBins values, 0-1 threshold per bin

    juce::CriticalSection fftLock;       // guards smoothedSpectrumMagnitudes & gateCurve

    // ── Helpers ───────────────────────────────────────────────────────────────
    void allocateBuffers(int numChannels);
    void performSpectralGate(int channel);
    void createWindow();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralGateAudioProcessor)
};