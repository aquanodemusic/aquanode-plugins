#pragma once // A "Guard": Ensures the compiler only reads this file once to prevent errors.

#include <JuceHeader.h> // Includes all the JUCE classes (AudioProcessor, dsp, etc.)

//==============================================================================
/**
 * BandpassModulatorAudioProcessor - The "Brain" class.
 * It inherits from juce::AudioProcessor.
 * Think of ': public' as saying "This class IS an AudioProcessor, but with my custom features added."
 */
class BandpassModulatorAudioProcessor : public juce::AudioProcessor
{
public:
    // --- LIFECYCLE METHODS ---

    // Constructor: Runs once when the plugin is loaded.
    BandpassModulatorAudioProcessor();

    // Destructor: Runs when the plugin is deleted. 'override' tells the compiler 
    // we are specifically replacing the base class version.
    ~BandpassModulatorAudioProcessor() override;

    // --- AUDIO METHODS ---

    // prepareToPlay: Called by the DAW right before audio starts. Good for initializing DSP.
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;

    // releaseResources: Called when audio stops. Good for clearing memory.
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    // Checks if the DAW supports the Mono/Stereo layout we want.
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    // processBlock: THE HEART. This is called every few milliseconds to process a buffer of audio.
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- UI INTERACTION ---

    // Creates the GUI window.
    juce::AudioProcessorEditor* createEditor() override;

    // Returns true if this plugin actually has a GUI (some don't!).
    bool hasEditor() const override;

    // --- INFORMATION FUNCTIONS (Const means they don't change the plugin state) ---

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // --- PRESET MANAGEMENT (Programs) ---
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // --- STATE MANAGEMENT ---
    // These allow the DAW to save and load your plugin settings.
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- PUBLIC DATA ---

    // APVTS: This is the "God Object" for parameters. 
    // It keeps the GUI and the Processor in perfect sync.
    juce::AudioProcessorValueTreeState apvts;

    // --- GETTERS (For the GUI) ---
    // These are 'inline' functions. They are very fast and let the Editor "see" 
    // private variables without being allowed to change them.
    float getCurrentCutoff() const { return currentCutoff; }
    float getCurrentPan() const { return currentPanValue; }

private:
    // createParameterLayout: Defines all the sliders/buttons in the .cpp file.
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // --- DSP OBJECTS ---

    // std::array: A fixed-size container. 
    // Here we have 2 filters (one for Left, one for Right).
    // juce::dsp::StateVariableTPTFilter: A high-quality "Topology Preserving Transform" filter.

    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> filters;

    // juce::Random: A fast, high-quality random number generator.
    juce::Random random;

    // --- MODULATION VARIABLES (The "State" of our filter) ---

    float currentCutoff = 1000.0f; // The cutoff frequency right now
    float startCutoff = 1000.0f;   // The frequency where the current "Glide" started
    float targetCutoff = 1000.0f;  // The frequency we are moving toward

    // --- TRACKING ---

    double timeInCurrentState = 0.0; // Tracks how long we've been in "Stay" or "Glide"
    bool isGliding = false;          // True if the filter is currently moving

    // --- PANNING ---

    // LinearSmoothedValue: A special JUCE class that prevents "clicks" by 
    // slowly ramping values over time.
    juce::LinearSmoothedValue<float> smoothedPanning{ 0.0f };

    float currentPanValue = 0.0f;
    float targetPanValue = 0.0f;
    float startPanValue = 0.0f;
    double panTimeCounter = 0.0;
    bool isGlidingPan = false;

    // --- SEQUENCE LOGIC ---

    // currentNoteIndex: Used in UP/DOWN modes to know which note we just played.
    int currentNoteIndex = 0;

    // --- HELPER FUNCTIONS ---
    // These are private because only this class needs to run these calculations.

    // std::vector: A dynamic array that can grow or shrink (used for note lists).
    std::vector<float> getActiveNoteFrequencies(float minFreq, float maxFreq);

    // Calculates Hertz from a MIDI note number and octave.
    float getFrequencyForNoteName(int noteInOctave, int octave);

    // JUCE macro that prevents the computer from trying to copy this plugin 
    // and adds a detector to find "Memory Leaks."
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandpassModulatorAudioProcessor)
};