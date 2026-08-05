#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Static member definitions ────────────────────────────────────────────────
// All zero/default initialised via static storage duration before any ctor runs.
// ringWritePos and ringReadPos are int (not atomic) because they are always
// accessed under the corresponding ringLocks[i] spinlock.
juce::AudioBuffer<float>  AudioRerouterPluginAudioProcessor::ringBuffers[8];
juce::SpinLock            AudioRerouterPluginAudioProcessor::ringLocks[8];
int                       AudioRerouterPluginAudioProcessor::ringWritePos[8]; // zero-init by static storage
int                       AudioRerouterPluginAudioProcessor::ringReadPos[8];  // zero-init by static storage
std::atomic<bool>         AudioRerouterPluginAudioProcessor::channelReady[8];
std::atomic<bool>         AudioRerouterPluginAudioProcessor::channelHasTransmitter[8];
std::atomic<float>        AudioRerouterPluginAudioProcessor::channelPeakLevel[8];

// ── Parameter layout ─────────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout
AudioRerouterPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>("mode", "Mode", false));
    params.push_back(std::make_unique<juce::AudioParameterInt>("channel", "Channel", 1, 8, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>("limiter", "Limiter", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("threshold", "Threshold",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.9f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("dry", "Dry Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("wet", "Wet Level",
        juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f));

    return { params.begin(), params.end() };
}

// ── Constructor / destructor ──────────────────────────────────────────────────
AudioRerouterPluginAudioProcessor::AudioRerouterPluginAudioProcessor()
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

AudioRerouterPluginAudioProcessor::~AudioRerouterPluginAudioProcessor()
{
    // When a transmitter instance is removed, clear its channel flags so
    // receivers stop reporting latency and output silence instead of stale data.
    if (lastChannel >= 0 && !lastMode)
    {
        channelHasTransmitter[lastChannel].store(false);
        channelPeakLevel[lastChannel].store(0.0f);
        channelReady[lastChannel].store(false);
    }
}

// ── prepareToPlay ─────────────────────────────────────────────────────────────
void AudioRerouterPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentBlockSize = samplesPerBlock;

    // 100 ms release time expressed as a per-sample increment toward unity
    releaseRate = 1.0f / (0.1f * static_cast<float> (sampleRate));
    limiterGain = 1.0f;

    // Force latency state to be re-evaluated on next processBlock
    lastReportedLatency = -1;

    // Pre-allocate the dry-mix capture buffer (used in receive mode).
    // Sized to kRingCapacity so it handles any block size the host may use.
    dryBuffer.setSize(2, kRingCapacity, false, true, true);

    // Pre-allocate all ring buffers here — never in processBlock — to keep
    // the audio thread heap-allocation-free.
    //
    // The ring buffers have a fixed capacity of kRingCapacity samples, which
    // is large enough to absorb any block size the host might use.  We use
    // avoidReallocating=true so that subsequent prepareToPlay calls from any
    // instance do not re-allocate memory that is already the right size.
    for (int i = 0; i < 8; ++i)
    {
        juce::SpinLock::ScopedLockType lock(ringLocks[i]);

        // setSize with avoidReallocating=true is a no-op if size is unchanged.
        // keepExistingContent=false and clearExtraSpace=true ensure the buffer
        // starts zeroed on first allocation.
        ringBuffers[i].setSize(2, kRingCapacity, false, true, true);
        ringBuffers[i].clear();
        ringWritePos[i] = 0;
        ringReadPos[i] = 0;
    }
}

void AudioRerouterPluginAudioProcessor::releaseResources() {}

// ── Bus layout ────────────────────────────────────────────────────────────────
bool AudioRerouterPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

// ── processBlock ──────────────────────────────────────────────────────────────
void AudioRerouterPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int   totalIn = getTotalNumInputChannels();
    const int   totalOut = getTotalNumOutputChannels();
    const int   numSamples = buffer.getNumSamples();

    // Read all parameters atomically once per block
    const bool  mode = apvts.getRawParameterValue("mode")->load() > 0.5f;
    const int   channel = juce::jlimit(0, 7,
        static_cast<int> (*apvts.getRawParameterValue("channel")) - 1);
    const bool  limiterOn = apvts.getRawParameterValue("limiter")->load() > 0.5f;
    const float threshold = apvts.getRawParameterValue("threshold")->load();
    const float dryGain   = apvts.getRawParameterValue("dry")->load();
    const float wetGain   = apvts.getRawParameterValue("wet")->load();

    // Remember for destructor cleanup
    lastChannel = channel;
    lastMode = mode;

    // ── TRANSMIT ─────────────────────────────────────────────────────────────
    if (!mode)
    {
        // Measure input peak so receivers can detect live signal
        float peak = 0.0f;
        for (int ch = 0; ch < totalIn; ++ch)
        {
            const float* data = buffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
                peak = std::max(peak, std::abs(data[s]));
        }
        channelPeakLevel[channel].store(peak);
        channelHasTransmitter[channel].store(true);

        // Write input into the ring buffer under lock.
        // The ring buffer is a FIFO of fixed capacity kRingCapacity.  If the
        // write would overflow (i.e. the receiver is falling behind), the oldest
        // unread data is silently discarded to make room — this is preferable to
        // corrupting the buffer or blocking the audio thread.
        //
        // Each write is split into at most two contiguous memcpy segments to
        // handle the wrap-around at the end of the circular buffer, avoiding
        // sample-by-sample loops.
        {
            juce::SpinLock::ScopedLockType lock(ringLocks[channel]);
            auto& rb = ringBuffers[channel];
            const int cap = kRingCapacity;
            int& wPos = ringWritePos[channel];
            int& rPos = ringReadPos[channel];

            // Clamp to ring capacity (defensive; no host should exceed this)
            const int n = std::min(numSamples, cap - 1);

            // If writing n samples would overflow, advance the read head by
            // exactly the deficit so that free space == n afterwards.
            const int used = (wPos - rPos + cap) % cap;
            const int free = cap - 1 - used;       // -1: one slot gap keeps full ≠ empty
            if (free < n)
                rPos = (rPos + (n - free)) % cap;

            // Write with wrap-around: up to two contiguous segments
            const int seg1 = std::min(n, cap - wPos);
            const int seg2 = n - seg1;

            for (int ch = 0; ch < rb.getNumChannels(); ++ch)
            {
                float* dst = rb.getWritePointer(ch);
                if (ch < totalIn)
                {
                    const float* src = buffer.getReadPointer(ch);
                    juce::FloatVectorOperations::copy(dst + wPos, src, seg1);
                    if (seg2 > 0)
                        juce::FloatVectorOperations::copy(dst, src + seg1, seg2);
                }
                else
                {
                    // Fill extra ring channels with silence (e.g. mono input → stereo ring)
                    juce::FloatVectorOperations::fill(dst + wPos, 0.0f, seg1);
                    if (seg2 > 0)
                        juce::FloatVectorOperations::fill(dst, 0.0f, seg2);
                }
            }
            wPos = (wPos + n) % cap;
        }
        channelReady[channel].store(true);

        // Transmitter is pass-through: original buffer continues downstream.
        // The limiter below will still protect the forward chain.
    }

    // ── RECEIVE ──────────────────────────────────────────────────────────────
    else
    {
        // ── Dry-signal capture ────────────────────────────────────────────
        // Save the incoming buffer (e.g. the synth signal from FL) before the
        // ring-buffer receive overwrites it.  We only bother if dry > 0 so
        // there is no overhead at the default setting of 0.
        if (dryGain > 0.0f)
        {
            const int dryChannels = dryBuffer.getNumChannels();
            for (int ch = 0; ch < std::min(totalIn, dryChannels); ++ch)
                juce::FloatVectorOperations::copy(
                    dryBuffer.getWritePointer(ch),
                    buffer.getReadPointer(ch),
                    numSamples);
            // Zero any surplus dryBuffer channels (e.g. mono input → stereo output)
            for (int ch = totalIn; ch < std::min(totalOut, dryChannels); ++ch)
                juce::FloatVectorOperations::fill(dryBuffer.getWritePointer(ch), 0.0f, numSamples);
        }

        // ── Latency reporting ──────────────────────────────────────────────
        // We report 1 block of latency when a live transmitter is actively
        // pushing signal on this channel, and 0 otherwise.  We only call
        // setLatencySamples() when the value changes so the host isn't
        // hammered with display-refresh callbacks every block.
        //
        // Why 1 block?  In block-based DAW processing, the receiver always
        // reads data written by the transmitter in either the current block
        // (if transmitter ran first) or the previous block (if receiver ran
        // first).  Processing order across unconnected tracks is not
        // guaranteed, so the conservative — and honest — value is 1 block.
        constexpr float kSignalFloor = 1.0e-4f; // ≈ -80 dBFS

        const bool activeSignal = channelHasTransmitter[channel].load()
            && channelReady[channel].load()
            && channelPeakLevel[channel].load() > kSignalFloor;

        const int latencyToReport = activeSignal ? currentBlockSize : 0;

        if (latencyToReport != lastReportedLatency)
        {
            setLatencySamples(latencyToReport);
            lastReportedLatency = latencyToReport;
        }

        // ── Drain ring buffer into output ──────────────────────────────────
        // We read exactly numSamples from the ring buffer.  If fewer samples
        // are available (e.g. the transmitter hasn't run yet this block, or it
        // wrote a smaller block), the remainder is zero-padded with silence.
        // This gracefully handles any transmitter/receiver block size mismatch,
        // including FL Studio's variable buffer sizes when "use fixed size
        // buffer" is disabled.
        if (!channelReady[channel].load())
        {
            buffer.clear();
        }
        else
        {
            juce::SpinLock::ScopedLockType lock(ringLocks[channel]);
            auto& rb = ringBuffers[channel];
            const int cap = kRingCapacity;
            int& rPos = ringReadPos[channel];
            const int wPos = ringWritePos[channel];

            const int available = (wPos - rPos + cap) % cap;
            const int toRead = std::min(numSamples, available);
            const int toPad = numSamples - toRead; // samples to fill with silence

            // Read with wrap-around: up to two contiguous segments
            const int seg1 = (toRead > 0) ? std::min(toRead, cap - rPos) : 0;
            const int seg2 = toRead - seg1;

            for (int ch = 0; ch < totalOut; ++ch)
            {
                float* dst = buffer.getWritePointer(ch);
                if (ch < rb.getNumChannels())
                {
                    const float* src = rb.getReadPointer(ch);
                    if (seg1 > 0) juce::FloatVectorOperations::copy(dst, src + rPos, seg1);
                    if (seg2 > 0) juce::FloatVectorOperations::copy(dst + seg1, src, seg2);
                }
                else
                {
                    // Output channel has no matching ring channel: silence
                    if (toRead > 0) juce::FloatVectorOperations::fill(dst, 0.0f, toRead);
                }
                // Pad any remaining samples if the ring had fewer than requested
                if (toPad > 0)
                    juce::FloatVectorOperations::fill(dst + toRead, 0.0f, toPad);
            }
            rPos = (rPos + toRead) % cap;
        }

        // ── Wet level ─────────────────────────────────────────────────────
        // Scale the ring-buffer signal before the dry signal is added.
        // At the default of 1.0 this is a no-op; turning it down attenuates
        // the feedback return without touching the dry blend.
        if (wetGain != 1.0f)
            buffer.applyGain(wetGain);

        // ── Dry mix ───────────────────────────────────────────────────────
        // Add the captured pre-receive signal (e.g. the synth) back into the
        // output at the requested gain.  This lets the synth "kick-start" or
        // sustain a feedback loop even when the ring buffer is empty or silent.
        if (dryGain > 0.0f)
        {
            const int dryChannels = dryBuffer.getNumChannels();
            for (int ch = 0; ch < totalOut; ++ch)
            {
                const int srcCh = std::min(ch, dryChannels - 1);
                juce::FloatVectorOperations::addWithMultiply(
                    buffer.getWritePointer(ch),
                    dryBuffer.getReadPointer(srcCh),
                    dryGain,
                    numSamples);
            }
        }
    }

    // ── LIMITER (both modes) ──────────────────────────────────────────────────
    // Applied to the output buffer in both modes:
    //   • In transmit mode: protects the forward signal chain downstream.
    //   • In receive mode:  protects against feedback runaway on injection.
    //
    // Uses a hard-knee attack (immediate clamp) and a smooth exponential
    // release toward unity gain over ~100 ms to prevent pumping artefacts.
    if (limiterOn)
    {
        for (int ch = 0; ch < totalOut; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s)
            {
                const float absVal = std::abs(data[s]);

                if (absVal > threshold)
                    limiterGain = threshold / absVal;               // instant attack
                else
                    limiterGain = std::min(1.0f, limiterGain + releaseRate); // smooth release

                data[s] *= limiterGain;
            }
        }
    }

    // Clear any surplus output channels
    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear(i, 0, numSamples);
}

