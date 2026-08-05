#pragma once
#include <JuceHeader.h>
#include <complex>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>

//==============================================================================
class SpectralEditProcessor : public juce::AudioProcessor,
    public juce::MidiKeyboardStateListener
{
public:
    //==========================================================================
    static constexpr int MAX_POLY = 16;

    // ── Runtime-configurable FFT parameters (default = 8192-pt, order 13) ───
    int fftOrder = 13;
    int fftSize = 1 << 13;          // 8192
    int hopSize = 1 << 12;          // 4096
    int numBins = (1 << 12) + 1;    // 4097

    int  getNumBins() const noexcept { return numBins; }
    int  getFftSize() const noexcept { return fftSize; }

    /** Change the FFT order (9–14) and re-analyse the loaded file. */
    void setFftOrder(int order);

    SpectralEditProcessor();
    ~SpectralEditProcessor() override;

    //==========================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()   const override { return true; }

    const juce::String getName()     const override { return "SpectralEdit"; }
    bool acceptsMidi()   const override { return true; }
    bool producesMidi()  const override { return false; }
    bool isMidiEffect()  const override { return false; }

    double getTailLengthSeconds() const override { return 4.0; }

    int  getNumPrograms()                       override { return 1; }
    int  getCurrentProgram()                    override { return 0; }
    void setCurrentProgram(int)                override {}
    const juce::String getProgramName(int)     override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int)   override {}

    //==========================================================================
    /**  Load a WAV (or other format) file, starting at startSec.
     *   Up to 60 seconds of audio is analysed. */
    void loadFile(const juce::File& file, double startSec = 0.0);

    bool         isFileLoaded() const noexcept { return fileLoaded.load(); }
    bool         isCompiling()  const noexcept { return compiling.load(); }
    juce::String getFileName()  const { return fileName; }

    /** Thread-safe copy of the synthesised buffer (for export). */
    juce::AudioBuffer<float> copySynthBuffer();

    /** Stop all playing voices immediately (called from UI thread or audio thread). */
    void stopAllVoices();

    //==========================================================================
    struct SelRect
    {
        int  x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool valid = false;

        int left()   const noexcept { return std::min(x0, x1); }
        int right()  const noexcept { return std::max(x0, x1); }
        int bottom() const noexcept { return std::min(y0, y1); }
        int top()    const noexcept { return std::max(y0, y1); }
    };

    //==========================================================================
    mutable std::mutex                                   spectralMutex;
    std::vector<std::vector<std::complex<float>>>        spectralData;
    int    numFrames = 0;
    double sourceSampleRate = 44100.0;

    //==========================================================================
    void applyMirrorLR(const SelRect& s);
    void applyMirrorUD(const SelRect& s);
    void applyDelete(const SelRect& s);
    void applyRotateBinsUp(const SelRect& s, int bins);
    void applyRotateRight(const SelRect& s, int frames);
    void applyChangeVolume(const SelRect& s, float pct);

    // ── Drawing tools ─────────────────────────────────────────────────────────
    /** Returns the maximum spectral magnitude (sqrt of max |complex|^2).
     *  Used by the draw tool to compute absolute amplitude from a 0..1 fraction. */
    float getMaxMagnitude() const noexcept;

    /** Paint a stroke at a single frame.
     *  @param frame       Frame index to paint into.
     *  @param binCenter   Centre frequency bin of the brush.
     *  @param halfThick   Half-width of the brush in bins (cosine-tapered).
     *  @param amplitude   Absolute complex magnitude to write (raw spectral units). */
    void applyPaintStroke(int frame, int binCenter, int halfThick, float amplitude);

    /** Apply a time-domain smear (moving average across frames) centred at
     *  (frame, binCenter) with the given radius.  Magnitude-preserving: the
     *  average is computed over magnitudes and the original per-frame phase is
     *  kept, so no level is lost during smearing. */
    void applySmearStroke(int frame, int binCenter, int radius);

    /** Spectral expansion on a selection.
     *  Calculates the average magnitude inside the selection, then for every bin:
     *    mag_new = mag_old * (mag_old / average)^strength
     *  Values above the average are pushed higher; values below are pushed lower.
     *  Phase is preserved.  @param strength  Exponent ≥ 0 (0 = no effect). */
    void applySpectralCompress(const SelRect& s, float strength);

    /** Spectral freeze: copies the first frame of the selection to every other
     *  frame inside the selection, erasing temporal variation. */
    void applyFreeze(const SelRect& s);

    void resynthesize();

    //==========================================================================
    juce::MidiKeyboardState keyboardState;

    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) override;
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float vel) override;

    // ── Playback state ────────────────────────────────────────────────────────

    /** 0.0–1.0 fraction along synthBuf that playback starts from. */
    std::atomic<float> startFraction{ 0.0f };

    /** Length of the current synthBuf in samples (safe to read lock-free). */
    std::atomic<int>   synthBufLen{ 0 };

    /** Number of "meaningful" audio samples in synthBuf = numFrames * hopSize.
     *  Use this (not synthBufLen) when converting startFraction to a sample
     *  offset so the fftSize-sized tail of zeros is excluded from the range. */
    std::atomic<int>   synthContentLen{ 0 };

    /** Current playback position as a 0–1 fraction of synthBuf.
     *  −1 when no voice is active. Updated every processBlock. */
    std::atomic<float> displayPlayhead{ -1.0f };

    // ── Scrub playback (audio-thread-driven, UI writes target) ───────────────
    /** Set true while the user is scrubbing (mouse held in Scrub mode). */
    std::atomic<bool>  scrubActive{ false };
    /** Normalised 0–1 target position the scrub playhead glides toward. */
    std::atomic<float> scrubTargetFraction{ 0.0f };
    /** Glide smoother – written on the audio thread, but the UI may call
     *  setCurrentAndTargetValue() on mouseDown to snap to the click position
     *  before the audio thread picks it up.  Writes must be guarded by
     *  synthMutex when called from the UI thread. */
    juce::SmoothedValue<double> scrubSmoothed;

    /** 0 = unlimited glide (original behaviour), 1 = max 1× playback speed.
     *  Values in-between linearly limit the maximum per-sample advance. */
    std::atomic<float> scrubMaxSpeed{ 0.0f };

    /** Written by the UI thread on mouseDown to snap the speed-limited scrub
     *  position; the audio thread reads it once and resets to -1. */
    std::atomic<double> scrubSnapPos{ -1.0 };

    float getPlayheadFraction() const noexcept { return displayPlayhead.load(); }

    // ── File / re-load state ──────────────────────────────────────────────────
    juce::File         lastLoadedFile;
    std::atomic<double> lastLoadStartSec{ 0.0 };

    // ── DAW transport tracking (audio thread only) ────────────────────────────
    std::atomic<bool>  transportWasPlaying{ false };

    std::mutex               synthMutex;

private:
    //==========================================================================
    std::vector<float> hannWin;
    void buildWindow();
    void analyzeAudio(const juce::AudioBuffer<float>& mono, double sr);

    juce::AudioBuffer<float> synthBuf;


    std::atomic<bool> fileLoaded{ false };
    std::atomic<bool> compiling{ false };
    juce::String      fileName;

    struct Voice
    {
        bool   on = false;
        int    note = 60;
        double pos = 0.0;
        double rate = 1.0;
    };
    Voice      voices[MAX_POLY];
    std::mutex voicesMutex;
    double     currentSR = 44100.0;

    /** Current position for the speed-limited scrub path (audio thread only). */
    double scrubCurrentPos = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralEditProcessor)
};