#pragma once

#include <JuceHeader.h>

class BandpassModulatorAudioProcessor : public juce::AudioProcessor
{
public:
    BandpassModulatorAudioProcessor();
    ~BandpassModulatorAudioProcessor() override;

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

    float getCurrentCutoff() const { return currentCutoff; }
    float getCurrentPan() const { return currentPanValue; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> filters;

    juce::Random random;

    float currentCutoff = 1000.0f; 
    float startCutoff = 1000.0f;
    float targetCutoff = 1000.0f;  

    double timeInCurrentState = 0.0;
    bool isGliding = false;          

    juce::LinearSmoothedValue<float> smoothedPanning{ 0.0f };
    float currentPanValue = 0.0f;
    float targetPanValue = 0.0f;
    float startPanValue = 0.0f;
    double panTimeCounter = 0.0;
    bool isGlidingPan = false;

    int currentNoteIndex = 0;

    std::vector<float> getActiveNoteFrequencies(float minFreq, float maxFreq);

    float getFrequencyForNoteName(int noteInOctave, int octave);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandpassModulatorAudioProcessor)
};