#pragma once

#include <JuceHeader.h>

//==============================================================================
// This is the main audio processor class for the EQ Resonator plugin.
// It manages parameters, filters, and audio processing logic for each note
// and octave. It also provides data for the visualizer.
//==============================================================================
class EQResonatorAudioProcessor : public juce::AudioProcessor
{
public:
    EQResonatorAudioProcessor();
    ~EQResonatorAudioProcessor() override;

    // Called before playback starts. Use this to initialize filters,
    // allocate buffers, and set up any per-sample processing.
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;

    // Called when playback stops. Release memory or reset states here.
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    // Checks if the requested audio input/output layout is supported.
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    // Main audio processing function. Called for each block of audio samples.
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // GUI-related functions
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // Plugin identification
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // Program (preset) management
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // Save/restore plugin state (parameters, presets)
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- PUBLIC DATA FOR VISUALIZER / QUICK PARAM ACCESS ---

    // Pointers to parameter values for fast access in the audio thread
    std::array<std::atomic<float>*, 12> noteParams;        // One pointer per note (C-B)
    std::array<std::atomic<float>*, 10> octaveActiveParams; // Active/inactive state per octave (0-9)
    std::array<std::atomic<float>*, 10> octaveQParams;      // Q (resonance) per octave

    // Constants to define array sizes
    static constexpr int numNotes = 12;        // 12 chromatic notes
    static constexpr int numOctaves = 10;      // 10 octaves
    static constexpr int totalFilters = numNotes * numOctaves; // Total number of filters needed

    // Frequencies for each note across all octaves
    // This is used to set the center frequency of each resonator filter
    std::array<float, totalFilters> noteFrequencies;

    std::atomic<float>* wetOnlyParam = nullptr; // wet-only toggle

    // JUCE's state management class for parameters
    juce::AudioProcessorValueTreeState apvts;

private:
    // Creates all parameters (notes, octaves, Qs, wet gain, etc.) and their ranges
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Filter objects for each note/octave
    // Separate filters for left and right channels
    juce::IIRFilter filtersL[totalFilters];
    juce::IIRFilter filtersR[totalFilters];

    // Previous error: removed the old C-style array
    // We now use the safer std::array in the public section.

    // Sample rate used for filter calculations
    double lastSampleRate = 44100.0;

    // Stores whether "attenuate" mode was active last frame
    // This is used to detect changes in state for optimization
    bool lastIsAttenuate = false;

    // Caches for parameters to avoid repeatedly reading atomic values
    // Initialized to safe defaults to prevent garbage values
    float lastOctaveQs[9] = { 0.0f }; // Last Q values for octaves 0-8
    float lastWetGain = 1.0f;         // Last wet gain applied

    // Direct pointers to specific parameters for faster access in audio thread
    std::atomic<float>* attenuateParam = nullptr;
    std::atomic<float>* wetGainParam = nullptr;

    // Tracks the previous state of each note button (pressed or released)
    // Used to detect note resets or changes
    std::array<float, 12> lastNoteStates{ 0.0f };

    // Helper functions for generating parameter IDs consistently
    // These are used when creating parameters or accessing them by name
    static juce::String getOctaveParamID(int oct) { return "octave" + juce::String(oct); }
    static juce::String getNoteParamID(int note) { return juce::String(note); }
    static juce::String getOctaveQParamID(int oct) { return "qOctave" + juce::String(oct); }

    // JUCE macro to prevent copying and detect memory leaks
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQResonatorAudioProcessor)
};