#pragma once

#include <JuceHeader.h>
#include <vector>
#include <random>

//==============================================================================
struct SlicePoint
{
    int samplePosition = 0;
    float amplitude = 0.0f;
    bool active = true;
    
    SlicePoint() = default;
    SlicePoint(int pos, float amp) : samplePosition(pos), amplitude(amp) {}
};

//==============================================================================
struct Voice
{
    bool isActive = false;
    int sliceIndex = -1;
    float currentSample = 0.0f;  // Changed to float for fractional playback speeds
    int sliceStart = 0;
    int sliceEnd = 0;
    float velocity = 1.0f;
    bool playingReverse = false;
    
    void reset()
    {
        isActive = false;
        currentSample = 0.0f;
        sliceIndex = -1;
        playingReverse = false;
    }
};

//==============================================================================
class SlicerAudioProcessor : public juce::AudioProcessor
{
public:
    SlicerAudioProcessor();
    ~SlicerAudioProcessor() override;

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

    // Sample management
    void loadSample(const juce::File& file);
    void loadSample(const void* data, size_t dataSize);
    juce::AudioBuffer<float>& getSampleBuffer() { return sampleBuffer; }
    bool hasSample() const { return sampleBuffer.getNumSamples() > 0; }

    // Slice management
    void analyzeAndSlice();
    const std::vector<SlicePoint>& getSlicePoints() const { return slicePoints; }
    void setSlicePosition(int index, int newPosition);
    void removeSlice(int index);
    void addSliceAtPosition(int samplePosition);
    
    // Apply slice end mode adjustments to slice positions
    void applySliceEndMode();
    
    // Calculate the actual end position for a slice (used only for Mode 1)
    int calculateSliceEnd(int sliceIndex) const;
    
    // Parameter access
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }
    
    // Get region bounds in samples
    int getRegionStart() const;
    int getRegionEnd() const;
    
    // Export slices to disc
    bool exportSlicesToDisc(const juce::File& outputFolder);
    
    // Check if a slice is currently playing
    bool isSlicePlaying(int sliceIndex) const;

    // Playback control (public so WaveformDisplay can call them directly)
    void triggerSlice(int sliceIndex, float velocity);
    void stopSlice(int sliceIndex);

    // Random stream control
    void startRandomStream();
    void stopRandomStream();
    bool isRandomStreamActive() const { return randomStreamActive; }

    // Add-slice mode — reads directly from the persisted APVTS parameter
    // so the choice survives DAW session save/reload.
    bool isAddSliceModeActive() const
    {
        auto* p = parameters.getRawParameterValue("interactionMode");
        return p ? (p->load() < 0.5f) : true;  // 0 = add-slice, 1 = play-slice
    }

private:
    // Path of the last successfully loaded sample — persisted in the DAW session
    juce::String lastLoadedSamplePath;
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioBuffer<float> sampleBuffer;
    double loadedSampleRate = 44100.0; // Default fallback
    
    std::vector<SlicePoint> slicePoints;
    std::vector<Voice> voices;
    
    // Random stream mode
    std::mt19937 randomEngine;
    std::uniform_int_distribution<int> randomSliceDistribution;
    int randomStreamVoiceIndex = -1;
    bool randomStreamActive = false;
    
    // Transient detection
    void detectTransients(float threshold, int regionStart, int regionEnd);
    float calculateEnvelopeFollower(int startSample, int windowSize);
    
    // Helper methods for slice end calculation
    int findQuietestPointInTail(int sliceStart, int sliceEnd) const;
    int findDb40EndPoint(int sliceStart, int sliceEnd) const;
    
    // MIDI note to slice mapping (C1 = 36, C9 = 108)
    int getNoteForSlice(int sliceIndex) const;
    int getSliceForNote(int midiNote) const;
    
    // Random stream helpers
    void triggerRandomSlice();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicerAudioProcessor)
};
