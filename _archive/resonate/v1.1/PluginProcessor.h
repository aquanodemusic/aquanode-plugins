/*
  ==============================================================================

    Ableton-Style Resonator Plugin
    Features 5 independent comb filter Resonators + global Note control
    
    Core architecture: Feedback delay networks (comb filters)
    Sound character: Plucked string, Karplus-Strong style resonance
    
    Modes:
    - Mode A: Classic comb filter
    - Mode B: All-pass diffused Resonator
    
    Filter Types:
    - Lowpass, Highpass, Bandpass, Notch

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class ResonateAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    ResonateAudioProcessor();
    ~ResonateAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    //==============================================================================
    // Public interface for editor
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    
    enum FilterType
    {
        Lowpass = 0,
        Highpass,
        Bandpass,
        Notch
    };
    
    enum ProcessingMode
    {
        ModeA = 0,
        ModeB = 1
    };

private:
    //==============================================================================
    
    // Comb filter Resonator with feedback delay network
    struct Resonate
    {
        static constexpr int MAX_DELAY_SAMPLES = 8192;
        std::vector<float> delayBuffer[2];
        
        int writeIndex[2] = {0, 0};
        
        // Parameters
        double targetFrequency = 440.0;
        double delayInSamples = 100.0;
        double feedback = 0.9;
        
        int pitchSemitones = 0;    // Pitch in semitones relative to Note
        double fineDetune = 0.0;   // -50 to +50 cents
        double gain = 0.0;         // in dB
        bool enabled = true;
        
        // Mode-specific processing
        float allpassState[2] = {0.0f, 0.0f};
        float allpassCoeff = 0.7f;
        
        // Color/damping filter
        float lpfState[2] = {0.0f, 0.0f};
        float lpfCoeff = 1.0f;
        
        // Chorus LFO for pitch drift/beating
        double lfoPhase[2] = {0.0, 0.0};    // Separate phase per channel for stereo
        double lfoRate = 0.0;                // LFO frequency in Hz
        double lfoDepthCents = 0.0;          // Modulation depth in cents
        double currentSampleRate = 44100.0;
        
        void prepare(double sampleRate);
        void updateParameters(double sampleRate, double globalDecay, double globalNote, 
                            double color, ProcessingMode mode, bool constMode, double chorusAmount, int resonatorIndex,
                            double userLfoRate, double userLfoDepth);
        float processA(float input, int channel); // Mode A: Simple comb
        float processB(float input, int channel); // Mode B: All-pass comb
        void reset();
        
    private:
        float readDelayLinear(int channel, double delaySamples);
    };
    
    Resonate resonators[5];
    
    // State-variable filter (2-pole) - supports multiple filter types
    struct StateVariableFilter
    {
        float low[2] = {0.0f, 0.0f};
        float band[2] = {0.0f, 0.0f};
        float freq = 0.5f;
        float q = 0.7f;
        
        void setFrequency(float normalizedFreq, float resonance);
        float process(float input, int channel, FilterType type);
        void reset();
    };
    
    StateVariableFilter inputFilter;
    
    juce::AudioProcessorValueTreeState parameters;
    double currentSampleRate = 44100.0;
    
    // Global parameters
    double globalNote = 60.0;     // Base note for all Resonators (MIDI)
    double globalDecay = 85.0;    // Adjusted default for better range
    double globalColor = 50.0;    // 0-100, controls damping
    double globalSmooth = 0.0;    // 0-100, smoothing amount (0=max smooth, 100=no smooth)
    double globalChorus = 0.0;    // 0-100, chorus/beating amount
    bool filterEnabled = false;
    double filterFrequency = 1000.0;
    FilterType filterType = Lowpass;
    ProcessingMode processingMode = ModeA;
    bool constMode = false;       // Const decay mode
    bool wetOnly = false;         // Wet-only mode (no dry signal)
    
    // Smoothing filters for input/output
    float inputSmoothing[2] = {0.0f, 0.0f};
    float outputSmoothing[2] = {0.0f, 0.0f};
    
    void updateResonateParameters();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonateAudioProcessor)
};
