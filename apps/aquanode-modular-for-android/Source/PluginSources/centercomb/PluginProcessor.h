#pragma once

#include <JuceHeader.h>

class CenterCombAudioProcessor : public juce::AudioProcessor
{
public:
    CenterCombAudioProcessor();
    ~CenterCombAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

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

    juce::AudioProcessorValueTreeState apvts;

    struct SmoothedParams {
        juce::LinearSmoothedValue<float> gain, freq, q, damp, spreadHz, spreadRatio;
    };

    SmoothedParams smoothedParams;

private:

    enum class FreqMode
    {
        Regular = 0,
        Wrap = 1,
        Mirror = 2
    };

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int maxFilters = 129; 
    std::array<std::array<juce::dsp::IIR::Filter<float>, maxFilters>, 2> filters;

    float lastFreqs[maxFilters];

    double currentSampleRate = 44100.0;

    float currentSmoothingMs = -1.0f; // Initialize to -1 to trigger first update

    float lastSampleRate = 44100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CenterCombAudioProcessor)
};