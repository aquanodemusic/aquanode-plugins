#pragma once

#include <JuceHeader.h>
#include <vector>
#include <random>

//==============================================================================
struct GrainVoice
{
    bool isActive = false;
    double samplePosition = 0.0;  // Use double for long files (up to 1 hour)
    float pitch = 1.0f;
    float amplitude = 1.0f;
    float panLeft = 1.0f;
    float panRight = 1.0f;
    float grainEnvelopePhase = 0.0f;  // Per-grain envelope
    float noteEnvelopePhase = 0.0f;   // Per-note envelope
    float pitchOffset = 0.0f;
    float amplitudeOffset = 0.0f;
    int midiNote = -1;
    
    // Grain-specific ADSR randomization
    float grainAttack = 0.01f;
    float grainDecay = 0.1f;
    float grainSustain = 0.7f;
    float grainRelease = 0.3f;
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
    float currentValue = 0.0f;
    float targetValue = 0.0f;
};

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
    
    const std::vector<GrainVoice>& getGrainVoices() const { return grainVoices; }
    
    // Mode control
    void setMousePosition(float position);
    void releaseMousePosition();
    bool isInMouseMode() const;

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioBuffer<float> sampleBuffer;
    juce::CriticalSection bufferLock;  // Protect sample buffer access
    
    std::vector<GrainVoice> grainVoices;
    float grainTimer = 0.0f;
    
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist01{ 0.0f, 1.0f };
    
    // MIDI tracking
    std::atomic<bool> anyNoteActive{ false };
    std::array<std::atomic<bool>, 128> noteStates;
    
    // Mouse mode
    std::atomic<bool> mousePressed{ false };
    std::atomic<float> mousePosition{ 0.0f };
    
    // Note envelope tracking for entire note press
    float noteEnvelopeTimer = 0.0f;
    bool noteReleased = false;

    void processGrains(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processMidiMessages(juce::MidiBuffer& midiMessages);
    void triggerGrain(int voiceIndex, float basePosition);
    void stopAllGrains();
    float getGrainEnvelope(float phase, const GrainVoice& voice);
    float getNoteEnvelope(float phase, bool released);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranulateAudioProcessor)
};