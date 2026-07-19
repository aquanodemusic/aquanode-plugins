#pragma once

#include <JuceHeader.h>
#include <vector>
#include <random>

//==============================================================================
struct GrainVoice
{
    bool isActive = false;
    double samplePosition = 0.0;  // double for long files (up to 1 hour)
    float pitch = 1.0f;
    float amplitude = 1.0f;
    float panLeft = 1.0f;
    float panRight = 1.0f;
    float grainEnvelopePhase = 0.0f;

    // Grain-specific ADSR (randomised per grain)
    float grainAttack  = 0.01f;
    float grainDecay   = 0.1f;
    float grainSustain = 0.7f;
    float grainRelease = 0.3f;
};

//==============================================================================
// One polyphonic slot — owns its own grain pool and note-envelope state.
// All fields are only touched on the audio thread, so no atomics needed here.
static constexpr int MAX_NOTE_VOICES    = 12;
static constexpr int MAX_GRAINS_PER_NOTE = 32;

struct NoteVoice
{
    bool  isActive   = false;
    bool  isReleased = false;
    int   midiNote   = -1;
    float noteEnvelopeTimer = 0.0f;
    float grainTimer        = 0.0f;
    int   ageCounter        = 0;  // monotonic stamp used for voice stealing
    std::array<GrainVoice, MAX_GRAINS_PER_NOTE> grains{};
};

//==============================================================================
class ParameterSmoother {
public:
    ParameterSmoother() = default;

    void setSmoothingTime(float timeInSeconds, float sampleRate) {
        smoothingFactor = 1.0f - std::exp(-1.0f / (timeInSeconds * sampleRate));
        currentValue = targetValue;
    }

    float process(float newValue) {
        targetValue = newValue;
        currentValue += (targetValue - currentValue) * smoothingFactor;
        return currentValue;
    }

    float getCurrentValue() const { return currentValue; }

private:
    float smoothingFactor = 0.02f;
    float currentValue    = 0.0f;
    float targetValue     = 0.0f;
};

//==============================================================================
class GranulateAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    GranulateAudioProcessor();
    ~GranulateAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    void loadSample(const juce::File& file);
    void loadSample(const void* data, size_t dataSize);
    juce::AudioBuffer<float>& getSampleBuffer() { return sampleBuffer; }
    bool hasSample() const { return sampleBuffer.getNumSamples() > 0; }

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // Returns a snapshot of all currently active grains across every voice.
    // Called from the GUI thread for the waveform-display playhead visualisation.
    std::vector<GrainVoice> getActiveGrains() const;

    // Mouse-mode control — called from the GUI thread
    void setMousePosition(float position);
    void releaseMousePosition();
    bool isInMouseMode() const;

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioBuffer<float> sampleBuffer;
    juce::CriticalSection bufferLock;

    // ---- Polyphonic MIDI voices (audio-thread only, no atomics needed) -------
    std::array<NoteVoice, MAX_NOTE_VOICES> noteVoices;
    int voiceAgeCounter = 0;   // global monotonic counter for age-based stealing

    // ---- Mouse-mode grain pool -----------------------------------------------
    // Grains are written/read exclusively on the audio thread.
    // mousePressed / mousePosition / timer / released are shared with the GUI
    // thread and therefore kept as atomics.
    std::array<GrainVoice, MAX_GRAINS_PER_NOTE> mouseGrains{};
    float mouseGrainTimer = 0.0f;

    std::atomic<bool>  mousePressed{ false };
    std::atomic<float> mousePosition{ 0.0f };
    std::atomic<float> mouseNoteEnvelopeTimer{ 0.0f };
    std::atomic<bool>  mouseNoteReleased{ false };

    // Smooths the mouse position to prevent sample-position jumps (clicks)
    // when the user drags quickly across the waveform display.
    ParameterSmoother smoothedMousePosition;

    std::mt19937 rng;
    std::uniform_real_distribution<float> dist01{ 0.0f, 1.0f };

    // ---- Private helpers -----------------------------------------------------
    void processGrains(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processMidiMessages(juce::MidiBuffer& midiMessages);

    // Initialise one GrainVoice.  midiNote == -1 → mouse mode (pitch knob).
    void triggerGrain(GrainVoice& grain, int midiNote, float basePosition);

    void stopAllGrains();

    float getGrainEnvelope(float phase, const GrainVoice& grain);
    float getNoteEnvelope(float phase, bool released);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranulateAudioProcessor)
};
