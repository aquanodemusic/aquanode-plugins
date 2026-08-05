#pragma once
#include <JuceHeader.h>

//==============================================================================
// Simple transposed-direct-form-II biquad.
//==============================================================================
struct Biquad
{
    float b0{}, b1{}, b2{}, a1{}, a2{};
    float z1{}, z2{};

    inline float process (float x) noexcept
    {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setBandpass (float fc, float Q, float sr) noexcept
    {
        fc = juce::jlimit (20.f, sr * 0.499f, fc);
        Q  = juce::jmax  (0.05f, Q);

        const float w0    = juce::MathConstants<float>::twoPi * fc / sr;
        const float sinW0 = std::sin (w0);
        const float cosW0 = std::cos (w0);
        const float alpha = sinW0 / (2.f * Q);
        const float inv   = 1.f / (1.f + alpha);

        b0 =  alpha * inv;
        b1 =  0.f;
        b2 = -alpha * inv;
        a1 = (-2.f * cosW0) * inv;
        a2 =  (1.f - alpha) * inv;
    }

    void reset() noexcept { z1 = z2 = 0.f; }
};

//==============================================================================
struct VocoderBand
{
    static constexpr int kMaxOrder = 4;

    Biquad modFilter [kMaxOrder];
    Biquad carFilterL[kMaxOrder];
    Biquad carFilterR[kMaxOrder];

    float  envelope   = 0.f;
    float  centerFreq = 440.f;

    std::atomic<float> displayLevel { 0.f };
};

//==============================================================================
class VocodeAudioProcessor  : public juce::AudioProcessor
{
public:
    //==========================================================================
    // Raised from 100 to 256. "256 bands" — a good-sounding number.
    // All arrays and loops use kMaxBands so nothing else needs to change.
    //==========================================================================
    static constexpr int kMaxBands = 256;

    VocodeAudioProcessor();
    ~VocodeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock  (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override          { return "Vocode"; }
    bool   acceptsMidi()  const override                 { return false; }
    bool   producesMidi() const override                 { return false; }
    double getTailLengthSeconds() const override         { return 0.0; }

    int  getNumPrograms()  override                      { return 1; }
    int  getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> bandLevels    [kMaxBands];
    float              bandwidthCurve[kMaxBands];

    // Per-band output-level multiplier drawn by the user in the display
    // (range 0.0 – 1.2).  Written from the GUI thread, read in processBlock.
    float              bandVolumeCurve[kMaxBands];

    // Set true each processBlock when no sidechain bus is active.
    // Read by the editor to show the AUTO-VOCODE indicator.
    std::atomic<bool>  isAutoVocoding { false };

private:
    void rebuildFilters();

    double currentSampleRate = 44100.0;
    int    currentNumBands   = 16;
    float  attackCoeff       = 0.f;
    float  releaseCoeff      = 0.f;
    float  compEnv           = 0.f;   // envelope follower for post-vocoder compressor

    VocoderBand bands[kMaxBands];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocodeAudioProcessor)
};
