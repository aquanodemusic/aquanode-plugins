#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>

//==============================================================================
static constexpr int kNumNotes    = 12;
static constexpr int kNumOctaves  = 11;
static constexpr int kTotalNotes  = kNumNotes * kNumOctaves;   // 132
static constexpr int kMaxChannels = 2;

// Maximum biquad stages cascaded per note for dampening
static constexpr int kMaxDampenStages = 4;
static constexpr double kMaxDampenPerStage = -32.0; // dB per biquad stage

static const juce::StringArray kNoteNames
    { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

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

// Low-pass biquad (variable Q) for high-cut
Biquad makeLowPassQ (double fc, double sampleRate, double Q) noexcept;
// Low-pass biquad (Butterworth 2nd order) — kept for compat
Biquad makeLowPass  (double fc, double sampleRate) noexcept;
// Peak EQ with frequency-proportional Q (constant semitone bandwidth)
Biquad makePeakingEQ (double fc, double sampleRate, double gainDB, double bwSemitones) noexcept;
// High-shelf biquad
Biquad makeHighShelf (double fc, double sampleRate, double gainDB) noexcept;

//==============================================================================
class PitchControlEQAudioProcessor : public juce::AudioProcessor
{
public:
    PitchControlEQAudioProcessor();
    ~PitchControlEQAudioProcessor() override;

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

    // MIDI Mode — when enabled, held MIDI notes drive the note_active params
    static juce::String midiModeParamID() { return "midi_mode"; }
    void setMidiMode (bool enabled) noexcept
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(midiModeParamID())))
            p->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
    }
    bool getMidiMode() const noexcept
    {
        auto* v = apvts.getRawParameterValue(midiModeParamID());
        return v && v->load(std::memory_order_relaxed) > 0.5f;
    }

    // Held note counts per note class — editor resets these when MIDI mode turns off
    std::array<std::atomic<int>, kNumNotes> m_midiHeldCount {};
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
    static juce::String dampenDBParamID()      { return "dampen_db"; }
    static juce::String dampenQParamID()       { return "dampen_q"; }
    static juce::String rangeFromParamID()     { return "range_from"; }
    static juce::String rangeToParamID()       { return "range_to"; }
    static juce::String boostDBParamID()       { return "boost_db"; }
    static juce::String boostQParamID()        { return "boost_q"; }
    static juce::String outputGainParamID()    { return "output_gain"; }
    // New global-bell params
    static juce::String globalBellDBParamID()   { return "global_bell_db"; }
    static juce::String globalBellBWParamID()   { return "global_bell_bw"; }
    static juce::String globalBellFreqParamID() { return "global_bell_freq"; }

    static juce::String chorusRateParamID()      { return "chorus_rate"; }
    static juce::String chorusDepthParamID()     { return "chorus_depth"; }
    static juce::String chorusMixParamID()       { return "chorus_mix"; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==========================================================================
    // Per-note cascade: up to kMaxDampenStages biquads for dampening
    std::array<std::array<Biquad, kMaxDampenStages>, kTotalNotes> m_filters;
    std::array<std::array<Biquad::State, kMaxDampenStages>, kTotalNotes> m_states[kMaxChannels];

    // Boost filters — processed on a separate dry branch, added back additively
    std::array<Biquad, kTotalNotes> m_boostFilters;
    std::array<std::array<Biquad::State, kTotalNotes>, kMaxChannels> m_boostStates;



    // Global bell — independent dry branch, added back additively
    Biquad        m_globalBell;
    Biquad::State m_globalBellState[kMaxChannels];

    // Chorus — modulated delay line applied to boost delta
    static constexpr int kChorusMaxDelaySamples = 4096; // ~85ms @ 48kHz
    std::array<std::vector<double>, kMaxChannels> m_chorusBuf;
    std::array<int,    kMaxChannels> m_chorusWritePos {};
    std::array<double, kMaxChannels> m_chorusLfoPhase {};
    double m_chorusLfoPhaseInc { 0.0 };

    // Active flags
    std::array<int,  kTotalNotes> m_filterStages {};
    std::array<bool, kTotalNotes> m_boostActive  {};
    bool m_anyActive    { false };
    bool m_globalBellOn { false };

    void rebuildFilters();

    //==========================================================================
    std::atomic<bool> m_dirty { true };

    struct ParamListener : public juce::AudioProcessorValueTreeState::Listener
    {
        PitchControlEQAudioProcessor& owner;
        explicit ParamListener (PitchControlEQAudioProcessor& o) : owner (o) {}
        void parameterChanged (const juce::String&, float) override
        {
            owner.m_dirty.store (true, std::memory_order_relaxed);
        }
    };
    ParamListener m_paramListener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchControlEQAudioProcessor)
};
