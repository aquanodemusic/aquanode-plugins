#pragma once

#include <JuceHeader.h>

// Forward declare RubberBand classes to avoid including the full header
namespace RubberBand {
    class RubberBandStretcher;
}

class AudioStretcherAudioProcessor : public juce::AudioProcessor
{
public:
    AudioStretcherAudioProcessor();
    ~AudioStretcherAudioProcessor() override;

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

    // Custom methods for audio loading and processing
    enum class ExportFormat { FLAC24, FLAC16 };
    void loadAudioFile(const juce::File& file);
    void processAndExport(const juce::File& outputFile, ExportFormat format = ExportFormat::FLAC24);

    // Region selection
    void setRegion(int startSample, int endSample);
    int getRegionStart() const { return regionStart; }
    int getRegionEnd() const { return regionEnd; }
    
    // Preview playback
    void renderPreview();
    void startPreview();
    void stopPreview();
    void clearPreview() { previewBuffer.setSize(0, 0); stopPreview(); }
    bool isPreviewPlaying() const { return previewPlaying; }
    bool hasPreviewRendered() const { return previewBuffer.getNumSamples() > 0; }

    // Progress tracking
    std::function<void(float)> progressCallback;
    std::function<void(const juce::String&)> stageCallback;
    void setProgressCallback(std::function<void(float)> callback) { progressCallback = callback; }
    void setStageCallback(std::function<void(const juce::String&)> callback) { stageCallback = callback; }

    // Getters
    bool hasAudioLoaded() const { return loadedBuffer.getNumSamples() > 0; }
    const juce::String& getLoadedFileName() const { return loadedFileName; }
    int getLoadedSampleCount() const { return loadedBuffer.getNumSamples(); }
    double getLoadedSampleRate() const { return loadedSampleRate; }
    const float* getLoadedBufferReadPointer(int channel) const 
    { 
        return channel < loadedBuffer.getNumChannels() ? loadedBuffer.getReadPointer(channel) : nullptr; 
    }
    int getLoadedNumChannels() const { return loadedBuffer.getNumChannels(); }

    // Parameters
    juce::AudioParameterFloat* pitchShiftParam;
    juce::AudioParameterFloat* timeStretchParam;
    juce::AudioParameterBool* useNaiveMethodParam;

private:
    // Loaded audio
    juce::AudioBuffer<float> loadedBuffer;
    double loadedSampleRate = 44100.0;
    juce::String loadedFileName;
    juce::File loadedFile;
    
    // Region selection
    int regionStart = 0;
    int regionEnd = 0;
    
    // Preview playback
    juce::AudioBuffer<float> previewBuffer;
    std::atomic<bool> previewPlaying { false };
    int previewPlayPosition = 0;
    juce::AudioTransportSource transportSource;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::MemoryInputStream> previewStream;
    std::unique_ptr<juce::AudioFormatReader> previewReader;
    std::unique_ptr<juce::AudioFormatReaderSource> previewReaderSource;

    // Processing methods - now return buffers instead of writing directly
    juce::AudioBuffer<float> processWithRubberBand(float pitchSemitones, float timeStretch);
    juce::AudioBuffer<float> processWithJUCENative(float pitchSemitones, float timeStretch);
    juce::AudioBuffer<float> processWithNaiveMethod(float pitchSemitones, float timeStretch);
    
    // Helper methods for time stretch and pitch shift
    juce::AudioBuffer<float> performTimeStretch(const juce::AudioBuffer<float>& input, float ratio);
    juce::AudioBuffer<float> performTimeStretchNaive(const juce::AudioBuffer<float>& input, float ratio);
    juce::AudioBuffer<float> performPitchShift(const juce::AudioBuffer<float>& input, double pitchRatio);

    // File writers
    bool writeToFlac(const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        const juce::File& outputFile);
    
    bool writeToFlac16(const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        const juce::File& outputFile);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioStretcherAudioProcessor)
};
