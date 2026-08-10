#pragma once

#include <JuceHeader.h>
#include <atomic>

class AudioRerouterPluginAudioProcessor : public juce::AudioProcessor
{
public:
    AudioRerouterPluginAudioProcessor();
    ~AudioRerouterPluginAudioProcessor() override;

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
    void setStateInformation(const void* data, int sizeInBytes) override; // Fixed: was const char*

    juce::AudioProcessorValueTreeState apvts;

    // ── Shared state across all instances of this plugin ────────────────────
    // One set of 8 virtual channels. The transmitter on channel N writes here;
    // the receiver on channel N reads from here.
    //
    // Each channel uses a lock-protected stereo ring buffer (FIFO) so that
    // transmitters and receivers can run with different — or variable — block
    // sizes without data corruption or silent output.  This is the correct fix
    // for FL Studio's "use fixed size buffer" option being off: when that option
    // is disabled the host may call processBlock with varying sample counts, but
    // the ring buffer handles any combination transparently.
    //
    // kRingCapacity must exceed the host's maximum possible block size.
    // 16 384 samples ≈ 370 ms at 44.1 kHz, comfortably above any DAW maximum.
    //
    // Invariant: (ringWritePos[i] - ringReadPos[i] + kRingCapacity) % kRingCapacity
    //            gives the number of unread samples currently stored.
    //            One slot is always left vacant so that "full" and "empty" are
    //            distinguishable, giving a usable capacity of kRingCapacity - 1.
    //
    // NOTE: This relies on all instances living in the same process (the same
    // DLL load). FL Studio's plugin bridge / sandbox mode will isolate instances
    // into separate processes, breaking this completely. There is no in-process
    // remedy for that; it would require OS shared memory (CreateFileMapping /
    // shm_open), which is a much larger undertaking.
    static constexpr int             kRingCapacity = 16384; // must be > max DAW block size
    static juce::AudioBuffer<float>  ringBuffers[8];        // 2 ch × kRingCapacity per channel
    static juce::SpinLock            ringLocks[8];          // one SpinLock per channel
    static int                       ringWritePos[8];       // write head, protected by ringLocks
    static int                       ringReadPos[8];        // read head,  protected by ringLocks
    static std::atomic<bool>         channelReady[8];       // true once transmitter has written ≥1 block
    static std::atomic<bool>         channelHasTransmitter[8];
    static std::atomic<float>        channelPeakLevel[8];   // last peak from transmitter

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Per-instance limiter state
    float limiterGain = 1.0f;
    float releaseRate = 0.0f; // samples⁻¹, set in prepareToPlay

    // Per-instance dry-mix capture buffer (receive mode only).
    // Pre-allocated in prepareToPlay to stay heap-allocation-free on the audio thread.
    juce::AudioBuffer<float> dryBuffer;

    // Per-instance block size (needed for latency reporting)
    int currentBlockSize = 0;

    // Track our own channel + mode so the destructor can clean up transmitter flags
    int  lastChannel = -1;
    bool lastMode = false; // false = transmit

    // Cache last reported latency to avoid redundant setLatencySamples() calls
    int lastReportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRerouterPluginAudioProcessor)
};