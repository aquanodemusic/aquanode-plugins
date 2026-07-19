/*
  ==============================================================================
    FrequencyDrawer — PluginProcessor.h
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>

//==============================================================================
struct DrawnEvent
{
    double time;
    double frequency;
    double amplitude;
};

struct SynthOscillator
{
    bool active = false;
    double phase = 0.0;
    double freq = 0.0;
    double amplitude = 0.0;
    double decayMult = 0.0;

    void trigger(double f, double a, double d)
    {
        freq = f;
        amplitude = a;
        decayMult = d;
        phase = 0.0;
        active = true;
    }

    float processSample(double sampleRate)
    {
        if (!active) return 0.0f;

        phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        float out = static_cast<float>(std::sin(phase) * amplitude);
        amplitude *= decayMult;

        if (amplitude < 1e-5)
            active = false;

        return out;
    }

    double getAmplitude() const { return amplitude; }
};

//==============================================================================
class FrequencyDrawerAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr double kDuration = 30.0;
    static constexpr double kFreqMin  = 20.0;
    static constexpr double kFreqMax  = 20000.0;
    static constexpr int    kMaxOsc   = 2048; // A safe pool size for dense strokes

    //==============================================================================
    FrequencyDrawerAudioProcessor();
    ~FrequencyDrawerAudioProcessor() override;

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
    // Editor-facing API
    //==============================================================================
    
    void addEvent(double time, double freq, double amplitude);
    void clearAllEvents();
    std::vector<DrawnEvent> getEventsCopy() const;

    void requestSeek(double seconds);
    void setPlaying(bool shouldPlay);

    void setNumHarmonics(int n) { numHarmonics_.store(n); }
    int getNumHarmonics() const { return numHarmonics_.load(); }

    void setBlurEnabled(bool b) { blurEnabled_.store(b); }
    void setBlurStrength(float s) { blurStrength_.store(s); }

    void triggerBackgroundRender();
    bool getIsRendering() const { return isRendering_.load(); }
    bool getIsPlaying() const { return playing_.load(); }
    double getPlayheadSeconds() const { return playheadSeconds_.load(); }

    bool exportToFlac(const juce::File& file);

private:
    juce::AudioBuffer<float> renderOffline(double targetSR);

    //==============================================================================
    std::atomic<double> sampleRate_{ 44100.0 };
    std::atomic<bool>   playing_{ false };

    std::atomic<double> playheadSeconds_{ 0.0 };
    int64_t             playheadSamples_{ 0 };

    std::atomic<bool>   seekPending_{ false };
    std::atomic<double> seekTarget_{ 0.0 };

    std::vector<DrawnEvent>       events_;
    mutable juce::CriticalSection eventsCS_;

    std::atomic<int>   numHarmonics_{ 1 };
    std::atomic<bool>  blurEnabled_{ false };
    std::atomic<float> blurStrength_{ 1.0f };

    std::atomic<bool> isRendering_{ false };
    std::atomic<bool> rerenderPending_{ false };
    std::atomic<int>  renderGeneration_{ 0 };

    std::unique_ptr<juce::AudioBuffer<float>> pendingBuffer_;
    std::unique_ptr<juce::AudioBuffer<float>> activeBuffer_;
    juce::CriticalSection                     bufferSwapCS_;
    std::atomic<bool>                         newBufferReady_{ false };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrequencyDrawerAudioProcessor)
};