#pragma once

#include <JuceHeader.h>
#include <vector>

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
    int currentSample = 0;
    int sliceStart = 0;
    int sliceEnd = 0;
    float velocity = 1.0f;
    
    void reset()
    {
        isActive = false;
        currentSample = 0;
        sliceIndex = -1;
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

private:
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioBuffer<float> sampleBuffer;
    double loadedSampleRate = 44100.0; // Default fallback
    
    std::vector<SlicePoint> slicePoints;
    std::vector<Voice> voices;
    
    // Transient detection
    void detectTransients(float threshold, int regionStart, int regionEnd);
    float calculateEnvelopeFollower(int startSample, int windowSize);
    
    // Helper methods for slice end calculation
    int findQuietestPointInTail(int sliceStart, int sliceEnd) const;
    int findDb40EndPoint(int sliceStart, int sliceEnd) const;
    
    // MIDI note to slice mapping (C1 = 36, C9 = 108)
    int getNoteForSlice(int sliceIndex) const;
    int getSliceForNote(int midiNote) const;
    
    void triggerSlice(int sliceIndex, float velocity);
    void stopSlice(int sliceIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicerAudioProcessor)
};
