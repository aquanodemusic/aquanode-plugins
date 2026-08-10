/*
  ==============================================================================
    PitchControl – IIR Bell Filter Plugin
    Processor Header
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>

//==============================================================================
// Constants
static constexpr int kNumNotes   = 12;
static constexpr int kNumOctaves = 10;
static constexpr int kTotalNotes = kNumNotes * kNumOctaves; // 120  (C0 … B9)

static const juce::StringArray kNoteNames
    { "C","C#","D","D'","E","F","F#","G","G#","A","A#","B" };

//==============================================================================
// One second-order IIR peaking EQ section (Direct Form II Transposed)
// Coefficients: b0, b1, b2, a1, a2   (a0 = 1, normalised)
//==============================================================================
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;

    struct State { double s1 = 0.0, s2 = 0.0; };

    inline double process (double x, State& s) const noexcept
    {
        double y  = b0 * x + s.s1;
        s.s1 = b1 * x - a1 * y + s.s2;
        s.s2 = b2 * x - a2 * y;
        return y;
    }
    void reset (State& s) const noexcept { s.s1 = s.s2 = 0.0; }
};

// Build a peaking (bell) EQ biquad.  gainDB <= 0 gives a cut.
Biquad makePeakingEQ (double fc, double sampleRate, double gainDB, double Q) noexcept;

//==============================================================================
class PitchControlAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    PitchControlAudioProcessor();
    ~PitchControlAudioProcessor() override;

    //==============================================================================
    void prepareToPlay   (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName()              const override;
    bool acceptsMidi()                        const override;
    bool producesMidi()                       const override;
    bool isMidiEffect()                       const override;
    double getTailLengthSeconds()             const override;

    int            getNumPrograms()                              override;
    int            getCurrentProgram()                           override;
    void           setCurrentProgram  (int index)                override;
    const juce::String getProgramName (int index)                override;
    void           changeProgramName  (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData)       override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Public APVTS – editor attaches to this
    juce::AudioProcessorValueTreeState apvts;

    static juce::String noteActiveParamID (int i);  // i = 0…11
    static juce::String depthParamID();
    static juce::String qParamID();
    static juce::String rangeFromParamID();
    static juce::String rangeToParamID();

    static juce::String wetOnlyParamID() { return "wet_only"; }
    static juce::String boostDBParamID() { return "boost_db"; }
    static juce::String boostQParamID() { return "boost_q"; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    // 120 biquad filters, one per MIDI note (C0 … B9)
    std::array<Biquad, kTotalNotes> m_filters;
    
    // Boost filters for protected notes (peaking EQ with positive gain)
    std::array<Biquad, kTotalNotes> m_boostFilters;

    // Per-channel biquad states: m_states[channel][noteIndex]
    // Allocated in prepareToPlay for up to 2 channels
    static constexpr int kMaxChannels = 2;
    std::array<std::array<Biquad::State, kTotalNotes>, kMaxChannels> m_states;
    std::array<std::array<Biquad::State, kTotalNotes>, kMaxChannels> m_boostStates;

    // Which of the 120 filters is currently active (applied to signal)
    std::array<bool, kTotalNotes> m_filterActive {};
    std::array<bool, kTotalNotes> m_boostActive {};

    // At least one filter active (cached, avoids unnecessary work)
    bool m_anyActive { false };

    double m_sampleRate { 44100.0 };
    int    m_numChannels { 0 };

    // Recompute filter coefficients & active set from current parameter values
    void rebuildFilters();

    // Dirty flag: set by parameter listener, checked & cleared in processBlock
    std::atomic<bool> m_dirty { true };

    //==============================================================================
    struct ParamListener : public juce::AudioProcessorValueTreeState::Listener
    {
        PitchControlAudioProcessor& owner;
        explicit ParamListener (PitchControlAudioProcessor& o) : owner (o) {}
        void parameterChanged (const juce::String&, float) override
        {
            owner.m_dirty.store (true, std::memory_order_relaxed);
        }
    };
    ParamListener m_paramListener;

    bool m_wetOnly{ false };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchControlAudioProcessor)
};
