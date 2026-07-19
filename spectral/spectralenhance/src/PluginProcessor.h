#pragma once

#include <JuceHeader.h>

class SpectralEnhanceAudioProcessor : public juce::AudioProcessor
{
public:
    SpectralEnhanceAudioProcessor();
    ~SpectralEnhanceAudioProcessor() override;

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

    // Accessors for editor
    bool isAttenuateMode() const { return *attenuateParameter >= 0.5f; }
    float getLowerFreqBin() const { return *lowerFreqParameter; }
    float getUpperFreqBin() const { return *upperFreqParameter; }
    float getMagnitudeThreshold() const { return *magnitudeParameter; }
    float getSlope() const { return *slopeParameter * 2.0f - 1.0f; }  // Convert 0-1 to -1 to +1

    // Parameters with automation support
    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* lowerFreqParameter  = nullptr;
    std::atomic<float>* upperFreqParameter  = nullptr;
    std::atomic<float>* magnitudeParameter  = nullptr;
    std::atomic<float>* slopeParameter      = nullptr;
    std::atomic<float>* attenuateParameter  = nullptr;

private:
    std::unique_ptr<juce::dsp::FFT> fftAnalysis;
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;

    // Processing buffers (per channel)
    std::vector<std::vector<float>> inputFifo;
    std::vector<std::vector<float>> analysisFrame;
    std::vector<std::vector<float>> fftBuffer;
    std::vector<std::vector<float>> outputAccum;

    std::vector<float> window;

    // Display data
    std::vector<float> spectrumMagnitudes;
    std::vector<float> smoothedSpectrumMagnitudes;
    juce::CriticalSection fftLock;

    // Processing state (per channel)
    std::vector<int> inputFifoIndex;
    std::vector<int> outputWritePos;
    std::vector<int> grainCounter;

    void performSpectralEnhance(int channel);
    void createWindow();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralEnhanceAudioProcessor)
};
