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

    // Custom methods for audio loading and processing.
    //
    // These take a juce::URL rather than a juce::File because on Android the
    // file picker almost always hands back a content:// URI (Storage Access
    // Framework), not a real filesystem path - juce::File simply can't
    // represent that. juce::URL can represent both a real path (desktop, and
    // the occasional case where Android does give back a local path) and a
    // content:// URI, and knows how to open a stream for either.
    void loadAudioFile(const juce::URL& audioFileURL);

    // Exports the current selection, writing WAV/AIFF/FLAC to match
    // outputURL's extension (set that via getRecommendedExportExtension()).
    void processAndExport(const juce::URL& outputURL);

    // Formats we can genuinely write back out (JUCE ships no MP3 encoder).
    bool canExportInSourceFormat() const;
    juce::String getRecommendedExportExtension() const; // includes the leading dot
    const juce::String& getLoadedFileExtension() const { return loadedFileExtension; }

    // Region selection
    void setRegion(int startSample, int endSample);
    int getRegionStart() const { return regionStart; }
    int getRegionEnd() const { return regionEnd; }

    // Preview playback
    void renderPreview();
    void startPreview();  // starts, or resumes from wherever it was paused
    void pausePreview();  // stops without resetting playback position
    void stopPreview();   // stops and resets playback position to the start
    void clearPreview()
    {
        const juce::SpinLock::ScopedLockType lock(previewLock);
        previewPlaying = false;
        previewPlayPosition = 0;
        previewBuffer.setSize(0, 0);
    }
    bool isPreviewPlaying() const { return previewPlaying; }
    void setPreviewLooping(bool shouldLoop) { previewLooping = shouldLoop; }
    bool isPreviewLooping() const { return previewLooping; }
    bool hasPreviewRendered() const
    {
        const juce::SpinLock::ScopedLockType lock(previewLock);
        return previewBuffer.getNumSamples() > 0;
    }

    // Playhead position for the GUI, normalised 0..1 across the currently
    // rendered preview (which spans the selected region). Returns -1 if
    // nothing is playing right now, or if the lock is momentarily held by
    // a render/clear on another thread (the caller - a ~30Hz UI timer -
    // will just pick it up on the next tick instead of blocking here).
    float getPreviewProgress() const
    {
        const juce::SpinLock::ScopedTryLockType lock(previewLock);
        if (!lock.isLocked() || !previewPlaying || previewBuffer.getNumSamples() == 0)
            return -1.0f;
        return (float)previewPlayPosition / (float)previewBuffer.getNumSamples();
    }

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
    int getLoadedBitsPerSample() const { return loadedBitsPerSample; }
    const float* getLoadedBufferReadPointer(int channel) const
    {
        return channel < loadedBuffer.getNumChannels() ? loadedBuffer.getReadPointer(channel) : nullptr;
    }
    int getLoadedNumChannels() const { return loadedBuffer.getNumChannels(); }

    // Parameters
    juce::AudioParameterFloat* pitchShiftParam;   // semitones, -32 .. +32
    juce::AudioParameterFloat* fineTuneParam;     // cents, -100 .. +100
    juce::AudioParameterFloat* timeStretchParam;  // playback speed, 0.2x .. 4x (1x = unity, at centre)
    juce::AudioParameterFloat* fadeInParam;       // seconds, 0 .. 5 (skewed for extra resolution up to ~200ms)
    juce::AudioParameterFloat* fadeOutParam;      // seconds, 0 .. 5 (skewed for extra resolution up to ~200ms)

private:
    // Loaded audio
    juce::AudioBuffer<float> loadedBuffer;
    double loadedSampleRate = 44100.0;
    // The rate audio is actually leaving through (DAW project rate,
    // standalone device rate, APK device rate...). Preview is rendered at
    // loadedSampleRate but must be resampled to this before playback, or
    // it comes out pitched/sped up or down by the ratio between the two.
    double hostSampleRate = 44100.0;
    int loadedBitsPerSample = 24; // detected from source; fallback for formats with no fixed PCM depth
    juce::String loadedFileName;
    juce::String loadedFileExtension; // e.g. ".wav", lowercase, includes the dot
    juce::URL loadedURL; // wherever it was actually loaded from - local path or content:// URI

    // Region selection
    int regionStart = 0;
    int regionEnd = 0;

    // Preview playback
    // previewBuffer/previewPlayPosition are touched from the audio thread
    // (processBlock), the message thread (knob tweaks -> clearPreview(),
    // transport buttons), and a background render thread (renderPreview()).
    // previewLock serialises all of that; the audio thread only ever
    // try-locks it so it can never be blocked by the other two.
    mutable juce::SpinLock previewLock;
    juce::AudioBuffer<float> previewBuffer;
    std::atomic<bool> previewPlaying{ false };
    std::atomic<bool> previewLooping{ false };
    int previewPlayPosition = 0;
    juce::AudioTransportSource transportSource;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::MemoryInputStream> previewStream;
    std::unique_ptr<juce::AudioFormatReader> previewReader;
    std::unique_ptr<juce::AudioFormatReaderSource> previewReaderSource;

    // Processing methods - take the input explicitly (rather than reading
    // the loadedBuffer member) so loadedBuffer never has to be temporarily
    // swapped out while these run on a background thread. That swap used to
    // race against the UI thread painting the waveform from loadedBuffer.
    juce::AudioBuffer<float> processWithRubberBand(const juce::AudioBuffer<float>& input, float pitchSemitones, float timeStretch);

    // Applies a linear fade-in/fade-out (in seconds, at bufferSampleRate) to
    // the start/end of buffer in place. If the two overlap on a short
    // buffer, both are scaled down proportionally so they just meet in the
    // middle rather than double-attenuating the overlapping samples.
    void applyFades(juce::AudioBuffer<float>& buffer, double bufferSampleRate, float fadeInSeconds, float fadeOutSeconds) const;

    // File writers - bitsPerSample is passed in per call so output can match
    // source. These take an already-open OutputStream (rather than a File)
    // so the caller can hand them either a plain juce::FileOutputStream
    // (desktop, or a local Android path) or a stream backed by Android's
    // ContentResolver (a content:// URI from the SAF save dialog) with no
    // format-writing code needing to know or care which one it got.
    bool writeToWav(const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        int bitsPerSample,
        std::unique_ptr<juce::OutputStream> outputStream);

    bool writeToAiff(const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        int bitsPerSample,
        std::unique_ptr<juce::OutputStream> outputStream);

    bool writeToFlac(const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        int bitsPerSample,
        std::unique_ptr<juce::OutputStream> outputStream);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioStretcherAudioProcessor)
};