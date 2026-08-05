#pragma once

#include <JuceHeader.h>
#include <array>
#include <complex>

//==============================================================================
class PhorestAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    PhorestAudioProcessor();
    ~PhorestAudioProcessor() override;

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
    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

    // Frequency response data for EQ curve display
    struct FrequencyResponse
    {
        static constexpr int numPoints = 512;
        std::array<float, numPoints> magnitudes;
        std::array<float, numPoints> frequencies;
        float currentLFOPhase = 0.0f;

        FrequencyResponse()
        {
            magnitudes.fill(1.0f);
            // Initialize with logarithmic frequency spacing
            for (int i = 0; i < numPoints; ++i)
            {
                float normalized = i / static_cast<float>(numPoints - 1);
                frequencies[i] = 20.0f * std::pow(1000.0f, normalized); // 20Hz to 20kHz
            }
        }
    };

    FrequencyResponse getFrequencyResponse();

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Improved Phaser DSP state using proper allpass filters
    struct PhaserState
    {
        std::array<float, 48> z1; // Previous sample for each allpass stage (up to 48 stages)
        float lfoPhase = 0.0f;
        float feedbackSample = 0.0f;

        PhaserState()
        {
            z1.fill(0.0f);
        }
    };

    PhaserState leftChannel;
    PhaserState rightChannel;

    double currentSampleRate = 44100.0;

    // Thread-safe storage for current parameters (for visualization)
    juce::CriticalSection responseLock;
    FrequencyResponse currentResponse;

    // Process a single sample through the phaser
    // lfoValue: [0,1], maxFreqHz: upper bound of the sweep in Hz (from freqRange param)
    float processSample(float input, PhaserState& state, float lfoValue,
        int numStages, float feedback, float detune, float maxFreqHz);

    // Calculate allpass coefficient from frequency (Hz)
    float calculateAllpassCoeff(float frequencyHz);

    // LFO shape generation
    float generateLFO(float phase, int shape);

    // Update frequency response for visualization
    void updateFrequencyResponse(float lfoValue, int stages, float feedback);

    // Calculate complex response at a specific frequency
    std::complex<float> calculateComplexResponse(float testFreq, float lfoValue, int stages, float feedback);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhorestAudioProcessor)
};