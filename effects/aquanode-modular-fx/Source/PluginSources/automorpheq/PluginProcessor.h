#pragma once

#include <JuceHeader.h>

enum class MorphWaveform
{
    Sine = 0,
    Triangle,
    RampUp,
    RampDown,
    Square,
    Tanh,
    Random
};

class AutoMorphEQAudioProcessor : public juce::AudioProcessor
{
public:
    AutoMorphEQAudioProcessor();
    ~AutoMorphEQAudioProcessor() override;

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

    float getMorphPosition(int bandIndex)
    {
        if (bandIndex >= 0 && bandIndex < 7)
            return (float)oscPhases[bandIndex];
        return 0.0f;
    }
    
    float getRandomCurrentValue(int bandIndex)
    {
        if (bandIndex >= 0 && bandIndex < 7)
            return randomCurrentValue[bandIndex];
        return 0.0f;
    }
    
    float getRandomTargetValue(int bandIndex)
    {
        if (bandIndex >= 0 && bandIndex < 7)
            return randomTargetValue[bandIndex];
        return 0.0f;
    }

    void randomizeAllParameters();

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float applyWaveformShape(float phase, MorphWaveform waveform, int bandIndex);

    struct FilterBand
    {
        juce::dsp::IIR::Filter<float> filterLeft;
        juce::dsp::IIR::Filter<float> filterRight;

        void prepare(const juce::dsp::ProcessSpec& spec)
        {
            filterLeft.prepare(spec);
            filterRight.prepare(spec);
        }

        void reset()
        {
            filterLeft.reset();
            filterRight.reset();
        }
    };

    std::array<FilterBand, 7> filters;
    std::array<double, 7> oscPhases;
    double currentSampleRate = 44100.0;
    
    // Random mode state - per band
    std::array<float, 7> randomCurrentValue;
    std::array<float, 7> randomTargetValue;
    std::array<double, 7> randomLastPhase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoMorphEQAudioProcessor)
};
