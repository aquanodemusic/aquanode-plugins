#pragma once

#include <JuceHeader.h>

// Vorwärtsdeklaration
class FM12SynthAudioProcessor;

//==============================================================================
// FM12Voice: Die Synthesizer-Stimme
//==============================================================================
struct FM12Voice : public juce::SynthesiserVoice
{
    FM12Voice(FM12SynthAudioProcessor& processor);

    bool canPlaySound(juce::SynthesiserSound* sound) override;

    void startNote(int midiNoteNumber,
        float velocity,
        juce::SynthesiserSound* sound,
        int currentPitchWheelPosition) override;

    void stopNote(float velocity, bool allowTailOff) override;

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
        int startSample,
        int numSamples) override;

    void setCurrentPlaybackSampleRate(double newRate) override;

    // Statische Hilfsfunktionen für Parameter-IDs (damit der Processor sie nutzen kann)
    static inline juce::String opParamID(int op, const char* name);
    static inline juce::String opPhaseID(int op);
    static inline juce::String routeID(int from, int to);

    // Konstanten
    static constexpr int numOperators = 12;

private:
    FM12SynthAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;

    double sampleRate = 44100.0;
    int noteMidi = 60;
    float level = 1.0f;
    bool isVoiceActiveFlag = false;

    // Arrays für die Operatoren
    std::array<float, numOperators> opPhase{};
    std::array<float, numOperators> opOutput{};
    std::array<float, numOperators> opLastOutput{};
    std::array<juce::ADSR, numOperators> opEnvelopes;

    // Optimization structures
    struct Connection { int source; int dest; };
    std::vector<Connection> activeConnections;
    std::vector<int> activeOperatorIndices;
    bool isCarrier[12];

    // Temp buffer for processing
    float currentRawSamples[12];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM12Voice)
};

//==============================================================================
// FM12SynthAudioProcessor
//==============================================================================
class FM12SynthAudioProcessor : public juce::AudioProcessor
{
public:
    FM12SynthAudioProcessor();
    ~FM12SynthAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FM12 Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // APVTS muss public sein, damit Voices darauf zugreifen können
    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::Synthesiser synth;

    // Konstante für Polyphonie
    static constexpr int maxVoices = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM12SynthAudioProcessor)
};
