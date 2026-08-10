#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>

/*  SpectrogramProcessor
    ====================
    Implements full time-frequency reassignment (Auger & Flandrin, 1995):

      • winH   – Hann window          → standard STFT
      • winDH  – d/dn Hann            → instantaneous-frequency estimator
      • winTH  – (n-N/2)·Hann         → group-delay estimator

    DECIMATION
    ----------
    8× decimation before the FFT gives an effective sample rate of ~5 513 Hz
    (at 44.1 kHz), so all 4 097 FFT bins land in the 20–2 000 Hz display
    range with ~0.67 Hz spacing.

    4-POLE BUTTERWORTH DECIMATION FILTER
    -------------------------------------
    Each ×2 stage uses a 4th-order Butterworth lowpass (fc = 0.225 × stage Fs)
    implemented as two cascaded Direct-Form-I biquad sections:

      Section A:  Q₁ = 1/(2·sin(π/8))  ≈ 1.3066  (high-Q Butterworth pole pair)
      Section B:  Q₂ = 1/(2·sin(3π/8)) ≈ 0.5412  (low-Q  Butterworth pole pair)

    WHY 4-POLE (vs original 2-pole)?
    ---------------------------------
    At the aliasing fold frequency (0.5 × stage Fs, Wc ratio ≈ 2.22):
      2-pole: |H|² ≈ −13.8 dB  → aliases fold back as phantom bands near C2
      4-pole: |H|² ≈ −30.6 dB  → aliases suppressed by an additional ~17 dB

    PER-FRAME GATE
    --------------
    Any FFT bin below `paramGateDeltaDB` dB relative to the frame peak is
    skipped before reassignment (exposed as a knob, default −40 dB).
*/
class SpectrogramProcessor : public juce::AudioProcessor
{
public:
    //==========================================================================
    static constexpr int   FFT_ORDER      = 13;
    static constexpr int   FFT_SIZE       = 1 << FFT_ORDER;    // 8 192
    static constexpr int   NUM_BINS       = FFT_SIZE / 2 + 1;  // 4 097
    static constexpr int   DECIMATION     = 8;
    static constexpr int   HOP_SIZE       = 32;
    static constexpr int   LOOKAHEAD_COLS = 4;
    static constexpr int   LOOKAHEAD_BUF  = 2 * LOOKAHEAD_COLS + 1;
    static constexpr int   MAX_COLUMNS    = 4096;
    static constexpr float FREQ_MIN       = 20.0f;
    static constexpr float FREQ_MAX       = 2000.0f;
    static constexpr int   DISPLAY_BINS   = 1024;

    SpectrogramProcessor();
    ~SpectrogramProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()                  const override { return true; }
    const juce::String getName()      const override { return "Spectra"; }
    bool acceptsMidi()                const override { return false; }
    bool producesMidi()               const override { return false; }
    double getTailLengthSeconds()     const override { return 0.0; }
    int    getNumPrograms()                 override { return 1; }
    int    getCurrentProgram()              override { return 0; }
    void   setCurrentProgram (int)          override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&)     override {}
    void setStateInformation (const void*, int)       override {}

    //==========================================================================
    using DisplayColumn = std::array<float, DISPLAY_BINS>;

    std::array<DisplayColumn, MAX_COLUMNS> ringBuffer;
    std::atomic<int>    writeHead            { 0 };
    std::atomic<int>    totalColumnsProduced { 0 };
    std::atomic<double> currentSampleRate    { 44100.0 };

    // ------------------------------------------------------------------
    //  User-facing display parameters — written by editor knobs.
    //
    //   paramGateDeltaDB : relative spectral gate  [−70 … −20 dB,  def −40]
    //   paramBrightGamma : display gamma            [0.3 … 1.2,    def 0.65]
    //   paramPeakDecay   : running-peak decay rate  [0.9990 … 1.0, def 0.99995]
    // ------------------------------------------------------------------
    std::atomic<float> paramGateDeltaDB { -40.0f   };
    std::atomic<float> paramBrightGamma {   0.65f  };
    std::atomic<float> paramPeakDecay   { 0.99995f };

private:
    juce::dsp::FFT fft;

    std::vector<float> winH, winDH, winTH;
    std::vector<float> inputRing;
    int inputWritePos   = 0;
    int samplesSinceHop = 0;

    std::vector<float> fftBufH, fftBufDH, fftBufTH;

    std::array<DisplayColumn, LOOKAHEAD_BUF> lookaheadBuf;
    int lookaheadPos = 0;

    // ------------------------------------------------------------------
    //  4-pole Butterworth decimator per stage
    //
    //  Section A (Q₁ ≈ 1.3066):  b0a / b1a / b2a,  a1a / a2a
    //  Section B (Q₂ ≈ 0.5412):  b0b / b1b / b2b,  a1b / a2b
    // ------------------------------------------------------------------
    struct BiquadState
    {
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
    };

    float decB0a = 0, decB1a = 0, decB2a = 0, decA1a = 0, decA2a = 0;
    float decB0b = 0, decB1b = 0, decB2b = 0, decA1b = 0, decA2b = 0;

    BiquadState decStageA[3];
    BiquadState decStageB[3];
    int         decCnt   [3] = { 0, 0, 0 };

    bool  feedDecimator (float x, float& out) noexcept;
    float runBiquadA    (BiquadState& s, float x) const noexcept;
    float runBiquadB    (BiquadState& s, float x) const noexcept;

    void buildWindows();
    void processFrame();
    static int freqToDisplayBin (float freqHz) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramProcessor)
};
