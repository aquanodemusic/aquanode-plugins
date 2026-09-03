/*
  ==============================================================================

    PluginProcessor.h
    IRForge — impulse response foundry and convolver.

    Replaces the CepstralIR + IRConvolve pair with a single plugin, because the
    two-plugin workflow had no feedback loop: you rendered a file in one plugin
    and loaded it in another, so you could never hear what a control did.

    Here you drop a sample in and hear it convolved immediately. Saving the IR
    to disk is optional.

    --------------------------------------------------------------------------
    THE CENTRAL CONTROL: CHARACTER
    --------------------------------------------------------------------------

    Cepstral IR extraction and puriFIR-style minimum-phase conversion are the
    SAME operation at different settings:

        log|FFT(x)|  ->  cepstrum  ->  lifter  ->  exp  ->  minimum-phase IR

    The only difference is where the lifter sits. Keeping few quefrency
    coefficients leaves the smooth spectral ENVELOPE — the room, the cabinet,
    the system response — and discards the source. Keeping all of them leaves
    the FULL magnitude spectrum including the harmonic comb, so convolving with
    it imposes the sample's entire spectral fingerprint. That is what makes
    white noise turn into a chord, and it is the basis of the "colour bass"
    technique.

    Measured on a guitar sample at a 32768-point FFT, harmonic fine structure
    surviving the lifter:

        lifter    64  ->  0.00 dB   (envelope only, nothing musical left)
        lifter   128  ->  2.56 dB   (the old CepstralIR default: half gone)
        lifter   512  ->  4.69 dB
        lifter 16384  ->  5.00 dB   (full detail)

    So CHARACTER runs 0..100% of the available quefrency range, and defaults
    high rather than low. At 0% you get the old room-capture behaviour; at
    100% you get puriFIR.

    Credit: the minimum-phase-sample-as-IR technique was popularised by
    puriFIR (Patrick "Kallyn" Kallenbach, 2025; Max port by aptrn), itself
    building on discussion by mystran, Z1202 and AnalogGuy1 on KVR.

    --------------------------------------------------------------------------
    SIGNAL FLOW
    --------------------------------------------------------------------------

      source sample -> crop region -> FFT -> log magnitude
                                              |
                                          cepstrum
                                              |
                                      lifter (CHARACTER)
                                              |
                            minimum- or linear-phase reconstruction
                                              |
        length / decay -> stretch -> reverse -> low & high cut -> tilt
                                              |
                                     predelay -> width
                                              |
                           normalise (peak or energy for gain-matched A/B)
                                              |
                                    juce::dsp::Convolution
                                              |
                          input -> convolve -> mix -> output

    IR construction runs on a background thread; the finished buffer is handed
    to the convolver on the message thread. Nothing allocates on the audio
    thread.

    Everything lives in these two .h/.cpp pairs so it drops straight into a
    Projucer project (add modules: juce_audio_basics, juce_audio_devices,
    juce_audio_formats, juce_audio_plugin_client, juce_audio_processors,
    juce_audio_utils, juce_core, juce_data_structures, juce_dsp,
    juce_events, juce_graphics, juce_gui_basics, juce_gui_extra).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <complex>

namespace irforge
{
    constexpr int kMinFFTOrder = 10;
    // Was 19 (~12s @44.1kHz) — that silently truncated any crop longer than
    // that, discarding the rest with an unwindowed edge. 23 (~190s @44.1kHz,
    // ~95s @96kHz) comfortably covers the full kMaxLoadWindowSeconds (180s),
    // so a crop spanning the whole resident window never gets truncated.
    // Going further is possible (juce::dsp::FFT supports it) but each rebuild
    // at the ceiling does several FFTs of that size on the background build
    // thread — at 23 that's on the order of half a second or so per rebuild
    // (roughly double the cost at 22), which is fine since it never touches
    // the audio thread, but dragging a knob at max FFT order will feel
    // noticeably less snappy than at the default of 15.
    // Bump this further if you want more and don't mind that lag.
    constexpr int kMaxFFTOrder = 23;

    namespace pid
    {
        static const juce::String character = "character";    // lifter, 0..1
        static const juce::String fftOrder = "fftorder";
        static const juce::String irLength = "irlength";     // ms
        static const juce::String decayShape = "decayshape";   // extra exponential decay
        static const juce::String linearPhase = "linearphase";
        static const juce::String stretch = "stretch";
        static const juce::String reverse = "reverse";
        static const juce::String predelay = "predelay";     // ms
        static const juce::String lowCut = "lowcut";       // Hz, on the IR
        static const juce::String highCut = "highcut";      // Hz, on the IR
        static const juce::String tilt = "tilt";         // dB/oct
        static const juce::String width = "width";
        static const juce::String cropStart = "cropstart";    // 0..1
        static const juce::String cropEnd = "cropend";      // 0..1
        static const juce::String mix = "mix";
        static const juce::String gain = "gain";         // dB
        static const juce::String gainMatch = "gainmatch";
        static const juce::String bypass = "bypass";
    }

    //==========================================================================
    // Settings snapshot handed to the build thread, so the builder never reads
    // parameters directly and cannot race the audio thread.
    //==========================================================================
    struct BuildSettings
    {
        float character = 0.85f;
        int   fftOrder = 15;
        float irLengthMs = 400.0f;
        float decayShape = 0.0f;
        bool  linearPhase = false;
        float stretch = 1.0f;
        bool  reverse = false;
        float predelayMs = 0.0f;
        float lowCutHz = 20.0f;
        float highCutHz = 20000.0f;
        float tiltDbOct = 0.0f;
        float width = 1.0f;
        float cropStart = 0.0f;
        float cropEnd = 1.0f;
        double sampleRate = 44100.0;

        bool operator!= (const BuildSettings& o) const
        {
            return character != o.character || fftOrder != o.fftOrder
                || irLengthMs != o.irLengthMs || decayShape != o.decayShape
                || linearPhase != o.linearPhase || stretch != o.stretch
                || reverse != o.reverse || predelayMs != o.predelayMs
                || lowCutHz != o.lowCutHz || highCutHz != o.highCutHz
                || tiltDbOct != o.tiltDbOct || width != o.width
                || cropStart != o.cropStart || cropEnd != o.cropEnd
                || sampleRate != o.sampleRate;
        }
    };

    //==========================================================================
    // The forge itself. Pure DSP, no JUCE audio-thread contact.
    //==========================================================================
    class IRBuilder
    {
    public:
        // source: the loaded sample. Returns a stereo IR buffer.
        static juce::AudioBuffer<float> build(const juce::AudioBuffer<float>& source,
            double sourceRate,
            const BuildSettings& s);

    private:
        static void minimumPhaseFromMagnitude(const std::vector<float>& logMag,
            std::vector<float>& out,
            int fftSize, int lifterCutoff,
            bool linearPhase);
        static void applyPostFilters(juce::AudioBuffer<float>& ir,
            const BuildSettings& s);
        static void resampleInPlace(juce::AudioBuffer<float>& buf, float ratio);
    };
}

//==============================================================================
class IRForgeAudioProcessor : public juce::AudioProcessor,
    private juce::Thread,
    private juce::AsyncUpdater
{
public:
    IRForgeAudioProcessor();
    ~IRForgeAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //--- sample handling ---------------------------------------------------
    //
    // Only a bounded window of a source file is ever brought into memory at
    // once. windowStartSeconds picks where in the file that window begins;
    // loadSourceFile re-reads from disk each time it's called, so as the
    // crop is dragged around a long file, ensureWindowCovers() re-reads a
    // fresh window a scrub at a time — the editor never needs its own UI for
    // this. The FFT/DSP cost of a build doesn't scale with this (fftOrder is
    // capped independently of loaded length), so the cap here is really
    // about memory and load time, not analysis CPU.
    static constexpr double kMaxLoadWindowSeconds = 180.0;

    bool loadSourceFile(const juce::File& file, double windowStartSeconds = 0.0);

    // Makes sure the resident window (the audio IRBuilder actually gets)
    // covers [startSeconds, endSeconds] of the source file, reloading a new
    // window from disk if it currently doesn't. No-op (and returns false)
    // for sources that are already fully resident, e.g. a recorded take.
    // Called from the editor once a crop drag finishes.
    bool ensureWindowCovers(double startSeconds, double endSeconds);

    void clearSource();
    bool hasSource() const noexcept { return sourceBuffer.getNumSamples() > 0; }
    juce::String getSourceName() const { return sourceName; }
    double getSourceRate() const noexcept { return sourceRate; }

    // Whether the current source is backed by a file on disk (and therefore
    // only ever partially resident) as opposed to a fully in-memory take
    // (a recording, or nothing loaded).
    bool isFileBacked() const noexcept { return fileBacked.load(std::memory_order_relaxed); }

    // Whole-timeline duration and how much of it is actually loaded right
    // now. For a file-backed source these differ once the file is longer
    // than kMaxLoadWindowSeconds; for a recording the window is the whole
    // buffer, so they're equal.
    double getSourceFileTotalSeconds() const noexcept { return sourceFileTotalSeconds; }
    double getSourceWindowStartSeconds() const noexcept { return sourceWindowStartSeconds; }
    double getResidentWindowSeconds() const noexcept { return windowLenSecondsAtomic.load(std::memory_order_relaxed); }
    double getMaxLoadWindowSeconds() const noexcept { return kMaxLoadWindowSeconds; }

    // Bumped whenever sourceBuffer, or the overview below, is replaced —
    // lets the editor detect a change without polling sample counts (which
    // can stay the same across a windowed reload).
    int getSourceGeneration() const noexcept { return sourceGeneration.load(std::memory_order_relaxed); }

    // read-only access for the waveform display. Guarded by sourceLock.
    juce::CriticalSection sourceLock;
    const juce::AudioBuffer<float>& getSourceBuffer() const noexcept { return sourceBuffer; }

    // A fixed-resolution min/max overview of the *whole* timeline (the whole
    // file, or the whole recorded take) — independent of how much of it is
    // actually resident in sourceBuffer. This is what the editor draws, so
    // the waveform is always shown in full even though only a bounded window
    // around the crop is ever loaded into memory at once. Guarded by
    // sourceLock, rebuilt whenever a new source is loaded (not on every
    // windowed reload triggered by ensureWindowCovers).
    const std::vector<float>& getOverviewMin() const noexcept { return overviewMin; }
    const std::vector<float>& getOverviewMax() const noexcept { return overviewMax; }
    double getOverviewTotalSeconds() const noexcept { return overviewTotalSeconds; }

    // finished IR, for the editor's display. Guarded by irLock.
    juce::CriticalSection irLock;
    const juce::AudioBuffer<float>& getDisplayIR() const noexcept { return displayIR; }

    // Bumped every time a newly-built IR is installed, so the editor can
    // detect a rebuild even when the new IR happens to have the same
    // sample count as the old one (e.g. Character, FFT size, Decay).
    std::atomic<int> irGeneration{ 0 };

    bool saveIRToFile(const juce::File& file);

    void requestRebuild() { rebuildFlag.store(true); notify(); }
    std::atomic<bool> building{ false };
    std::function<void()> onIRUpdated;

    //--- recording -----------------------------------------------------
    // Captures whatever is coming into the plugin's input so it can be used
    // as a source sample directly, without leaving the plugin to record
    // elsewhere first. While armed, the convolver is skipped entirely
    // (processBlock passes audio through dry) so you're always hearing the
    // real input while it's captured.
    bool startRecording();
    void stopRecording();                              // halts capture immediately
    void finalizeRecording(const juce::File& fileToSaveTo); // writes file (if valid) and loads the take as the source
    bool isRecordingNow() const noexcept { return recording.load(); }

private:
    void run() override;                 // build thread
    void handleAsyncUpdate() override;   // hand finished IR to the convolver
    irforge::BuildSettings gatherSettings() const;

    juce::AudioFormatManager formatManager;

    juce::AudioBuffer<float> sourceBuffer;
    double sourceRate = 44100.0;
    juce::String sourceName;
    std::atomic<int> sourceGeneration{ 0 }; // bumped whenever sourceBuffer is replaced

    // Whole-timeline overview for display, guarded by sourceLock alongside
    // sourceBuffer. Rebuilt only when a genuinely new source is loaded.
    std::vector<float> overviewMin, overviewMax;
    double overviewTotalSeconds = 0.0;

    juce::File currentSourceFile;              // empty if source came from recording, not a file
    double sourceFileTotalSeconds = 0.0;
    double sourceWindowStartSeconds = 0.0;

    // Mirrors of currentSourceFile/sourceFileTotalSeconds/sourceWindowStartSeconds
    // that are safe to read from the build thread inside gatherSettings(),
    // since the plain members above are only ever touched on the message
    // thread. Updated at the same points those members are.
    std::atomic<bool> fileBacked{ false };
    std::atomic<double> windowStartSecondsAtomic{ 0.0 };
    std::atomic<double> windowLenSecondsAtomic{ 0.0 };
    std::atomic<double> fileTotalSecondsAtomic{ 0.0 };

    // isNewFile: true for a genuinely new load (resets crop and rebuilds the
    // overview), false when only shifting the resident window to follow an
    // existing crop selection (ensureWindowCovers).
    bool loadSourceFileInternal(const juce::File& file, double windowStartSeconds, bool isNewFile);
    static void buildOverviewFromReader(juce::AudioFormatReader& reader,
        std::vector<float>& mn, std::vector<float>& mx);
    static void buildOverviewFromBuffer(const juce::AudioBuffer<float>& buf,
        std::vector<float>& mn, std::vector<float>& mx);

    juce::AudioBuffer<float> pendingIR, displayIR;
    juce::CriticalSection pendingLock;
    std::atomic<bool> pendingReady{ false };
    std::atomic<bool> rebuildFlag{ false };

    irforge::BuildSettings lastBuilt;

    juce::dsp::Convolution convolution;
    juce::dsp::ProcessSpec spec{ 44100.0, 512, 2 };
    bool convolutionReady = false;

    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;
    float irEnergyGain = 1.0f;   // for gain-matched A/B
    double currentTailSeconds = 1.0;

    // recording: preallocated on the message thread when armed, written to
    // from the audio thread with no locking or allocation, so it's safe to
    // start alongside the running convolver.
    static constexpr double kMaxRecordSeconds = 600.0; // 10 minutes
    std::atomic<bool> recording{ false };
    juce::AudioBuffer<float> recordBuffer;
    std::atomic<int> recordWritePos{ 0 };
    double recordSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRForgeAudioProcessor)
};