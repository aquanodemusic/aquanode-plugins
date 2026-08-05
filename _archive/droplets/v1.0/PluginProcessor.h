#pragma once

#include <JuceHeader.h>

class DropletsAudioProcessor : public juce::AudioProcessor
{
public:
    // --- 1. Construction & Destruction ---
    DropletsAudioProcessor();
    ~DropletsAudioProcessor() override;

    // --- 2. Audio & MIDI Lifecycle ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- 3. Editor & UI ---
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // --- 4. DAW Information Overrides ---
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // --- 5. Programs (Presets) ---
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // --- 6. State Management (Persistence) ---
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- 7. Public Custom Logic & Data ---
    juce::AudioProcessorValueTreeState apvts;

private:
    // --- 8. Private Internal Components ---
    juce::Synthesiser synth;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DropletsAudioProcessor)
};
