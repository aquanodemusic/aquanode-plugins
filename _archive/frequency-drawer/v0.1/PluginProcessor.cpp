/*
  ==============================================================================
    FrequencyDrawer -- PluginProcessor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// WaypointOscillator
//
//  Phase-continuous frequency oscillator that follows an arbitrary sequence of
//  (time, frequency) waypoints using log-interpolation between each pair.
//  Used only inside renderNewContent.
//==============================================================================
namespace
{

    struct WaypointOscillator
    {
        bool   active = false;
        double runningAmp = 0.0;
        double baseAmp = 0.0;
        double tailDecay = 1.0;
        bool   hasTail = false;
        double phase = 0.0;
        int    wpIdx = 0;

        std::vector<std::pair<double, double>> wps; // (time, frequency)

        void trigger(std::vector<std::pair<double, double>> scaledWps,
            double amp, double tdm, bool tail) noexcept
        {
            wps = std::move(scaledWps);
            baseAmp = amp;
            runningAmp = amp;
            tailDecay = tdm;
            hasTail = tail;
            phase = 0.0;
            wpIdx = 0;
            active = !wps.empty();
        }

        float processSample(double t, double sampleRate) noexcept
        {
            if (!active) return 0.0f;

            const int n = static_cast<int>(wps.size());

            while (wpIdx + 2 < n && t >= wps[wpIdx + 1].first)
                ++wpIdx;

            double f;
            float  env;

            if (n >= 2 && t < wps[n - 1].first)
            {
                const int    i0 = juce::jmin(wpIdx, n - 2);
                const double t0 = wps[i0].first;
                const double t1 = wps[i0 + 1].first;
                const double dur = juce::jmax(1.0e-6, t1 - t0);
                const double alpha = juce::jlimit(0.0, 1.0, (t - t0) / dur);

                f = std::exp(std::log(juce::jmax(1.0, wps[i0].second)) * (1.0 - alpha)
                    + std::log(juce::jmax(1.0, wps[i0 + 1].second)) * alpha);

                env = static_cast<float>(baseAmp);

                constexpr double kFade = 0.005;
                const double age = t - wps[0].first;
                if (age >= 0.0 && age < kFade)
                    env *= static_cast<float>(age / kFade);
                if (!hasTail)
                {
                    const double rem = wps[n - 1].first - t;
                    if (rem >= 0.0 && rem < kFade)
                        env *= static_cast<float>(rem / kFade);
                }
            }
            else
            {
                if (!hasTail) { active = false; return 0.0f; }
                f = (n > 0) ? wps[n - 1].second : 440.0;
                runningAmp *= tailDecay;
                if (runningAmp < 1.0e-7) { active = false; return 0.0f; }
                env = static_cast<float>(runningAmp);
            }

            const float out = env * static_cast<float>(std::sin(phase));
            phase += juce::MathConstants<double>::twoPi * f / sampleRate;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
            return out;
        }

        double getAmplitude() const noexcept
        {
            return active ? (hasTail ? runningAmp : baseAmp) : 0.0;
        }
    };

} // anonymous namespace

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
const juce::String FrequencyDrawerAudioProcessor::getName()  const { return JucePlugin_Name; }
bool   FrequencyDrawerAudioProcessor::acceptsMidi()          const { return false; }
bool   FrequencyDrawerAudioProcessor::producesMidi()         const { return false; }
bool   FrequencyDrawerAudioProcessor::isMidiEffect()         const { return false; }
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
    const int totalSamples = static_cast<int>(kDuration * sampleRate);

    {
        juce::ScopedLock lk(committedBufferCS_);
        // Initialise (or resize) the committed buffer.
        // If the sample rate changed we start fresh -- existing audio would be
        // at the wrong pitch/duration anyway.
        if (!committedBuffer_ || committedBuffer_->getNumSamples() != totalSamples)
        {
            committedBuffer_ = std::make_unique<juce::AudioBuffer<float>>(2, totalSamples);
            committedBuffer_->clear();
        }
    }

    // Promote any committed audio so processBlock can play it immediately.
    {
        juce::ScopedLock ck(committedBufferCS_);
        juce::ScopedLock lk(bufferSwapCS_);
        if (committedBuffer_)
        {
            pendingBuffer_ = std::make_unique<juce::AudioBuffer<float>>(*committedBuffer_);
            newBufferReady_.store(true);
        }
    }

    // If events were added before prepareToPlay (unlikely but safe), render them.
    if (!events_.empty() || !paths_.empty())
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
// processBlock
//
//  Reads from the pre-committed buffer.  Applies tanh soft-clip per sample --
//  the committed buffer stores raw (linear) mixed audio so multiple incremental
//  additions compose correctly before limiting.
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

    // Apply any pending seek
    if (seekPending_.exchange(false))
    {
        const double pos = seekTarget_.load();
        playheadSamples_ = static_cast<int64_t>(pos * sr);
        playheadSeconds_.store(pos);
    }

    // Copy from pre-committed buffer, applying tanh soft-clip on the fly.
    // (Raw linear audio is stored in activeBuffer_; the clip must be applied
    //  at the last moment so incremental mixes add up correctly.)
    if (activeBuffer_ && activeBuffer_->getNumSamples() > 0)
    {
        const int total = activeBuffer_->getNumSamples();
        const int startSmp = static_cast<int>(juce::jmin(
            static_cast<int64_t>(total), playheadSamples_));
        const int toCopy = juce::jmin(numSamples, total - startSmp);

        if (toCopy > 0)
        {
            const int numOut = getTotalNumOutputChannels();
            for (int ch = 0; ch < juce::jmin(numOut, 2); ++ch)
            {
                const int    srcCh = juce::jmin(ch, activeBuffer_->getNumChannels() - 1);
                const float* src = activeBuffer_->getReadPointer(srcCh) + startSmp;
                float* dst = buffer.getWritePointer(ch);
                for (int s = 0; s < toCopy; ++s)
                    dst[s] = std::tanh(src[s] * 0.6f) * 0.9f;
            }
        }
    }

    // Advance playhead
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

void FrequencyDrawerAudioProcessor::addEvent(double time, double freq, double amplitude,
    bool blurEnabled, float blurSecs)
{
    juce::ScopedLock lk(eventsCS_);
    events_.push_back({ time, freq, amplitude, blurEnabled, blurSecs });
    std::sort(events_.begin(), events_.end(),
        [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
}

void FrequencyDrawerAudioProcessor::addEvents(std::vector<DrawnEvent> newEvents)
{
    if (newEvents.empty()) return;
    juce::ScopedLock lk(eventsCS_);
    events_.insert(events_.end(),
        std::make_move_iterator(newEvents.begin()),
        std::make_move_iterator(newEvents.end()));
    std::sort(events_.begin(), events_.end(),
        [](const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
}

void FrequencyDrawerAudioProcessor::addPath(DrawnPath path)
{
    if (path.waypoints.size() < 2) return;
    juce::ScopedLock lk(pathsCS_);
    paths_.push_back(std::move(path));
    std::sort(paths_.begin(), paths_.end(),
        [](const DrawnPath& a, const DrawnPath& b)
        { return a.waypoints.front().first < b.waypoints.front().first; });
}

void FrequencyDrawerAudioProcessor::clearAllEvents()
{
    {
        juce::ScopedLock lk(eventsCS_);
        events_.clear();
    }
    {
        juce::ScopedLock lk(pathsCS_);
        paths_.clear();
    }

    playing_.store(false);
    playheadSamples_ = 0;
    playheadSeconds_.store(0.0);

    // Bump generation so any in-flight render is discarded
    renderGeneration_.fetch_add(1);
    rerenderPending_.store(false);
    isRendering_.store(false);

    // Silence the committed buffer
    {
        juce::ScopedLock lk(committedBufferCS_);
        if (committedBuffer_)
            committedBuffer_->clear();
    }

    // Flush swap buffers
    {
        juce::ScopedLock lk(bufferSwapCS_);
        activeBuffer_.reset();
        pendingBuffer_.reset();
    }
    newBufferReady_.store(false);
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

std::vector<DrawnEvent> FrequencyDrawerAudioProcessor::extractEvents()
{
    juce::ScopedLock lk(eventsCS_);
    std::vector<DrawnEvent> result;
    std::swap(result, events_);
    return result;
}

std::vector<DrawnPath> FrequencyDrawerAudioProcessor::extractPaths()
{
    juce::ScopedLock lk(pathsCS_);
    std::vector<DrawnPath> result;
    std::swap(result, paths_);
    return result;
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
// Incremental background render
//
//  Key difference from the old full re-render:
//    1. Extract (atomically remove) only the pending events/paths.
//    2. Render just those into a temporary buffer (raw, no tanh).
//    3. Mix (add) the temporary buffer into committedBuffer_.
//    4. Copy committedBuffer_ into pendingBuffer_ so processBlock picks it up.
//
//  This means a single new dot causes only that dot's oscillator decay to be
//  computed -- not the entire canvas history.  Render time is proportional to
//  what was just drawn.
//==============================================================================
void FrequencyDrawerAudioProcessor::triggerBackgroundRender()
{
    if (sampleRate_ < 1.0) return;

    rerenderPending_.store(true);
    if (isRendering_.exchange(true)) return;   // another thread already running

    const double sr = sampleRate_;
    const int    gen = renderGeneration_.load();

    juce::Thread::launch([this, sr, gen]
        {
            while (rerenderPending_.exchange(false))
            {
                // Atomically extract pending content (clears the lists)
                auto evts = extractEvents();
                auto paths = extractPaths();

                if (evts.empty() && paths.empty())
                    continue;

                if (renderGeneration_.load() != gen)
                    break; // Clear() was called; discard

                try
                {
                    // Render only the new content (raw, linear signal)
                    auto tempBuf = renderNewContent(evts, paths, sr);

                    if (renderGeneration_.load() != gen)
                        break; // Clear() raced in during render; discard

                    // Mix into the committed master buffer
                    {
                        juce::ScopedLock lk(committedBufferCS_);
                        const int n = juce::jmin(committedBuffer_->getNumSamples(),
                            tempBuf.getNumSamples());
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            float* dst = committedBuffer_->getWritePointer(ch);
                            const float* src = tempBuf.getReadPointer(ch);
                            for (int s = 0; s < n; ++s)
                                dst[s] += src[s];
                        }
                    }

                    // Hand a copy to processBlock via the double-buffer swap
                    {
                        std::unique_ptr<juce::AudioBuffer<float>> copy;
                        {
                            juce::ScopedLock lk(committedBufferCS_);
                            copy = std::make_unique<juce::AudioBuffer<float>>(*committedBuffer_);
                        }
                        {
                            juce::ScopedLock lk(bufferSwapCS_);
                            pendingBuffer_ = std::move(copy);
                            newBufferReady_.store(true);
                        }
                    }
                }
                catch (...) {}
            }

            isRendering_.store(false);

            // Edge-case: a new stroke arrived while we were finishing
            if (rerenderPending_.load())
                triggerBackgroundRender();
        });
}

//==============================================================================
// renderNewContent
//
//  Synthesises only the supplied events/paths.
//  Returns RAW (pre-tanh) audio; the caller is responsible for mixing and
//  limiting.  This keeps incremental additions in the linear domain so they
//  compose correctly before soft-clipping at playback / export time.
//==============================================================================
juce::AudioBuffer<float> FrequencyDrawerAudioProcessor::renderNewContent(
    const std::vector<DrawnEvent>& evts,
    const std::vector<DrawnPath>& paths,
    double targetSR)
{
    const int totalSamples = static_cast<int>(kDuration * targetSR);
    juce::AudioBuffer<float> buf(2, totalSamples);
    buf.clear();

    if (evts.empty() && paths.empty()) return buf;

    // Build WaypointOscillator trigger specs (one per path × harmonic)
    struct PathTrigger
    {
        double startTime;
        std::vector<std::pair<double, double>> scaledWps;
        double amp;
        double tailDecayMult;
        bool   hasTail;
    };

    std::vector<PathTrigger> pathTriggers;
    pathTriggers.reserve(paths.size() * 8);

    for (const auto& path : paths)
    {
        if (path.waypoints.size() < 2) continue;

        double maxF = 0.0;
        for (const auto& wp : path.waypoints)
            maxF = std::max(maxF, wp.second);

        double hsum = 0.0;
        for (int h = 1; h <= path.numHarmonics; ++h)
        {
            if (maxF * h > kFreqMax) break;
            hsum += 1.0 / static_cast<double>(h);
        }
        if (hsum < 1.0e-10) continue;

        const bool   hasTail = path.blurEnabled && path.blurSecs > 0.001f;
        const double decaySec = hasTail ? static_cast<double>(path.blurSecs) : 0.04;
        const double tdm = hasTail
            ? std::exp(-5.0 / (juce::jmax(0.01, decaySec) * targetSR))
            : 1.0;

        for (int h = 1; h <= path.numHarmonics; ++h)
        {
            if (maxF * h > kFreqMax) break;
            const double amp = (path.amplitude / static_cast<double>(h)) / hsum;

            std::vector<std::pair<double, double>> scaledWps;
            scaledWps.reserve(path.waypoints.size());
            for (const auto& wp : path.waypoints)
                scaledWps.push_back({ wp.first, wp.second * static_cast<double>(h) });

            pathTriggers.push_back({
                path.waypoints.front().first,
                std::move(scaledWps),
                amp, tdm, hasTail
                });
        }
    }

    std::sort(pathTriggers.begin(), pathTriggers.end(),
        [](const PathTrigger& a, const PathTrigger& b)
        { return a.startTime < b.startTime; });

    // Oscillator pools
    std::vector<SynthOscillator> oscs(kMaxOsc);
    auto getFreeOsc = [&]() -> int
        {
            for (int i = 0; i < kMaxOsc; ++i)
                if (!oscs[i].active) return i;
            int q = 0; double minA = oscs[0].getAmplitude();
            for (int i = 1; i < kMaxOsc; ++i)
                if (oscs[i].getAmplitude() < minA) { minA = oscs[i].getAmplitude(); q = i; }
            return q;
        };

    constexpr int kMaxWayOsc = 512;
    std::vector<WaypointOscillator> wayOscs(kMaxWayOsc);
    auto getFreeWayOsc = [&]() -> int
        {
            for (int i = 0; i < kMaxWayOsc; ++i)
                if (!wayOscs[i].active) return i;
            int q = 0; double minA = wayOscs[0].getAmplitude();
            for (int i = 1; i < kMaxWayOsc; ++i)
                if (wayOscs[i].getAmplitude() < minA) { minA = wayOscs[i].getAmplitude(); q = i; }
            return q;
        };

    float* L = buf.getWritePointer(0);
    float* R = buf.getWritePointer(1);

    int evtIdx = 0;
    int pathIdx = 0;

    for (int s = 0; s < totalSamples; ++s)
    {
        const double t = static_cast<double>(s) / targetSR;

        while (evtIdx < static_cast<int>(evts.size()) && evts[evtIdx].time <= t)
        {
            const bool   blur = evts[evtIdx].blurEnabled && evts[evtIdx].blurSecs > 0.001f;
            const double decaySec = blur ? evts[evtIdx].blurSecs : 0.04;
            const double dm = std::exp(-5.0 / (juce::jmax(0.01, decaySec) * targetSR));
            oscs[getFreeOsc()].trigger(evts[evtIdx].frequency, evts[evtIdx].amplitude, dm);
            ++evtIdx;
        }

        while (pathIdx < static_cast<int>(pathTriggers.size())
            && pathTriggers[pathIdx].startTime <= t)
        {
            auto& pt = pathTriggers[pathIdx];
            wayOscs[getFreeWayOsc()].trigger(
                std::move(pt.scaledWps), pt.amp, pt.tailDecayMult, pt.hasTail);
            ++pathIdx;
        }

        float sample = 0.0f;
        for (auto& o : oscs)    if (o.active)  sample += o.processSample(targetSR);
        for (auto& w : wayOscs) if (w.active)  sample += w.processSample(t, targetSR);

        // NOTE: no tanh here -- soft-clip is applied at playback/export time
        L[s] = sample;
        R[s] = sample;
    }

    return buf;
}

//==============================================================================
// FLAC export
//
//  Uses the committed buffer directly -- no re-render required.
//  Tanh soft-clip is applied to an export copy so the committed master stays
//  in linear domain for future incremental additions.
//==============================================================================
bool FrequencyDrawerAudioProcessor::exportToFlac(const juce::File& file)
{
    juce::AudioBuffer<float> exportBuf;

    {
        juce::ScopedLock lk(committedBufferCS_);
        if (!committedBuffer_ || committedBuffer_->getNumSamples() == 0)
            return false;
        exportBuf = *committedBuffer_;   // copy
    }

    // Apply soft-clip to the export copy
    for (int ch = 0; ch < exportBuf.getNumChannels(); ++ch)
    {
        float* data = exportBuf.getWritePointer(ch);
        for (int s = 0; s < exportBuf.getNumSamples(); ++s)
            data[s] = std::tanh(data[s] * 0.6f) * 0.9f;
    }

    juce::FlacAudioFormat fmt;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    if (!stream) return false;

    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        fmt.createWriterFor(stream.get(), sampleRate_, 2, 24, {}, 8));
    if (!writer) return false;

    stream.release(); // writer owns the stream
    return writer->writeFromAudioSampleBuffer(exportBuf, 0, exportBuf.getNumSamples());
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrequencyDrawerAudioProcessor();
}