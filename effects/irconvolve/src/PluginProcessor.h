#pragma once

#include <JuceHeader.h>

class IRConvolverAudioProcessor : public juce::AudioProcessor
{
public:
    IRConvolverAudioProcessor();
    ~IRConvolverAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // IR management - THREAD SAFE
    void loadImpulseResponse (const juce::File& file);
    bool isIRLoaded() const { return irLoaded.load(); }
    juce::String getCurrentIRName() const;
    
    // Thread-safe way to get IR buffer for visualization
    void copyIRBufferTo(juce::AudioBuffer<float>& destination);

    // Parameter access
    juce::AudioParameterFloat* getMixParameter() { return mixParam; }
    juce::AudioParameterFloat* getGainParameter() { return gainParam; }

private:
    juce::dsp::Convolution convolution;
    juce::AudioBuffer<float> dryBuffer;
    
    // Thread-safe IR buffer with mutex protection
    juce::AudioBuffer<float> irBuffer;
    juce::CriticalSection irBufferLock;
    
    std::atomic<bool> irLoaded { false };
    juce::String currentIRName;
    juce::CriticalSection irNameLock;
    
    double currentSampleRate = 44100.0;
    
    // Parameters
    juce::AudioParameterFloat* mixParam;
    juce::AudioParameterFloat* gainParam;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRConvolverAudioProcessor)
};
