#pragma once

#include <JuceHeader.h>

class AnyFMAudioProcessor : public juce::AudioProcessor
{
public:
    AnyFMAudioProcessor();
    ~AnyFMAudioProcessor() override;

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

    // Parameters
    juce::AudioParameterFloat* modulationIndex;
    juce::AudioParameterFloat* modulatorGain;
    juce::AudioParameterFloat* carrierGain;
    juce::AudioParameterFloat* dryWet;
    juce::AudioParameterChoice* fmType;

    // Metering
    float getCarrierLevel() const { return carrierLevel.load(); }
    float getModulatorLevel() const { return modulatorLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }

private:
    // Sample rate
    double currentSampleRate = 44100.0;

    // Phase accumulators for phase modulation
    juce::AudioBuffer<float> delayBuffer;
    int writePosition = 0;

    // Smoothing filters for modulation index
    std::vector<juce::SmoothedValue<float>> modulationIndexSmoothed;

    // Level metering
    std::atomic<float> carrierLevel{ 0.0f };
    std::atomic<float> modulatorLevel{ 0.0f };
    std::atomic<float> outputLevel{ 0.0f };

    // Processing
    void performPhaseModulation(juce::AudioBuffer<float>& carrier,
        const juce::AudioBuffer<float>& modulator,
        int numSamples);

    void performFrequencyModulation(juce::AudioBuffer<float>& carrier,
        const juce::AudioBuffer<float>& modulator,
        int numSamples);

    void updateMeters(const juce::AudioBuffer<float>& carrier,
        const juce::AudioBuffer<float>& modulator,
        const juce::AudioBuffer<float>& output);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnyFMAudioProcessor)
};