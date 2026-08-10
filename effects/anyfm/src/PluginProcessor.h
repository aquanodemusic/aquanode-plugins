#pragma once

#include <JuceHeader.h>

class AnyFMAudioProcessor : public juce::AudioProcessor
{
public:
    AnyFMAudioProcessor();
    ~AnyFMAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
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

    // APVTS – public so the editor can attach sliders/combos directly
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Metering
    float getCarrierLevel()   const { return carrierLevel.load(); }
    float getModulatorLevel() const { return modulatorLevel.load(); }
    float getOutputLevel()    const { return outputLevel.load(); }

private:
    double currentSampleRate = 44100.0;

    juce::AudioBuffer<float> delayBuffer;
    int writePosition = 0;

    // Per-channel smoothers for all continuously-varying parameters
    std::vector<juce::SmoothedValue<float>> modulationIndexSmoothed;
    std::vector<juce::SmoothedValue<float>> carrierGainSmoothed;
    std::vector<juce::SmoothedValue<float>> modulatorGainSmoothed;
    std::vector<juce::SmoothedValue<float>> dryWetSmoothed;

    // Fade-in state machine per channel.
    // 0 = SILENT   (env=0, waiting for signal)
    // 1 = FADING_IN (ramping up)
    // 2 = OPEN      (env=1.0 – resets to SILENT after 0.1s of continuous silence)
    int   carFadeState   [2] { 0, 0 };
    float carEnvelope    [2] { 0.0f, 0.0f };
    int   carSilenceCount[2] { 0, 0 };
    int   modFadeState   [2] { 0, 0 };
    float modEnvelope    [2] { 0.0f, 0.0f };
    int   modSilenceCount[2] { 0, 0 };

    // Per-channel feedback for Feedback (Experimental) mode
    float feedbackSample[2] { 0.0f, 0.0f };

    std::atomic<float> carrierLevel  { 0.0f };
    std::atomic<float> modulatorLevel{ 0.0f };
    std::atomic<float> outputLevel   { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnyFMAudioProcessor)
};
