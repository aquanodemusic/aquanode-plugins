/*
  ==============================================================================
    ClipPreserve - Detail Preserving Clipper
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class ClipPreserveAudioProcessor : public juce::AudioProcessor
{
public:
    ClipPreserveAudioProcessor();
    ~ClipPreserveAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
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

    // Parameter layout
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "Parameters", createParameterLayout() };

    // GR metering for output level visualisation
    std::atomic<float> inputLevelL  { 0.f }, inputLevelR  { 0.f };
    std::atomic<float> outputLevelL { 0.f }, outputLevelR { 0.f };

private:
    // ---------- HP filter for delta chain (per channel) ----------
    // Two cascaded 1-pole RC high-pass filters ≈ 12 dB/oct, zero-delay
    struct SimpleHP
    {
        float z = 0.f;   // integrator state
        float process (float x, float a) noexcept
        {
            // a = exp(-2π·fc/fs)  →  coeff for the allpass / LP pole
            z = a * z + (1.f - a) * x;
            return x - z;           // HP = input - LP
        }
        void reset() { z = 0.f; }
    };

    // Two cascaded HP stages per channel gives ~12 dB/oct
    SimpleHP hp1L, hp2L, hp1R, hp2R;

    double currentSampleRate = 44100.0;

    // Helper: hard clip to [-threshold, +threshold]
    static float hardClip (float x, float threshold) noexcept
    {
        return juce::jlimit (-threshold, threshold, x);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipPreserveAudioProcessor)
};
