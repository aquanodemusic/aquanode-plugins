/*
  ==============================================================================
    FrequencyDrawer -- PluginProcessor.h
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <memory>
#include <cmath>

//==============================================================================
/** A single stamped frequency event (individual sine hit). */
struct DrawnEvent
{
    double time;                    // seconds  [0, 30]
    double frequency;               // Hz       [20, 20000]
    double amplitude;               // linear   (pre-normalised by the harmonic series)
    bool   blurEnabled = false;
    float  blurSecs = 0.0f;
};

//==============================================================================
/** A complete interpolation stroke stored as (time, fundamentalFreq) waypoints. */
struct DrawnPath
{
    std::vector<std::pair<double, double>> waypoints; ///< (time s, fundamental Hz)
    int    numHarmonics = 1;
    double amplitude = 0.25;
    bool   blurEnabled = false;
    float  blurSecs = 0.0f;
};

//==============================================================================
/** Sine oscillator with exponential amplitude decay -- used only in renderNewContent. */
class SynthOscillator
{
public:
    bool active = false;

    void trigger(double freq, double initialAmp, double decayMult) noexcept
    {
        frequency_ = freq;
        amplitude_ = initialAmp;
        decayMult_ = decayMult;
        phase_ = 0.0;
        active = true;
    }

    float processSample(double sampleRate) noexcept
    {
        if (!active) return 0.0f;
        const float out = static_cast<float>(amplitude_ * std::sin(phase_));
        phase_ += juce::MathConstants<double>::twoPi * frequency_ / sampleRate;
        if (phase_ >= juce::MathConstants<double>::twoPi)
            phase_ -= juce::MathConstants<double>::twoPi;
        amplitude_ *= decayMult_;
        if (amplitude_ < 1.0e-7) active = false;
        return out;
    }

    double getAmplitude() const noexcept { return amplitude_; }

private:
    double frequency_ = 440.0;
    double amplitude_ = 0.0;
    double decayMult_ = 0.9999;
    double phase_ = 0.0;
};

//==============================================================================
class FrequencyDrawerAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr double kDuration = 30.0;
    static constexpr double kFreqMin = 20.0;
    static constexpr double kFreqMax = 20000.0;

    static constexpr int kMaxOsc = 2048;

    //==========================================================================
    FrequencyDrawerAudioProcessor();
    ~FrequencyDrawerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources()                                       override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()                    const  override;

    const juce::String getName()               const override;
    bool  acceptsMidi()                        const override;
    bool  producesMidi()                       const override;
    bool  isMidiEffect()                       const override;
    double getTailLengthSeconds()              const override;
    int   getNumPrograms()                           override;
    int   getCurrentProgram()                        override;
    void  setCurrentProgram(int index)               override;
    const juce::String getProgramName(int index)     override;
    void  changeProgramName(int index, const juce::String& newName) override;
    void  getStateInformation(juce::MemoryBlock& destData)          override;
    void  setStateInformation(const void* data, int sizeInBytes)    override;

    //==========================================================================
    // Editor API
    //==========================================================================

    void addEvent(double time, double freq, double amplitude = 0.25,
        bool blurEnabled = false, float blurSecs = 0.0f);
    void addEvents(std::vector<DrawnEvent> newEvents);
    void addPath(DrawnPath path);

    /** Stop playback, silence committed audio, and flush all pending events. */
    void clearAllEvents();

    std::vector<DrawnEvent> getEventsCopy() const;
    std::vector<DrawnPath>  getPathsCopy()  const;

    void requestSeek(double seconds);
    void setPlaying(bool shouldPlay);

    double getPlayheadSeconds() const noexcept { return playheadSeconds_.load(); }
    bool   getIsPlaying()       const noexcept { return playing_.load(); }
    bool   getIsRendering()     const noexcept { return isRendering_.load(); }

    /** Launch an INCREMENTAL background render.
     *
     *  Only the currently pending events/paths are synthesised.  The result is
     *  mixed (summed) into the persistent committedBuffer_ so the render time
     *  is proportional to the NEW content only -- not the entire canvas history.
     *
     *  After mixing the pending lists are cleared, so the raw event data is no
     *  longer needed.  On the next processBlock boundary the committed audio
     *  is swapped in transparently. */
    void triggerBackgroundRender();

    void  setNumHarmonics(int n) { numHarmonics_.store(juce::jlimit(1, 64, n)); }
    int   getNumHarmonics()  const { return numHarmonics_.load(); }
    void  setBlurStrength(float s) { blurStrength_.store(s); }
    float getBlurStrength()  const { return blurStrength_.load(); }

    /** Export committed audio to FLAC.  No re-render needed -- we just apply
        soft-clip to the committedBuffer_ copy and write it out. */
    bool exportToFlac(const juce::File& file);

private:
    //==========================================================================
    // Pending events & paths -- cleared after each incremental render
    mutable juce::CriticalSection eventsCS_;
    std::vector<DrawnEvent>       events_;      // sorted by time

    mutable juce::CriticalSection pathsCS_;
    std::vector<DrawnPath>        paths_;

    //==========================================================================
    // The master audio mix (raw, pre-tanh).
    //
    //  Each incremental render adds its contribution here.  Tanh soft-clip is
    //  applied at playback time (processBlock) and at export time
    //  (exportToFlac), so the committed signal stays in a linear domain that
    //  can be mixed correctly across multiple strokes.
    mutable juce::CriticalSection             committedBufferCS_;
    std::unique_ptr<juce::AudioBuffer<float>> committedBuffer_;

    //==========================================================================
    // Double-buffer swap for thread-safe processBlock handoff
    std::atomic<bool>  isRendering_{ false };
    std::atomic<bool>  rerenderPending_{ false };
    std::atomic<bool>  newBufferReady_{ false };
    std::atomic<int>   renderGeneration_{ 0 };

    juce::CriticalSection                     bufferSwapCS_;
    std::unique_ptr<juce::AudioBuffer<float>> activeBuffer_;
    std::unique_ptr<juce::AudioBuffer<float>> pendingBuffer_;

    //==========================================================================
    // Playback state
    std::atomic<bool>   playing_{ false };
    std::atomic<double> playheadSeconds_{ 0.0 };
    std::atomic<bool>   seekPending_{ false };
    std::atomic<double> seekTarget_{ 0.0 };

    double  sampleRate_ = 44100.0;
    int64_t playheadSamples_ = 0;

    //==========================================================================
    std::atomic<int>   numHarmonics_{ 1 };
    std::atomic<float> blurStrength_{ 0.0f };

    //==========================================================================
    /** Atomically extract and clear the pending event list. */
    std::vector<DrawnEvent> extractEvents();
    /** Atomically extract and clear the pending path list. */
    std::vector<DrawnPath>  extractPaths();

    /** Synthesise only the supplied events/paths into a fresh 30-second buffer.
     *  Returns a RAW (pre-tanh) signal.  The caller mixes it into
     *  committedBuffer_. */
    juce::AudioBuffer<float> renderNewContent(
        const std::vector<DrawnEvent>& evts,
        const std::vector<DrawnPath>& paths,
        double targetSR);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyDrawerAudioProcessor)
};