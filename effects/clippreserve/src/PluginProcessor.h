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

private:
    // ---------- 1-pole RC filter (shared for HP and LP) ----------
    struct SimplePole
    {
        float z = 0.f;
        // Returns HP output: x - LP
        float processHP (float x, float a) noexcept
        {
            z = a * z + (1.f - a) * x;
            return x - z;
        }
        // Returns LP output
        float processLP (float x, float a) noexcept
        {
            z = a * z + (1.f - a) * x;
            return z;
        }
        void reset() { z = 0.f; }
    };

    // Two cascaded HP stages + two cascaded LP stages per channel
    SimplePole hp1L, hp2L, hp1R, hp2R;
    SimplePole lp1L, lp2L, lp1R, lp2R;

    // Oversampling: 2x using JUCE dsp module
    juce::dsp::Oversampling<float> oversampler { 2,   // num channels (stereo main)
                                                  1,   // factor (2^1 = 2x)
                                                  juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                  true, false };

    double currentSampleRate  = 44100.0;
    int    currentBlockSize   = 512;

    // Helper: hard clip to [-threshold, +threshold]
    static float hardClip (float x, float threshold) noexcept
    {
        return juce::jlimit (-threshold, threshold, x);
    }

    // Process one sample through the delta filter chain
    // hpOn / lpOn control whether each filter stage is active
    float processDelta (float delta,
                        SimplePole& s1hp, SimplePole& s2hp,
                        SimplePole& s1lp, SimplePole& s2lp,
                        float aHP, float aLP,
                        bool hpOn, bool lpOn) noexcept
    {
        float d = delta;
        if (hpOn) d = s2hp.processHP (s1hp.processHP (d, aHP), aHP);
        if (lpOn) d = s2lp.processLP (s1lp.processLP (d, aLP), aLP);
        return d;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipPreserveAudioProcessor)
};
