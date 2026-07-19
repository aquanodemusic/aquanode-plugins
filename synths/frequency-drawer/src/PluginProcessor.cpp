/*
  ==============================================================================
    FrequencyDrawer — PluginProcessor.cpp   (unified Engine 1 + Engine 2)
  ==============================================================================

  ENGINE MODES
    0 = Engine 1  incremental WaypointOscillator render; supports DrawnPaths
    1 = Engine 2  full offline re-render on every stroke; global decay; no paths
                  Uses a 512-oscillator pool (original behaviour).
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

// Pool size used by Engine 2's renderOffline.  The original Engine 2 used 512;
// Engine 1 uses kMaxOsc (2048) from the header.
static constexpr int kE2OscPool = 512;

//==============================================================================
FrequencyDrawerAudioProcessor::FrequencyDrawerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
}

FrequencyDrawerAudioProcessor::~FrequencyDrawerAudioProcessor() {}

//==============================================================================
const juce::String FrequencyDrawerAudioProcessor::getName()          const { return JucePlugin_Name; }
bool   FrequencyDrawerAudioProcessor::acceptsMidi()                  const { return false; }
bool   FrequencyDrawerAudioProcessor::producesMidi()                 const { return false; }
bool   FrequencyDrawerAudioProcessor::isMidiEffect()                 const { return false; }
double FrequencyDrawerAudioProcessor::getTailLengthSeconds()         const { return 0.0; }
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

    // Non-blocking buffer swap: pick up result from background renderer
    if (newBufferReady_.load())
    {
        juce::ScopedLock lk(bufferSwapCS_);
        if (newBufferReady_.exchange(false))
            activeBuffer_ = std::move(pendingBuffer_);
    }

    if (!playing_.load()) return;

    const int    numSamples = buffer.getNumSamples();
    const double sr = sampleRate_;

    if (seekPending_.exchange(false))
    {
        const double pos = seekTarget_.load();
        playheadSamples_ = static_cast<int64_t>(pos * sr);
        playheadSeconds_.store(pos);
    }

    if (activeBuffer_ && activeBuffer_->getNumSamples() > 0)
    {
        const int total = activeBuffer_->getNumSamples();
        const int startSmp = static_cast<int>(
            juce::jmin(static_cast<int64_t>(total), playheadSamples_));
        const int toCopy = juce::jmin(numSamples, total - startSmp);

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
// Engine selection
//==============================================================================
void FrequencyDrawerAudioProcessor::setEngineMode(int mode)
{
    const int clamped = juce::jlimit(0, 1, mode);
    engineMode_.store(clamped);
    // Switching engines invalidates the current buffer because the two engines
    // produce different waveforms even for the same event list.
    triggerFullRerender();
}

//==============================================================================
// Editor-facing API
//==============================================================================

void FrequencyDrawerAudioProcessor::addEvent(double time, double freq,
    double amplitude,
    bool   blurEnabled,
    float  blurSecs)
{
    if (time < 0.0 || time > kDuration) return;
    freq = juce::jlimit(kFreqMin, kFreqMax, freq);

    DrawnEvent evt{ time, freq, amplitude, blurEnabled, blurSecs };

    // Add to pending queue (consumed by incremental Engine 1 render)
    {
        juce::ScopedLock lk(eventsCS_);
        auto it = std::lower_bound(events_.begin(), events_.end(), evt,
            [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
        events_.insert(it, evt);
    }

    // Add to permanent history (used for full re-renders)
    {
        juce::ScopedLock lk(allEventsCS_);
        auto it = std::lower_bound(allEvents_.begin(), allEvents_.end(), evt,
            [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
        allEvents_.insert(it, evt);
    }
}

void FrequencyDrawerAudioProcessor::addEvents(std::vector<DrawnEvent> newEvents)
{
    if (newEvents.empty()) return;

    // Sort the incoming batch
    std::sort(newEvents.begin(), newEvents.end(),
        [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });

    // Clamp and validate
    newEvents.erase(std::remove_if(newEvents.begin(), newEvents.end(),
        [](const DrawnEvent& e) {
            return e.time < 0.0 || e.time > FrequencyDrawerAudioProcessor::kDuration;
        }), newEvents.end());

    // Add to pending queue (Engine 1 incremental)
    {
        juce::ScopedLock lk(eventsCS_);
        for (auto& evt : newEvents)
        {
            auto it = std::lower_bound(events_.begin(), events_.end(), evt,
                [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
            events_.insert(it, evt);
        }
    }

    // Add to permanent history
    {
        juce::ScopedLock lk(allEventsCS_);
        for (auto& evt : newEvents)
        {
            auto it = std::lower_bound(allEvents_.begin(), allEvents_.end(), evt,
                [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
            allEvents_.insert(it, evt);
        }
    }
}

void FrequencyDrawerAudioProcessor::addPath(DrawnPath path)
{
    {
        juce::ScopedLock lk(pathsCS_);
        paths_.push_back(path);
    }
    {
        juce::ScopedLock lk(allPathsCS_);
        allPaths_.push_back(path);
    }
}

void FrequencyDrawerAudioProcessor::clearAllEvents()
{
    // Pending queues
    { juce::ScopedLock lk(eventsCS_);    events_.clear(); }
    { juce::ScopedLock lk(pathsCS_);     paths_.clear(); }

    // Permanent history
    { juce::ScopedLock lk(allEventsCS_); allEvents_.clear(); }
    { juce::ScopedLock lk(allPathsCS_);  allPaths_.clear(); }

    // Committed audio buffer
    {
        juce::ScopedLock lk(committedBufferCS_);
        committedBuffer_.reset();
    }

    // Stop playback and reset position
    playing_.store(false);
    playheadSamples_ = 0;
    playheadSeconds_.store(0.0);

    // Invalidate any in-flight render
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

std::vector<DrawnPath> FrequencyDrawerAudioProcessor::getPathsCopy() const
{
    juce::ScopedLock lk(pathsCS_);
    return paths_;
}

void FrequencyDrawerAudioProcessor::requestSeek(double seconds)
{
    const double clamped = juce::jlimit(0.0, kDuration, seconds);
    seekTarget_.store(clamped);
    seekPending_.store(true);
    playheadSeconds_.store(clamped);
}

void FrequencyDrawerAudioProcessor::setPlaying(bool shouldPlay)
{
    if (shouldPlay && playheadSeconds_.load() >= kDuration)
        requestSeek(0.0);
    playing_.store(shouldPlay);
}

//==============================================================================
// Internal helpers
//==============================================================================

std::vector<DrawnEvent> FrequencyDrawerAudioProcessor::extractEvents()
{
    juce::ScopedLock lk(eventsCS_);
    return std::move(events_);
}

std::vector<DrawnPath> FrequencyDrawerAudioProcessor::extractPaths()
{
    juce::ScopedLock lk(pathsCS_);
    return std::move(paths_);
}

//==============================================================================
// triggerFullRerender  — re-render from allEvents_ / allPaths_, replacing the
//                        committedBuffer_ entirely.  Used when a global
//                        parameter (blur strength, engine switch) changes.
//==============================================================================
void FrequencyDrawerAudioProcessor::triggerFullRerender()
{
    if (sampleRate_ < 1.0) return;

    // Copy complete history so the background thread is self-contained
    std::vector<DrawnEvent> allEvtsCopy;
    {
        juce::ScopedLock lk(allEventsCS_);
        allEvtsCopy = allEvents_;
    }
    std::vector<DrawnPath> allPathsCopy;
    {
        juce::ScopedLock lk(allPathsCS_);
        allPathsCopy = allPaths_;
    }

    // Also drain the pending queues into the copies so nothing is lost
    {
        juce::ScopedLock lk(eventsCS_);
        for (auto& e : events_)
        {
            auto it = std::lower_bound(allEvtsCopy.begin(), allEvtsCopy.end(), e,
                [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
            allEvtsCopy.insert(it, e);
        }
        events_.clear();
    }
    {
        juce::ScopedLock lk(pathsCS_);
        for (auto& p : paths_)
            allPathsCopy.push_back(p);
        paths_.clear();
    }

    rerenderPending_.store(true);
    if (isRendering_.exchange(true)) return;

    const double sr = sampleRate_;
    const int    gen = renderGeneration_.load();
    const int    eng = engineMode_.load();

    juce::Thread::launch([this, sr, gen, eng,
        evts = std::move(allEvtsCopy),
        paths = std::move(allPathsCopy)]() mutable
        {
            while (rerenderPending_.exchange(false))
            {
                try
                {
                    std::unique_ptr<juce::AudioBuffer<float>> buf;

                    if (eng == 1)
                        buf = std::make_unique<juce::AudioBuffer<float>>(
                            renderOffline(evts, sr));
                    else
                        buf = std::make_unique<juce::AudioBuffer<float>>(
                            renderNewContent(evts, paths, sr));

                    if (renderGeneration_.load() == gen)
                    {
                        // Also store in committedBuffer_ so subsequent incremental
                        // renders (Engine 1) can mix on top of this.
                        {
                            juce::ScopedLock lk(committedBufferCS_);
                            committedBuffer_ = std::make_unique<juce::AudioBuffer<float>>(*buf);
                        }

                        juce::ScopedLock lk(bufferSwapCS_);
                        pendingBuffer_ = std::move(buf);
                        newBufferReady_.store(true);
                    }
                }
                catch (...) {}
            }

            isRendering_.store(false);

            if (rerenderPending_.load())
                triggerFullRerender();
        });
}

//==============================================================================
// triggerBackgroundRender
//
//  Engine 2  →  full re-render from allEvents_ every time (original behaviour)
//  Engine 1  →  incremental: render only new events/paths and mix into
//               committedBuffer_
//==============================================================================
void FrequencyDrawerAudioProcessor::triggerBackgroundRender()
{
    if (sampleRate_ < 1.0) return;

    if (engineMode_.load() == 1)
    {
        // Engine 2: always full re-render from permanent history
        triggerFullRerender();
        return;
    }

    // ---- Engine 1: incremental ----
    rerenderPending_.store(true);
    if (isRendering_.exchange(true)) return;

    const double sr = sampleRate_;
    const int    gen = renderGeneration_.load();

    juce::Thread::launch([this, sr, gen]
        {
            while (rerenderPending_.exchange(false))
            {
                try
                {
                    auto newEvts = extractEvents();
                    auto newPaths = extractPaths();

                    if (newEvts.empty() && newPaths.empty())
                        continue;

                    auto newAudio = renderNewContent(newEvts, newPaths, sr);

                    if (renderGeneration_.load() != gen) break;

                    // Mix new audio into committedBuffer_
                    {
                        juce::ScopedLock lk(committedBufferCS_);

                        const int totalSamples = static_cast<int>(kDuration * sr);

                        if (!committedBuffer_ ||
                            committedBuffer_->getNumSamples() != totalSamples)
                        {
                            committedBuffer_ =
                                std::make_unique<juce::AudioBuffer<float>>(2, totalSamples);
                            committedBuffer_->clear();
                        }

                        const int toCopy = juce::jmin(newAudio.getNumSamples(), totalSamples);
                        for (int ch = 0; ch < 2; ++ch)
                            committedBuffer_->addFrom(ch, 0, newAudio, ch, 0, toCopy);
                    }

                    // Apply tanh to a copy for playback
                    if (renderGeneration_.load() == gen)
                    {
                        juce::ScopedLock lkCB(committedBufferCS_);
                        auto playBuf = std::make_unique<juce::AudioBuffer<float>>(
                            *committedBuffer_);

                        const int n = playBuf->getNumSamples();
                        for (int ch = 0; ch < playBuf->getNumChannels(); ++ch)
                        {
                            float* p = playBuf->getWritePointer(ch);
                            for (int s = 0; s < n; ++s)
                                p[s] = std::tanh(p[s] * 0.6f) * 0.9f;
                        }

                        if (renderGeneration_.load() == gen)
                        {
                            juce::ScopedLock lk(bufferSwapCS_);
                            pendingBuffer_ = std::move(playBuf);
                            newBufferReady_.store(true);
                        }
                    }
                }
                catch (...) {}
            }

            isRendering_.store(false);

            if (rerenderPending_.load())
                triggerBackgroundRender();
        });
}

//==============================================================================
// Engine 2 — renderOffline
//
//  Full re-render of all supplied events.  Returns a tanh-clipped stereo buffer
//  ready for direct playback.  Uses a global blur/decay (original behaviour).
//  Pool size: kE2OscPool (512) — matches the original Engine 2.
//==============================================================================
juce::AudioBuffer<float>
FrequencyDrawerAudioProcessor::renderOffline(const std::vector<DrawnEvent>& evts,
    double targetSR)
{
    const int totalSamples = static_cast<int>(kDuration * targetSR);
    juce::AudioBuffer<float> buf(2, totalSamples);
    buf.clear();

    if (evts.empty()) return buf;

    const bool   blur = blurEnabled_.load();
    const float  bt = blurStrength_.load();
    const double decaySec = (blur && bt > 0.05f) ? static_cast<double>(bt) : 0.04;
    const double dm = std::exp(-5.0 / (juce::jmax(0.01, decaySec) * targetSR));

    std::vector<SynthOscillator> oscs(kE2OscPool);

    auto getFree = [&]() -> int
        {
            for (int i = 0; i < kE2OscPool; ++i)
                if (!oscs[i].active) return i;
            // Pool full: steal the quietest oscillator
            int q = 0;  double minA = oscs[0].getAmplitude();
            for (int i = 1; i < kE2OscPool; ++i)
                if (oscs[i].getAmplitude() < minA) { minA = oscs[i].getAmplitude(); q = i; }
            return q;
        };

    float* L = buf.getWritePointer(0);
    float* R = buf.getWritePointer(1);

    int evtIdx = 0;
    for (int s = 0; s < totalSamples; ++s)
    {
        const double t = static_cast<double>(s) / targetSR;

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
// Engine 1 — renderNewContent
//
//  Synthesises only the supplied (new) events/paths into a fresh 30-second
//  buffer.  Returns a RAW (pre-tanh) signal so the caller can mix it into
//  committedBuffer_ before applying tanh for playback.
//==============================================================================
juce::AudioBuffer<float>
FrequencyDrawerAudioProcessor::renderNewContent(const std::vector<DrawnEvent>& evts,
    const std::vector<DrawnPath>& paths,
    double targetSR)
{
    const int totalSamples = static_cast<int>(kDuration * targetSR);
    juce::AudioBuffer<float> buf(2, totalSamples);
    buf.clear();

    if (evts.empty() && paths.empty()) return buf;

    // Use the same global decay multiplier as Engine 2 for consistency,
    // but honour per-event blur if set.
    const bool   globalBlur = blurEnabled_.load();
    const float  bt = blurStrength_.load();
    const double defDecaySec = (globalBlur && bt > 0.05f)
        ? static_cast<double>(bt) : 0.04;
    const double defDm = std::exp(-5.0 / (juce::jmax(0.01, defDecaySec) * targetSR));

    std::vector<SynthOscillator> oscs(kMaxOsc);
    int oscWriteHead = 0; // round-robin for Engine 1 (avoids the quiet-steal search)

    auto getOsc = [&]() -> int
        {
            // First pass: find a genuinely free slot
            for (int i = 0; i < kMaxOsc; ++i)
                if (!oscs[i].active) return i;
            // Steal: quietest
            int q = 0; double minA = oscs[0].getAmplitude();
            for (int i = 1; i < kMaxOsc; ++i)
                if (oscs[i].getAmplitude() < minA) { minA = oscs[i].getAmplitude(); q = i; }
            return q;
        };

    // ---- Stamp events ----
    // Sort by time for the sample-accurate trigger loop
    auto sortedEvts = evts; // already sorted but copy for safety
    std::sort(sortedEvts.begin(), sortedEvts.end(),
        [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });

    // ---- Interpolation paths ----
    // Pre-expand each path into time-sorted events so the main loop handles
    // everything uniformly.
    std::vector<DrawnEvent> pathEvts;
    for (const auto& path : paths)
    {
        if (path.waypoints.size() < 2) continue;

        const double stepSec = 0.005; // 5 ms interpolation resolution
        const int    nHarm = juce::jlimit(1, 64, path.numHarmonics);
        const double baseAmp = path.amplitude / static_cast<double>(nHarm);

        for (size_t wi = 0; wi + 1 < path.waypoints.size(); ++wi)
        {
            const auto& [t0, f0] = path.waypoints[wi];
            const auto& [t1, f1] = path.waypoints[wi + 1];
            const double span = t1 - t0;
            if (span <= 0.0) continue;

            const int steps = juce::jmax(1, static_cast<int>(span / stepSec));
            for (int si = 0; si < steps; ++si)
            {
                const double alpha = static_cast<double>(si) / steps;
                const double t = t0 + alpha * span;
                const double f = f0 + alpha * (f1 - f0); // linear in log space is fine

                for (int h = 1; h <= nHarm; ++h)
                {
                    const double hf = juce::jlimit(kFreqMin, kFreqMax, f * h);
                    pathEvts.push_back({ t, hf, baseAmp / h,
                                         path.blurEnabled, path.blurSecs });
                }
            }
        }
    }

    // Merge stamp events and path events
    auto allEvts = sortedEvts;
    for (auto& e : pathEvts) allEvts.push_back(e);
    std::sort(allEvts.begin(), allEvts.end(),
        [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });

    float* L = buf.getWritePointer(0);
    float* R = buf.getWritePointer(1);

    int evtIdx = 0;
    for (int s = 0; s < totalSamples; ++s)
    {
        const double t = static_cast<double>(s) / targetSR;

        while (evtIdx < (int)allEvts.size() && allEvts[evtIdx].time <= t)
        {
            const auto& e = allEvts[evtIdx];

            // Per-event decay: use event's blur setting if present, else global
            double dm = defDm;
            if (e.blurEnabled && e.blurSecs > 0.05f)
                dm = std::exp(-5.0 / (static_cast<double>(e.blurSecs) * targetSR));

            oscs[getOsc()].trigger(e.frequency, e.amplitude, dm);
            ++evtIdx;
        }

        float sample = 0.0f;
        for (auto& o : oscs)
            if (o.active) sample += o.processSample(targetSR);

        // Raw output (no tanh here — caller mixes multiple renderNewContent calls)
        L[s] = sample;
        R[s] = sample;
    }

    return buf;
}

//==============================================================================
// FLAC export
//==============================================================================
bool FrequencyDrawerAudioProcessor::exportToFlac(const juce::File& file)
{
    constexpr double targetSR = 44100.0;

    // Render from complete history regardless of engine
    std::vector<DrawnEvent> allEvtsCopy;
    {
        juce::ScopedLock lk(allEventsCS_);
        allEvtsCopy = allEvents_;
    }

    juce::AudioBuffer<float> buf;
    if (engineMode_.load() == 1)
    {
        // Engine 2 renders with tanh already applied
        buf = renderOffline(allEvtsCopy, targetSR);
    }
    else
    {
        // Engine 1: raw mix then soft-clip
        std::vector<DrawnPath> allPathsCopy;
        {
            juce::ScopedLock lk(allPathsCS_);
            allPathsCopy = allPaths_;
        }
        buf = renderNewContent(allEvtsCopy, allPathsCopy, targetSR);
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int s = 0; s < n; ++s)
                p[s] = std::tanh(p[s] * 0.6f) * 0.9f;
        }
    }

    juce::FlacAudioFormat fmt;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    if (!stream) return false;

    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        fmt.createWriterFor(stream.get(), targetSR,
            /*numChannels=*/2,
            /*bitsPerSample=*/24,
            /*metadata=*/{},
            /*qualityOptionIndex=*/8));
    if (!writer) return false;

    stream.release(); // writer now owns the stream
    return writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrequencyDrawerAudioProcessor();
}