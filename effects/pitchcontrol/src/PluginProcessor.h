/*
  ==============================================================================
    PitchControl – IIR Bell Filter + FFT Spectral Shift Plugin
    Processor Header
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>

//==============================================================================
static constexpr int kNumNotes    = 12;
static constexpr int kNumOctaves  = 10;
static constexpr int kTotalNotes  = kNumNotes * kNumOctaves;   // 120
static constexpr int kMaxChannels = 2;

static const juce::StringArray kNoteNames
    { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

// FFT constants (fixed for FFT mode + spectrum analyser)
static constexpr int kFFTOrder   = 11;
static constexpr int kFFTSize    = 1 << kFFTOrder;    // 2048
static constexpr int kHopSize    = kFFTSize / 4;       // 512
static constexpr int kNumFFTBins = kFFTSize / 2 + 1;  // 1025

//==============================================================================
// IIR biquad (Direct Form II Transposed)
//==============================================================================
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;

    struct State { double s1 = 0.0, s2 = 0.0; };

    inline double process (double x, State& s) const noexcept
    {
        double y = b0 * x + s.s1;
        s.s1 = b1 * x - a1 * y + s.s2;
        s.s2 = b2 * x - a2 * y;
        return y;
    }
    void reset (State& s) const noexcept { s.s1 = s.s2 = 0.0; }
};

Biquad makePeakingEQ (double fc, double sampleRate, double gainDB, double Q) noexcept;

//==============================================================================
// Per-bin attraction mapping for FFT mode
//==============================================================================
struct BinMapping
{
    int   targetBin   { 0 };
    float blendWeight { 0.0f };
    bool  isProtected { false };
};

//==============================================================================
class PitchControlAudioProcessor : public juce::AudioProcessor
{
public:
    PitchControlAudioProcessor();
    ~PitchControlAudioProcessor() override;

    void prepareToPlay   (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName()              const override;
    bool acceptsMidi()                        const override;
    bool producesMidi()                       const override;
    bool isMidiEffect()                       const override;
    double getTailLengthSeconds()             const override;

    int  getNumPrograms()                              override;
    int  getCurrentProgram()                           override;
    void setCurrentProgram  (int index)                override;
    const juce::String getProgramName (int index)      override;
    void changeProgramName  (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData)        override;
    void setStateInformation (const void* data, int sizeInBytes)  override;

    //==========================================================================
    // Public APVTS
    juce::AudioProcessorValueTreeState apvts;
    double currentSampleRate { 44100.0 };

    // Parameter ID helpers
    static juce::String noteActiveParamID (int i);
    static juce::String depthParamID()            { return "depth_db"; }
    static juce::String qParamID()                { return "filter_q"; }
    static juce::String rangeFromParamID()        { return "range_from"; }
    static juce::String rangeToParamID()          { return "range_to"; }
    static juce::String wetOnlyParamID()          { return "wet_only"; }
    static juce::String boostDBParamID()          { return "boost_db"; }
    static juce::String boostQParamID()           { return "boost_q"; }
    static juce::String fftModeParamID()          { return "fft_mode"; }
    static juce::String fftMixParamID()           { return "fft_mix"; }
    static juce::String shiftStrengthParamID()    { return "shift_strength"; }
    static juce::String dampenUnprotectedParamID(){ return "dampen_unprotected"; }
    static juce::String electrifyParamID()        { return "electrify"; }
    static juce::String pullParamID()             { return "pull"; }
    static juce::String outputGainParamID()       { return "output_gain"; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    // Spectrum analyser data (message-thread safe snapshot)
    // Call from timer/paint thread; returns smoothed magnitudes for kNumFFTBins bins.
    void getSpectrumData (float* dest, int numBins) const;

private:
    //==========================================================================
    // IIR Filter Mode
    std::array<Biquad, kTotalNotes> m_filters;
    std::array<Biquad, kTotalNotes> m_boostFilters;

    std::array<std::array<Biquad::State, kTotalNotes>, kMaxChannels> m_states;
    std::array<std::array<Biquad::State, kTotalNotes>, kMaxChannels> m_boostStates;

    std::array<bool, kTotalNotes> m_filterActive {};
    std::array<bool, kTotalNotes> m_boostActive  {};
    bool m_anyActive { false };
    bool m_wetOnly   { false };

    void rebuildFilters();

    //==========================================================================
    // FFT Mode (STFT processing)
    std::unique_ptr<juce::dsp::FFT> m_fft;
    std::array<float, kFFTSize>     m_window;

    std::array<std::array<float, kFFTSize * 2>, kMaxChannels> m_inputFifo;
    std::array<std::array<float, kFFTSize * 2>, kMaxChannels> m_outputAccum;
    std::array<std::array<float, kFFTSize * 2>, kMaxChannels> m_fftWorkBuf;

    std::array<int, kMaxChannels> m_fifoWritePos  {};
    std::array<int, kMaxChannels> m_outputReadPos {};

    // Phase vocoder coherent-phase tracking (for Electrify)
    std::array<std::array<float, kNumFFTBins>, kMaxChannels> m_prevInputPhase  {};
    std::array<std::array<float, kNumFFTBins>, kMaxChannels> m_prevOutputPhase {};

    int m_samplesInHop    { 0 };
    int m_outputHopsReady { 0 };

    std::array<BinMapping, kNumFFTBins> m_binMap;
    std::atomic<bool> m_binMapDirty { true };

    void buildHannWindow();
    void rebuildBinMap();
    void processFFTHop (int numChannels);

    //==========================================================================
    // Spectrum analyser (always-on, both modes)
    // Separate lightweight FFT purely for display.
    std::unique_ptr<juce::dsp::FFT>     m_analyserFft;
    std::array<float, kFFTSize>         m_analyserFifo;       // mono input ring
    std::array<float, kFFTSize * 2>     m_analyserWorkBuf;
    int                                 m_analyserFifoPos { 0 };
    bool                                m_analyserReady   { false };

    // Double buffer: audio thread writes, message thread reads
    std::array<float, kNumFFTBins>      m_spectrumWrite {};
    mutable std::array<float, kNumFFTBins> m_spectrumRead  {};
    mutable juce::CriticalSection       m_spectrumLock;

    // Smoothing state (audio thread only)
    std::array<float, kNumFFTBins>      m_spectrumSmooth {};

    void pushSampleToAnalyser (float sample);    // called per-sample from processBlock
    void runAnalyserIfReady();                   // called when fifo is full

    //==========================================================================
    // Dirty flag + listener
    std::atomic<bool> m_dirty { true };

    struct ParamListener : public juce::AudioProcessorValueTreeState::Listener
    {
        PitchControlAudioProcessor& owner;
        explicit ParamListener (PitchControlAudioProcessor& o) : owner (o) {}
        void parameterChanged (const juce::String&, float) override
        {
            owner.m_dirty.store      (true, std::memory_order_relaxed);
            owner.m_binMapDirty.store(true, std::memory_order_relaxed);
        }
    };
    ParamListener m_paramListener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchControlAudioProcessor)
};
