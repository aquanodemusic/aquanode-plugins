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

    // FFT parameters
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    static constexpr int hopSize = fftSize / 4;   // 75% overlap

    // Get FFT data for visualization
    void getFFTData(float* fftData, int numBins);
    
    // Get gating info for visualization (which bins are being kept/removed)
    bool isInverted() const { return *invertParameter >= 0.5f; }
    float getLowerFreqBin() const { return *lowerFreqParameter; }
    float getUpperFreqBin() const { return *upperFreqParameter; }
    float getMagnitudeThreshold() const { return *magnitudeParameter; }
    float getSlope() const { return *slopeParameter * 2.0f - 1.0f; }  // Convert 0-1 to -1 to +1

    // Parameters with automation support
    juce::AudioProcessorValueTreeState parameters;
    
    std::atomic<float>* lowerFreqParameter = nullptr;
    std::atomic<float>* upperFreqParameter = nullptr;
    std::atomic<float>* magnitudeParameter = nullptr;
    std::atomic<float>* slopeParameter = nullptr;
    std::atomic<float>* invertParameter = nullptr;

private:
    std::unique_ptr<juce::dsp::FFT> fftAnalysis;
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;
    
    // Processing buffers (per channel)
    std::vector<std::vector<float>> inputFifo;      // Accumulates input samples
    std::vector<std::vector<float>> analysisFrame;  // Windowed frame for FFT
    std::vector<std::vector<float>> fftBuffer;      // FFT work buffer
    std::vector<std::vector<float>> outputAccum;    // Overlap-add accumulator
    
    std::vector<float> window;

    // Display data
    std::vector<float> spectrumMagnitudes;
    std::vector<float> smoothedSpectrumMagnitudes;  // Smoothed version for display
    juce::CriticalSection fftLock;

    // Processing state (per channel)
    std::vector<int> inputFifoIndex;
    std::vector<int> outputWritePos;
    std::vector<int> grainCounter;

    void performSpectralGate(int channel);
    void createWindow();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralGateAudioProcessor)
};
