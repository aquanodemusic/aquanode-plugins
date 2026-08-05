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

    /** Launch an INCREMENTAL background render (Engine 1) or full re-render
     *  (Engine 2) depending on the current engine mode. */
    void triggerBackgroundRender();

    void  setNumHarmonics(int n) { numHarmonics_.store(juce::jlimit(1, 64, n)); }
    int   getNumHarmonics()  const { return numHarmonics_.load(); }

    void  setBlurStrength(float s)
    {
        const float old = blurStrength_.exchange(s);
        blurEnabled_.store(s > 0.001f);
        // Engine 2 bakes the global decay constant into each rendered chunk, so
        // changing blur invalidates all previously rendered audio.  Re-render the
        // full history so the new decay applies everywhere.
        if (engineMode_.load() == 1 && std::abs(s - old) > 0.001f)
            triggerFullRerender();
    }
    float getBlurStrength()  const { return blurStrength_.load(); }

    //==========================================================================
    // Engine selection
    //
    //  0 = Engine 1  (incremental WaypointOscillator render -- supports paths)
    //  1 = Engine 2  (incremental render, global decay -- default)
    //==========================================================================
    void setEngineMode(int mode);
    int  getEngineMode() const noexcept { return engineMode_.load(); }

    /** Force a full re-render from allEvents_/allPaths_ history, replacing
     *  committedBuffer_ entirely.  Used when a global parameter (blur, engine
     *  switch) invalidates all previously rendered audio. */
    void triggerFullRerender();

    /** Export committed audio to FLAC.  No re-render needed -- we just apply
        soft-clip to the committedBuffer_ copy and write it out. */
    bool exportToFlac(const juce::File& file);

private:
    //==========================================================================
    // Pending events & paths -- consumed by Engine 1's incremental render
    mutable juce::CriticalSection eventsCS_;
    std::vector<DrawnEvent>       events_;      // sorted by time

    mutable juce::CriticalSection pathsCS_;
    std::vector<DrawnPath>        paths_;

    //==========================================================================
    // Permanent event/path history -- used by Engine 2 for full re-renders
    // and preserved across engine switches so neither engine loses drawn content.
    mutable juce::CriticalSection allEventsCS_;
    std::vector<DrawnEvent>       allEvents_;   // sorted by time

    mutable juce::CriticalSection allPathsCS_;
    std::vector<DrawnPath>        allPaths_;

    //==========================================================================
    // The master audio mix (raw, pre-tanh).
    //
    //  Engine 1: each incremental render adds its contribution here.
    //  Engine 2: each full re-render replaces this entirely.
    //  Tanh soft-clip is applied at playback time (processBlock) and at export
    //  time (exportToFlac), keeping the committed signal in a linear domain.
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
    std::atomic<bool>  blurEnabled_{ false };  // derived from blurStrength_; used by Engine 2

    std::atomic<int>   engineMode_{ 1 };       // 0 = Engine 1, 1 = Engine 2 (default)

    //==========================================================================
    /** Atomically extract and clear the pending event list. */
    std::vector<DrawnEvent> extractEvents();
    /** Atomically extract and clear the pending path list. */
    std::vector<DrawnPath>  extractPaths();

    // ---- Engine 1 renderer --------------------------------------------------
    /** Synthesise only the supplied events/paths into a fresh 30-second buffer.
     *  Returns a RAW (pre-tanh) signal.  The caller mixes it into
     *  committedBuffer_. */
    juce::AudioBuffer<float> renderNewContent(
        const std::vector<DrawnEvent>& evts,
        const std::vector<DrawnPath>& paths,
        double targetSR);

    // ---- Engine 2 renderer --------------------------------------------------
    /** Full re-render from all supplied events.  Returns a tanh-clipped buffer
     *  ready for direct playback.  Uses a global blur/decay rather than per-
     *  event settings, and does not handle interpolation paths. */
    juce::AudioBuffer<float> renderOffline(
        const std::vector<DrawnEvent>& evts,
        double targetSR);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyDrawerAudioProcessor)
};