// ── Editor ────────────────────────────────────────────────────────────────────
juce::AudioProcessorEditor* AudioRerouterPluginAudioProcessor::createEditor()
{
    return new AudioRerouterPluginAudioProcessorEditor(*this);
}

bool AudioRerouterPluginAudioProcessor::hasEditor() const { return true; }

// ── Identity / MIDI boilerplate ───────────────────────────────────────────────
const juce::String AudioRerouterPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioRerouterPluginAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool AudioRerouterPluginAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool AudioRerouterPluginAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double AudioRerouterPluginAudioProcessor::getTailLengthSeconds() const { return 0.0; }

// ── Program boilerplate ───────────────────────────────────────────────────────
int AudioRerouterPluginAudioProcessor::getNumPrograms() { return 1; }
int AudioRerouterPluginAudioProcessor::getCurrentProgram() { return 0; }
void AudioRerouterPluginAudioProcessor::setCurrentProgram(int) {}
const juce::String AudioRerouterPluginAudioProcessor::getProgramName(int) { return {}; }
void AudioRerouterPluginAudioProcessor::changeProgramName(int, const juce::String&) {}

// ── State persistence ─────────────────────────────────────────────────────────
void AudioRerouterPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AudioRerouterPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Fixed signature: const void* (was const char*, which silently failed to override)
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

// ── Plugin entry point ────────────────────────────────────────────────────────
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioRerouterPluginAudioProcessor();
}