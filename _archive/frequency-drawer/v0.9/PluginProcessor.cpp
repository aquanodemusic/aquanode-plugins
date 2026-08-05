/*
  ==============================================================================
    FrequencyDrawer — PluginProcessor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
FrequencyDrawerAudioProcessor::FrequencyDrawerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input",  juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{}

FrequencyDrawerAudioProcessor::~FrequencyDrawerAudioProcessor() {}

//==============================================================================
const juce::String FrequencyDrawerAudioProcessor::getName() const { return JucePlugin_Name; }
bool   FrequencyDrawerAudioProcessor::acceptsMidi()  const { return false; }
bool   FrequencyDrawerAudioProcessor::producesMidi() const { return false; }
bool   FrequencyDrawerAudioProcessor::isMidiEffect() const { return false; }
double FrequencyDrawerAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    FrequencyDrawerAudioProcessor::getNumPrograms() { return 1; }
int    FrequencyDrawerAudioProcessor::getCurrentProgram() { return 0; }
void   FrequencyDrawerAudioProcessor::setCurrentProgram(int) {}
const juce::String FrequencyDrawerAudioProcessor::getProgramName(int) { return {}; }
void   FrequencyDrawerAudioProcessor::changeProgramName(int, const juce::String&) {}
void   FrequencyDrawerAudioProcessor::getStateInformation(juce::MemoryBlock&) {}
void   FrequencyDrawerAudioProcessor::setStateInformation(const void*, int) {}

//==============================================================================
void FrequencyDrawerAudioProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    // Re-render at the new sample rate so the buffer always matches.
    triggerBackgroundRender();
}

void FrequencyDrawerAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FrequencyDrawerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif
    return true;
}
#endif

//==============================================================================
// processBlock — purely reads from the pre-rendered buffer; no real-time synthesis
//==============================================================================

void FrequencyDrawerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // ---- Non-blocking buffer swap: pick up result from background renderer ----
    if (newBufferReady_.load())
    {
        juce::ScopedLock lk(bufferSwapCS_);
        if (newBufferReady_.exchange(false))
            activeBuffer_ = std::move(pendingBuffer_);
    }

    if (!playing_.load()) return;

    const int    numSamples = buffer.getNumSamples();
    const double sr         = sampleRate_;

    // ---- Apply any pending seek ----
    if (seekPending_.exchange(false))
    {
        const double pos    = seekTarget_.load();
        playheadSamples_    = static_cast<int64_t>(pos * sr);
        playheadSeconds_.store(pos);
    }

    // ---- Copy from pre-rendered buffer ----
    if (activeBuffer_ && activeBuffer_->getNumSamples() > 0)
    {
        const int total     = activeBuffer_->getNumSamples();
        const int startSmp  = static_cast<int>(juce::jmin(
                                  static_cast<int64_t>(total), playheadSamples_));
        const int toCopy    = juce::jmin(numSamples, total - startSmp);

        if (toCopy > 0)
        {
            const int numOut = getTotalNumOutputChannels();
            for (int ch = 0; ch < juce::jmin(numOut, 2); ++ch)
            {
                const int srcCh = juce::jmin(ch, activeBuffer_->getNumChannels() - 1);
                buffer.copyFrom(ch, 0, *activeBuffer_, srcCh, startSmp, toCopy);
            }
        }
    }

    // ---- Advance playhead ----
    playheadSamples_ += numSamples;
    const double newPos = static_cast<double>(playheadSamples_) / sr;
    playheadSeconds_.store(newPos);

    if (newPos >= kDuration)
    {
        playing_.store(false);
        playheadSamples_ = static_cast<int64_t>(kDuration * sr);
        playheadSeconds_.store(kDuration);
    }
}

//==============================================================================
bool FrequencyDrawerAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* FrequencyDrawerAudioProcessor::createEditor()
{
    return new FrequencyDrawerAudioProcessorEditor(*this);
}

//==============================================================================
// Editor-facing API
//==============================================================================

void FrequencyDrawerAudioProcessor::addEvent(double time, double freq, double amplitude)
{
    if (time < 0.0 || time > kDuration) return;
    freq = juce::jlimit(kFreqMin, kFreqMax, freq);

    juce::ScopedLock lk(eventsCS_);
    DrawnEvent evt{ time, freq, amplitude };
    auto it = std::lower_bound(events_.begin(), events_.end(), evt,
        [](const DrawnEvent& a, const DrawnEvent& b){ return a.time < b.time; });
    events_.insert(it, evt);
}

void FrequencyDrawerAudioProcessor::clearAllEvents()
{
    {
        juce::ScopedLock lk(eventsCS_);
        events_.clear();
    }

    // Stop playback and reset position
    playing_.store(false);
    playheadSamples_ = 0;
    playheadSeconds_.store(0.0);

    // Invalidate any in-flight render by bumping the generation counter,
    // then immediately silence the active buffer.
    renderGeneration_.fetch_add(1);
    rerenderPending_.store(false);

    {
        juce::ScopedLock lk(bufferSwapCS_);
        activeBuffer_.reset();
        pendingBuffer_.reset();
    }
    newBufferReady_.store(false);
    isRendering_.store(false);
}

std::vector<DrawnEvent> FrequencyDrawerAudioProcessor::getEventsCopy() const
{
    juce::ScopedLock lk(eventsCS_);
    return events_;
}

void FrequencyDrawerAudioProcessor::requestSeek(double seconds)
{
    const double clamped = juce::jlimit(0.0, kDuration, seconds);
    seekTarget_.store(clamped);
    seekPending_.store(true);
    playheadSeconds_.store(clamped);  // optimistic UI update
}

void FrequencyDrawerAudioProcessor::setPlaying(bool shouldPlay)
{
    if (shouldPlay && playheadSeconds_.load() >= kDuration)
        requestSeek(0.0);
    playing_.store(shouldPlay);
}

//==============================================================================
// Background render
//==============================================================================

void FrequencyDrawerAudioProcessor::triggerBackgroundRender()
{
    if (sampleRate_ < 1.0) return;

    // Signal that a render is wanted.
    rerenderPending_.store(true);

    // If one is already running, it will loop back for us.
    if (isRendering_.exchange(true)) return;

    const double sr  = sampleRate_;
    const int    gen = renderGeneration_.load();

    juce::Thread::launch([this, sr, gen]
    {
        // Keep looping as long as new requests arrive mid-render.
        while (rerenderPending_.exchange(false))
        {
            try
            {
                auto buf = std::make_unique<juce::AudioBuffer<float>>(renderOffline(sr));

                // Only store result if we are still on the same "generation"
                // (i.e. clearAllEvents() hasn't been called since we started).
                if (renderGeneration_.load() == gen)
                {
                    juce::ScopedLock lk(bufferSwapCS_);
                    pendingBuffer_ = std::move(buf);
                    newBufferReady_.store(true);
                }
            }
            catch (...) {}
        }

        isRendering_.store(false);

        // Edge-case: a new request arrived in the tiny window between the
        // while-condition check and store(false).  Re-enter if needed.
        if (rerenderPending_.load())
            triggerBackgroundRender();
    });
}

//==============================================================================
// Offline renderer
//
//  Each DrawnEvent  →  one decaying sinusoid at evt.frequency with evt.amplitude.
//  Harmonic content is already baked into the event list by DrawingCanvas.
//==============================================================================

juce::AudioBuffer<float> FrequencyDrawerAudioProcessor::renderOffline(double targetSR)
{
    const int totalSamples = static_cast<int>(kDuration * targetSR);
    juce::AudioBuffer<float> buf(2, totalSamples);
    buf.clear();

    const std::vector<DrawnEvent> evts = getEventsCopy();
    if (evts.empty()) return buf;  // fast path — nothing to render

    // Decay constant: same logic as before but self-contained
    const bool  blur      = blurEnabled_.load();
    const float bt        = blurStrength_.load();
    const double decaySec = (blur && bt > 0.05f) ? static_cast<double>(bt) : 0.04;
    const double dm       = std::exp(-5.0 / (juce::jmax(0.01, decaySec) * targetSR));

    // Local oscillator pool — large enough for dense drawings
    std::vector<SynthOscillator> oscs(kMaxOsc);

    auto getFree = [&]() -> int
    {
        for (int i = 0; i < kMaxOsc; ++i)
            if (!oscs[i].active) return i;
        // Pool full: steal the quietest oscillator
        int q = 0;  double minA = oscs[0].getAmplitude();
        for (int i = 1; i < kMaxOsc; ++i)
            if (oscs[i].getAmplitude() < minA) { minA = oscs[i].getAmplitude(); q = i; }
        return q;
    };

    float* L = buf.getWritePointer(0);
    float* R = buf.getWritePointer(1);

    int evtIdx = 0;
    for (int s = 0; s < totalSamples; ++s)
    {
        const double t = static_cast<double>(s) / targetSR;

        // Fire events whose time has arrived
        while (evtIdx < (int)evts.size() && evts[evtIdx].time <= t)
        {
            oscs[getFree()].trigger(evts[evtIdx].frequency,
                                    evts[evtIdx].amplitude,
                                    dm);
            ++evtIdx;
        }

        float sample = 0.0f;
        for (auto& o : oscs)
            if (o.active) sample += o.processSample(targetSR);

        sample = std::tanh(sample * 0.6f) * 0.9f;
        L[s] = sample;
        R[s] = sample;
    }
    return buf;
}

//==============================================================================
// FLAC export (requires JUCE_USE_FLAC=1 in Projucer / CMake)
//==============================================================================

bool FrequencyDrawerAudioProcessor::exportToFlac(const juce::File& file)
{
    constexpr double targetSR = 44100.0;

    const auto buf = renderOffline(targetSR);

    juce::FlacAudioFormat fmt;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    if (!stream) return false;

    // qualityOptionIndex 8 = maximum FLAC compression (slowest encoder, smallest file)
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        fmt.createWriterFor(stream.get(), targetSR,
                            /*numChannels=*/2,
                            /*bitsPerSample=*/24,
                            /*metadata=*/{},
                            /*qualityOptionIndex=*/8));
    if (!writer) return false;

    stream.release();   // writer now owns the stream
    return writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrequencyDrawerAudioProcessor();
}