#pragma once
#include <JuceHeader.h>

/**
 *  ADelaySRProcessor  –  v3
 *
 *  Parameters
 *  ──────────────────────────────────────────────────────
 *  delayTimeMs       1–5000 ms, free-running
 *  timeSyncEnabled   lock to host BPM
 *  syncDivision      1/1 3/4 1/2 1/4 1/8 1/4T 1/8T
 *  attack            fade-in  [0–50 %] of delay window
 *  release           fade-out [0–50 %] of delay window (from end)
 *  tapVolume         per-tap level / feedback [0–100 %]
 *  noteDelayEnabled  sub-ms resonator mode
 *  noteDelayTimeMs   0.00001–1.0 ms, 5-decimal precision
 *  triggerEnabled    reset phase on each new note onset
 *  delayMode         0=Mono  1=Stereo  2=Ping Pong  3=Hard PP
 *  wetOnly           suppress dry signal (wet output only)
 */
class ADelaySRProcessor final : public juce::AudioProcessor
{
public:
    ADelaySRProcessor();
    ~ADelaySRProcessor() override;

    void prepareToPlay   (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock    (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()  const override { return true; }

    const juce::String getName()  const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override           { return false; }
    bool producesMidi() const override           { return false; }
    bool isMidiEffect() const override           { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int  getNumPrograms() override                       { return 1;  }
    int  getCurrentProgram() override                    { return 0;  }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //── Recording API (called from editor thread) ─────────────────────────────
    void startRecording();    // allocates buffer, sets flag
    void stopRecording();     // clears flag

    //── Buffer clear (called from editor thread) ──────────────────────────────
    void clearDelayBuffer();  // sets atomic flag; executed on audio thread

    double getSampleRate()       const noexcept { return currentSampleRate; }
    int    getRecordedSamples()  const noexcept { return recordPos.load (std::memory_order_relaxed); }

    std::atomic<bool>        isRecording    { false };
    juce::AudioBuffer<float> recordBuffer;            // written on audio thread while isRecording

private:
    static constexpr int kMaxDelaySamples = 960000;   // 5 s @ 192 kHz

    juce::AudioBuffer<float> delayBuffer;
    int    writePos   = 0;
    double cyclePhase = 0.0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDelay;
    double currentSampleRate = 44100.0;

    float inputEnv     = 0.0f;
    bool  wasTriggered = false;
    float atkCoeff     = 0.0f;
    float relCoeff     = 0.0f;
    int   hardPPTap    = 0;    // alternates L/R for Hard Ping Pong mode

    std::atomic<int> recordPos        { 0 };
    int              recordMaxSamples { 0 };
    std::atomic<bool> clearBufferRequested { false };
    float lastSmoothingMs { 50.0f };

    float targetDelayTimeSamples() const;
    static float tapEnvelope (float phase, float atkFrac, float relFrac) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ADelaySRProcessor)
};
