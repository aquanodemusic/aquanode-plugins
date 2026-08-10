#pragma once

#include <JuceHeader.h>

//==============================================================================
// Number of time slots in the drawn curve buffer (horizontal resolution)
// Height is full float precision — no quantisation there.
static constexpr int CURVE_RESOLUTION = 2048;

// Declick fade length in samples at 44100 – scales with actual SR
static constexpr int DECLICK_SAMPLES = 256;   // longer fade = cleaner transitions

class SignalControlAudioProcessor : public juce::AudioProcessor,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    SignalControlAudioProcessor();
    ~SignalControlAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // APVTS
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==============================================================================
    // Curve data: CURVE_RESOLUTION floats, each in [0,1] or -1.0f for "hole"
    // Written from editor thread, read from audio thread — atomic per slot.
    std::array<std::atomic<float>, CURVE_RESOLUTION> curveData;

    // Curve transform operations (called from editor / message thread)
    void mirrorCurveX();   // flip horizontally (time axis)
    void mirrorCurveY();   // flip vertically (value axis), holes stay holes
    void shiftCurveX(int slots);   // rotate left/right, wrapping at edges
    void shiftCurveY(float delta); // shift values up/down, clamping at [0,1], holes stay holes

    // Current scan position [0,1) — readable by editor for playhead
    std::atomic<float> scanPosition{ 0.0f };

    // Current raw curve value at scan head (-1 = hole)
    std::atomic<float> currentCV{ -1.0f };

    // Flag: set by editor while a multi-slot interpolated stroke write is
    // in progress (drawing/erasing a segment between two points).
    // Audio thread reads this to suppress gate-transition declick restarts
    // during bulk writes, preventing the mouseUp/drag glitch.
    std::atomic<bool> curveWriteInProgress{ false };

private:
    //==============================================================================
    double currentSampleRate = 44100.0;
    double phaseAccumulator = 0.0;

    // Output smoothing (one-pole IIR) — used for CV channel only
    float smoothedCV = 0.0f;
    float smoothedGate = 0.0f;

    // Declick envelope state — tracks gate transitions to avoid pops
    // declickCounter > 0 means we are fading; declickDir: +1 fade-in, -1 fade-out
    int   declickCounter = 0;
    int   declickDir = 0;
    bool  prevGateOn = false;
    float declickGain = 0.0f;   // current envelope gain [0,1]

    // Previous sample's rawCV — used to detect transitions more stably
    float prevRawCV = -1.0f;

    // Cached param values
    std::atomic<float> cachedRate{ 0.25f };
    std::atomic<float> cachedTempoSync{ 0.0f };
    std::atomic<float> cachedDivision{ 4.0f };

    // Cached pointer to the "cvOut" parameter, updated from the audio thread
    // at the end of every block so hosts (e.g. FL Studio's Patcher) can read
    // the current CV value as a modulation source.
    juce::RangedAudioParameter* cvOutParam = nullptr;

    double getSamplesPerCycle(double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalControlAudioProcessor)
};