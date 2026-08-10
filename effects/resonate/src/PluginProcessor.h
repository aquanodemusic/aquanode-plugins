/*
  ==============================================================================
    Ableton-Style Resonator Plugin

    Additions in this version (sound engine preserved at defaults):
      - Per-resonator Pan (defaults reproduce the old hard-L/R routing exactly)
      - MIDI input: up to 5 simultaneous notes, round-robin to res I..V
      - Decay knob curve re-skewed so 90-100 gets ~45% of knob travel
      - Preset save/load to standalone XML files
      - Pre-allocated scratch buffers (no heap allocation on the audio thread)
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>

//==============================================================================
class ResonateAudioProcessor : public juce::AudioProcessor
{
public:
    ResonateAudioProcessor();
    ~ResonateAudioProcessor() override;

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

    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    enum FilterType { Lowpass = 0, Highpass, Bandpass, Notch };
    enum ProcessingMode { ModeA = 0, ModeB = 1 };

    //==========================================================================
    // Preset file I/O (standalone .xml, independent of host session state)
    bool savePresetToFile(const juce::File& file);
    bool loadPresetFromFile(const juce::File& file);
    static juce::File getDefaultPresetDirectory();

    //==========================================================================
    // MIDI: which note (if any) is currently driving each resonator.
    // Returns -1 when the resonator is following its knobs instead.
    // Safe to call from the message thread (atomic).
    int getMidiNoteForResonator(int index) const
    {
        return (index >= 0 && index < 5) ? midiDisplayNote[index].load() : -1;
    }

private:
    // =========================================================================
    struct Resonate
    {
        static constexpr int MAX_DELAY_SAMPLES = 8192;
        std::vector<float> delayBuffer[2];
        int writeIndex[2] = { 0, 0 };

        // Core comb-filter params
        double targetFrequency = 440.0;
        double delayInSamples = 100.0;
        double feedback = 0.9;
        int    pitchSemitones = 0;
        double fineDetune = 0.0;
        double gain = 0.0;
        bool   enabled = true;

        // Color/damping LPF
        float  lpfState[2] = { 0.0f, 0.0f };
        float  lpfCoeff = 1.0f;

        // Chorus / LFO
        double lfoPhase[2] = { 0.0, 0.0 };
        double lfoRate = 0.0;
        double lfoDepthCents = 0.0;
        double currentSampleRate = 44100.0;

        // Membrane modulator
        uint32_t noiseSeed[2] = { 12345u, 67890u };
        float    membraneFilter[2] = { 0.0f, 0.0f };
        double   membraneSamples = 0.0;

        // DC-offset accumulator
        float  dcEnv[2] = { 0.0f, 0.0f };
        bool   centerMode = true;

        // Exponential decay envelope (output-side shaping)
        float  expEnv[2] = { 0.0f, 0.0f };
        float  expCoeff = 0.9f;   // per-sample decay coeff, freq-scaled
        bool   expDecay = false;

        // Per-resonator decay/color/const
        double localDecay = 85.0;
        double localColor = 50.0;
        bool   localConst = false;
        bool   useLocalParams = false;

        void  prepare(double sampleRate);
        void  updateParameters(double sampleRate,
            double globalDecay, double globalNote,
            double color,
            ProcessingMode mode, bool constMode,
            double chorusAmount, int resonatorIndex,
            double userLfoRate, double userLfoDepth,
            bool   center,
            bool   useLocal,
            double lDecay, double lColor, bool lConst,
            double midiNote = -1.0);   // >= 0 overrides globalNote + pitchSemitones
        float processA(float input, int channel);
        float processB(float input, int channel);
        void  reset();

    private:
        float readDelayLinear(int channel, double delaySamples);

        float fastNoise(int channel)
        {
            noiseSeed[channel] = noiseSeed[channel] * 1664525u + 1013904223u;
            return static_cast<float>(static_cast<int32_t>(noiseSeed[channel]))
                / static_cast<float>(0x80000000);
        }
    };

    Resonate resonators[5];

    struct StateVariableFilter
    {
        float low[2] = { 0.0f, 0.0f };
        float band[2] = { 0.0f, 0.0f };
        float freq = 0.5f;
        float q = 0.7f;

        void  setFrequency(float normalizedFreq, float resonance);
        float process(float input, int channel, FilterType type);
        void  reset();
    };

    StateVariableFilter inputFilter;

    juce::AudioProcessorValueTreeState parameters;
    double currentSampleRate = 44100.0;

    double globalNote = 60.0;
    double globalDecay = 85.0;
    double globalColor = 50.0;
    double globalSmooth = 0.0;
    double globalChorus = 0.0;
    bool   filterEnabled = false;
    double filterFrequency = 1000.0;
    FilterType     filterType = Lowpass;
    ProcessingMode processingMode = ModeA;
    bool   constMode = false;
    bool   wetOnly = false;
    bool   centerMode = true;
    bool   perResMode = false;

    // Per-resonator pan, -100 (hard L) .. +100 (hard R)
    double resPan[5] = { 0.0, -100.0, 100.0, -100.0, 100.0 };

    float inputSmoothing[2] = { 0.0f, 0.0f };
    float outputSmoothing[2] = { 0.0f, 0.0f };

    // ── MIDI voice allocation ────────────────────────────────────────────────
    struct MidiVoice
    {
        int          note   = -1;    // note currently held in this slot
        bool         active = false; // is a key still down for it?
        juce::uint32 order  = 0;     // for oldest-first voice stealing
    };

    MidiVoice     voices[5];
    int           heldNote[5]  = { -1, -1, -1, -1, -1 }; // sticky: survives note-off
    juce::uint32  voiceCounter = 0;
    int           rrPointer    = 0;
    bool          midiEnabled  = true;

    std::atomic<int> midiDisplayNote[5];

    void handleMidiMessages(const juce::MidiBuffer& midiMessages);

    // Pre-allocated scratch (avoids allocating on the audio thread)
    juce::AudioBuffer<float> res1Buffer, res2to5Buffer, wetBuffer;

    void updateResonateParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResonateAudioProcessor)
};
