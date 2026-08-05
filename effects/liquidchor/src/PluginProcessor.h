/*
  ==============================================================================
    LiquidChor - Roland Juno BBD Chorus
    PluginProcessor.h
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class LiquidChorAudioProcessor : public juce::AudioProcessor
{
public:
    LiquidChorAudioProcessor();
    ~LiquidChorAudioProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi()  const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    double currentSampleRate = 44100.0;

    // Stereo delay buffers
    std::vector<float> delayBufL, delayBufR;
    int delayBufSize = 0;
    int writePos     = 0;

    // LFO state (phases normalised 0..1)
    float lfoPhaseL = 0.0f;
    float lfoPhaseR = 0.0f;

    // Feedback last sample
    float fbL = 0.0f, fbR = 0.0f;

    // High-pass filter state (1-pole RC)
    float hpPrevInL = 0.0f, hpPrevInR = 0.0f;
    float hpStateL  = 0.0f, hpStateR  = 0.0f;

    // Low-pass filter state (1-pole, on wet signal)
    float lpStateL = 0.0f, lpStateR = 0.0f;

    // Noise LP filter state (for lush, warm hiss)
    float noiseLpL = 0.0f, noiseLpR = 0.0f;

    // Noise gate envelope follower
    float gateEnv = 0.0f;

    // Random generator for noise
    juce::Random rng;

    // Transport tracking (for startPhase reset)
    bool wasPlaying = false;

    // Helper: evaluate LFO  (type 0=sine, 1=saw) → [-1, +1]
    static float evaluateLfo (float phase, int type) noexcept;

    // Helper: linear-interpolated read from a circular buffer.
    float readDelayInterp (const std::vector<float>& buf, float delaySamples) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiquidChorAudioProcessor)
};
