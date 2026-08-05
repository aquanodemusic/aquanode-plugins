#pragma once

#include <JuceHeader.h>

class EQResonatorAudioProcessor : public juce::AudioProcessor
{
public:
    EQResonatorAudioProcessor();
    ~EQResonatorAudioProcessor() override;

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


    std::array<std::atomic<float>*, 12> noteParams;
    std::array<std::atomic<float>*, 10> octaveActiveParams;
    std::array<std::atomic<float>*, 10> octaveQParams;
    std::array<std::atomic<float>*, 10> octaveGainParams;

    static constexpr int numNotes = 12;
    static constexpr int numOctaves = 10;
    static constexpr int totalFilters = numNotes * numOctaves;

    std::array<float, totalFilters> noteFrequencies;

    std::atomic<float>* wetOnlyParam = nullptr;

    std::atomic<float>* subtractModeParam = nullptr;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::IIRFilter filtersL[totalFilters];
    juce::IIRFilter filtersR[totalFilters];

    double lastSampleRate = 44100.0;

    float lastOctaveQs[10] = { 0.0f };
    float lastOctaveGains[10] = { 0.0f };

    std::array<float, 12> lastNoteStates{ 0.0f };

    static juce::String getOctaveParamID(int oct) { return "octave" + juce::String(oct); }
    static juce::String getNoteParamID(int note) { return juce::String(note); }
    static juce::String getOctaveQParamID(int oct) { return "qOctave" + juce::String(oct); }

    juce::AudioBuffer<float> dryCopyBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQResonatorAudioProcessor)
};
