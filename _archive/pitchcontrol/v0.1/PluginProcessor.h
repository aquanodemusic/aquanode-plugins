#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

//==============================================================================
class PitchColorAudioProcessor : public juce::AudioProcessor,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    PitchColorAudioProcessor();
    ~PitchColorAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    // AudioProcessorValueTreeState::Listener callback
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter management
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    // Note names for the 12 chromatic notes
    static constexpr std::array<const char*, 12> noteNames =
    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;

    // Total number of notes: C0 to B9 = 10 octaves * 12 notes = 120 notes
    static constexpr int NUM_NOTES = 120;
    static constexpr int NOTES_PER_OCTAVE = 12;
    static constexpr int NUM_OCTAVES = 10;

    // Bell filters for each note, per channel (stereo)
    using FilterType = juce::dsp::IIR::Filter<float>;
    std::array<std::array<FilterType, NUM_NOTES>, 2> bellFilters;

    float currentSampleRate = 44100.0f;

    // Optimization: pre-allocated buffer for wet-only mode
    juce::AudioBuffer<float> dryBuffer;

    // Optimization: track when filters need updating
    std::atomic<bool> needsFilterUpdate{ true };

    //==============================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Helper function to get frequency for a given MIDI note number
    static float getFrequencyForNote(int midiNoteNumber);

    // Update filter coefficients for a specific note
    void updateFilterCoefficients(int noteIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchColorAudioProcessor)
};