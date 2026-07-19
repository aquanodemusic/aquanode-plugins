#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
static float randFloat01(std::mt19937& rng)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

static float randFloatSym(std::mt19937& rng)   // -1 .. +1
{
    return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SampleFieldAudioProcessor::createParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Global pan  -1 .. +1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pan", "Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));

    // Global sample rate multiplier  0.25x .. 4x  (log)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rate", "Sample Rate",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.4f), 1.0f));

    // Global volume  0 .. 2
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "vol", "Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));

    // Per-sample pan randomisation  0 .. 1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "panRnd", "Pan Random",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // Per-sample rate randomisation  0 .. 1  (maps to ±2 octaves)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rateRnd", "Rate Random",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // Per-sample volume randomisation  0 .. 1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "volRnd", "Vol Random",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // Skip probability  0 .. 1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "skip", "Skip Prob",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // Time limit: 0.1s .. 10s; 10s = unlimited (stored as the max sentinel)
    // Using a skewed range so the lower end has more resolution.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "time", "Time",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.001f, 0.4f), 10.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
SampleFieldAudioProcessor::SampleFieldAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "STATE", createParams())
{
    apvts.addParameterListener("pan", this);
    apvts.addParameterListener("rate", this);
    apvts.addParameterListener("vol", this);
    apvts.addParameterListener("panRnd", this);
    apvts.addParameterListener("rateRnd", this);
    apvts.addParameterListener("volRnd", this);
    apvts.addParameterListener("skip", this);
    apvts.addParameterListener("time", this);

    p_pan = apvts.getRawParameterValue("pan")->load();
    p_rate = apvts.getRawParameterValue("rate")->load();
    p_vol = apvts.getRawParameterValue("vol")->load();
    p_panRnd = apvts.getRawParameterValue("panRnd")->load();
    p_rateRnd = apvts.getRawParameterValue("rateRnd")->load();
    p_volRnd = apvts.getRawParameterValue("volRnd")->load();
    p_skip = apvts.getRawParameterValue("skip")->load();
    p_time = apvts.getRawParameterValue("time")->load();
}

SampleFieldAudioProcessor::~SampleFieldAudioProcessor()
{
    apvts.removeParameterListener("pan", this);
    apvts.removeParameterListener("rate", this);
    apvts.removeParameterListener("vol", this);
    apvts.removeParameterListener("panRnd", this);
    apvts.removeParameterListener("rateRnd", this);
    apvts.removeParameterListener("volRnd", this);
    apvts.removeParameterListener("skip", this);
    apvts.removeParameterListener("time", this);
}

//==============================================================================
void SampleFieldAudioProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id == "pan")     p_pan = v;
    else if (id == "rate")    p_rate = v;
    else if (id == "vol")     p_vol = v;
    else if (id == "panRnd")  p_panRnd = v;
    else if (id == "rateRnd") p_rateRnd = v;
    else if (id == "volRnd")  p_volRnd = v;
    else if (id == "skip")    p_skip = v;
    else if (id == "time")    p_time = v;
}

void SampleFieldAudioProcessor::setTempoLock(int steps)
{
    tempoLockStepsValue.store(steps);
}

//==============================================================================
void SampleFieldAudioProcessor::prepareToPlay(double sr, int /*block*/)
{
    hostSampleRate = sr;
    juce::ScopedLock sl(voiceLock);
    for (auto& v : voices) v.reset();
}

void SampleFieldAudioProcessor::releaseResources() {}

bool SampleFieldAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
// Returns the time limit in host samples, or -1 for "unlimited".
int SampleFieldAudioProcessor::computeTimeLimitSamples()
{
    float timeSec = p_time.load();

    // Treat the maximum value (10 s) as "unlimited"
    if (timeSec >= 9.999f)
        return -1;

    int steps = tempoLockStepsValue.load();
    if (steps > 0)
    {
        // Snap to a musical duration: steps * one 8th-note length
        double bpm = hostBpm.load();
        double beatSec = 60.0 / bpm;          // one quarter note
        double eighthSec = beatSec * 0.5;
        timeSec = (float)(steps * eighthSec);
    }

    return juce::roundToInt(timeSec * (float)hostSampleRate);
}

//==============================================================================
int SampleFieldAudioProcessor::pickRandomSample()
{
    int n = numLoadedSamples.load();
    if (n == 0) return -1;

    if (randFloat01(rng) < p_skip.load())
        return -1;   // caller must handle the silent duration

    return std::uniform_int_distribution<int>(0, n - 1)(rng);
}

void SampleFieldAudioProcessor::assignSampleToVoice(Voice& v)
{
    // Reset fade/time state for the new sample slot
    v.fadeoutSamplesTotal = 0;
    v.fadeoutSamplesLeft = 0;
    v.timeLimitSamples = -1;

    int idx = pickRandomSample();

    if (idx < 0)
    {
        // --- SKIP: stay silent for the estimated duration of a random sample ---
        v.sampleIndex = -1;

        int n = numLoadedSamples.load();
        if (n > 0)
        {
            // Pick a random sample just to measure its length (don't play it)
            int refIdx = std::uniform_int_distribution<int>(0, n - 1)(rng);
            auto& sb = samples[refIdx];
            if (sb)
            {
                // Duration of the reference sample at the current read speed
                // We approximate readSpeed using the base rate (no per-sample rnd yet)
                double baseSpeed = sb->originalSampleRate / hostSampleRate
                    * (double)p_rate.load();
                if (baseSpeed > 0.0)
                {
                    int timeLimitSmp = computeTimeLimitSamples();
                    int sampleFrames = sb->buffer.getNumSamples();
                    int playFrames = (timeLimitSmp >= 0)
                        ? juce::jmin(timeLimitSmp, sampleFrames)
                        : sampleFrames;
                    // Convert sample frames → host frames
                    v.skipSamplesRemaining = juce::roundToInt(playFrames / baseSpeed);
                }
                else
                {
                    v.skipSamplesRemaining = juce::roundToInt(0.5 * hostSampleRate);
                }
            }
            else
            {
                // Fallback: 500 ms silence
                v.skipSamplesRemaining = juce::roundToInt(0.5 * hostSampleRate);
            }
        }
        else
        {
            v.skipSamplesRemaining = juce::roundToInt(0.5 * hostSampleRate);
        }
        return;
    }

    auto& sb = samples[idx];
    if (!sb) { v.sampleIndex = -1; return; }

    v.sampleIndex = idx;
    v.readPos = 0.0;
    v.skipSamplesRemaining = 0;

    // --- Rate / pitch ----------------------------------------------------------
    double baseSpeed = sb->originalSampleRate / hostSampleRate;
    baseSpeed *= (double)p_rate.load();

    // Per-sample rate randomisation: ±24 semitones (2 octaves) scaled by rateRnd
    float rateRnd = p_rateRnd.load();
    if (rateRnd > 0.0f)
    {
        float semis = randFloatSym(rng) * 24.0f * rateRnd;   // ±24 semitones max
        baseSpeed *= std::pow(2.0, (double)semis / 12.0);
    }
    v.readSpeed = baseSpeed;

    // --- Gain / pan ------------------------------------------------------------
    float pan = juce::jlimit(-1.0f, 1.0f,
        p_pan.load() + randFloatSym(rng) * p_panRnd.load());
    float vol = p_vol.load();
    float volR = p_volRnd.load();
    if (volR > 0.0f)
        vol *= juce::jmax(0.0f, 1.0f + randFloatSym(rng) * volR);

    float panAngle = (pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
    v.gainL = vol * std::cos(panAngle);
    v.gainR = vol * std::sin(panAngle);

    // --- Time limit ------------------------------------------------------------
    int timeLimitSmp = computeTimeLimitSamples();
    if (timeLimitSmp >= 0)
    {
        v.timeLimitSamples = timeLimitSmp;

        // Prepare a 10 ms fade-out that fires just before the time limit
        int fadeoutLen = juce::roundToInt(0.010 * hostSampleRate);
        v.fadeoutSamplesTotal = fadeoutLen;
        // fadeoutSamplesLeft starts at 0 and will be set when we hit the limit
    }
    else
    {
        v.timeLimitSamples = -1;
        v.fadeoutSamplesTotal = 0;
        v.fadeoutSamplesLeft = 0;
    }
}

//==============================================================================
void SampleFieldAudioProcessor::handleNoteOn(int note, float /*velocity*/)
{
    juce::ScopedLock sl(voiceLock);

    int freeIdx = -1;
    for (int i = 0; i < kNumVoices; ++i)
        if (!voices[i].active) { freeIdx = i; break; }

    if (freeIdx < 0) freeIdx = 0; // steal first

    Voice& v = voices[freeIdx];
    v.reset();
    v.active = true;
    v.midiNote = note;
    assignSampleToVoice(v);
}

void SampleFieldAudioProcessor::handleNoteOff(int note)
{
    juce::ScopedLock sl(voiceLock);
    for (auto& v : voices)
        if (v.active && v.midiNote == note)
            v.active = false;
}

//==============================================================================
void SampleFieldAudioProcessor::renderVoice(Voice& v,
    juce::AudioBuffer<float>& out,
    int startSample,
    int numSamples)
{
    if (!v.active || numSamples <= 0) return;

    auto* outL = out.getWritePointer(0, startSample);
    auto* outR = out.getWritePointer(1, startSample);

    // ---- SKIP silence countdown ----
    if (v.sampleIndex < 0)
    {
        if (v.skipSamplesRemaining > 0)
        {
            int consume = juce::jmin(numSamples, v.skipSamplesRemaining);
            v.skipSamplesRemaining -= consume;

            if (v.skipSamplesRemaining == 0 && consume < numSamples)
            {
                // Silence done — pick the next sample and continue rendering
                assignSampleToVoice(v);
                if (v.sampleIndex < 0 && v.skipSamplesRemaining == 0)
                {
                    // Skipped again with no further silence needed (edge case)
                    return;
                }
                renderVoice(v, out, startSample + consume, numSamples - consume);
            }
            return;
        }
        else
        {
            // Nothing to skip and no sample → deactivate
            v.active = false;
            return;
        }
    }

    auto& sb = samples[v.sampleIndex];
    if (!sb) { v.active = false; return; }

    const int totalFrames = sb->buffer.getNumSamples();
    const int numCh = sb->buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        // ---- Time limit / fade-out ----
        if (v.timeLimitSamples >= 0)
        {
            if (v.timeLimitSamples == 0)
            {
                // Time is up — move to the next sample
                assignSampleToVoice(v);
                if (!v.active) return;

                if (v.sampleIndex < 0)
                {
                    // Skipped: consume the remaining output silently via recursion
                    renderVoice(v, out, startSample + i, numSamples - i);
                    return;
                }

                // Re-fetch buffer pointer for the new sample
                auto& sb2 = samples[v.sampleIndex];
                if (!sb2) { v.active = false; return; }

                renderVoice(v, out, startSample + i, numSamples - i);
                return;
            }

            // Start the fade-out window when we're within fadeoutSamplesTotal of the limit
            if (v.fadeoutSamplesTotal > 0 && v.fadeoutSamplesLeft == 0
                && v.timeLimitSamples <= v.fadeoutSamplesTotal)
            {
                v.fadeoutSamplesLeft = v.timeLimitSamples; // start counting down
            }

            --v.timeLimitSamples;
        }

        // ---- Per-sample fade-out gain ----
        float fadeGain = 1.0f;
        if (v.fadeoutSamplesLeft > 0)
        {
            fadeGain = (float)v.fadeoutSamplesLeft / (float)v.fadeoutSamplesTotal;
            --v.fadeoutSamplesLeft;
        }

        // ---- End of sample data ----
        if (v.readPos >= totalFrames)
        {
            assignSampleToVoice(v);
            if (!v.active) return;

            if (v.sampleIndex < 0)
            {
                renderVoice(v, out, startSample + i, numSamples - i);
                return;
            }

            renderVoice(v, out, startSample + i, numSamples - i);
            return;
        }

        // ---- Linear interpolation ----
        int   posI = (int)v.readPos;
        float frac = (float)(v.readPos - posI);
        int   posI1 = juce::jmin(posI + 1, totalFrames - 1);

        float sL, sR;
        if (numCh == 1)
        {
            float s0 = sb->buffer.getSample(0, posI);
            float s1 = sb->buffer.getSample(0, posI1);
            sL = sR = s0 + frac * (s1 - s0);
        }
        else
        {
            float s0L = sb->buffer.getSample(0, posI);
            float s1L = sb->buffer.getSample(0, posI1);
            float s0R = sb->buffer.getSample(1, posI);
            float s1R = sb->buffer.getSample(1, posI1);
            sL = s0L + frac * (s1L - s0L);
            sR = s0R + frac * (s1R - s0R);
        }

        outL[i] += sL * v.gainL * fadeGain;
        outR[i] += sR * v.gainR * fadeGain;

        v.readPos += v.readSpeed;
    }
}

//==============================================================================
void SampleFieldAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Update host BPM from playhead
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm().hasValue())
                hostBpm.store(*pos->getBpm());
        }
    }

    buffer.clear();

    for (const auto meta : midiMessages)
    {
        auto msg = meta.getMessage();
        if (msg.isNoteOn())  handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) handleNoteOff(msg.getNoteNumber());
    }

    {
        juce::ScopedLock sl(voiceLock);
        for (auto& v : voices)
            renderVoice(v, buffer, 0, buffer.getNumSamples());
    }
}

//==============================================================================
void SampleFieldAudioProcessor::loadSamples(const juce::Array<juce::File>& files)
{
    juce::AudioFormatManager formatMgr;
    formatMgr.registerBasicFormats();

    juce::ScopedLock pl(pathLock);

    for (const auto& f : files)
    {
        if (numLoadedSamples.load() >= kMaxSamples) break;

        std::unique_ptr<juce::AudioFormatReader> reader(
            formatMgr.createReaderFor(f));
        if (!reader) continue;

        auto sb = std::make_unique<SampleBuffer>();
        sb->originalSampleRate = reader->sampleRate;
        sb->filePath = f.getFullPathName();

        sb->buffer.setSize((int)juce::jmin((int)reader->numChannels, 2),
            (int)reader->lengthInSamples);
        reader->read(&sb->buffer, 0, (int)reader->lengthInSamples, 0, true, true);

        int idx = numLoadedSamples.load();
        samples[idx] = std::move(sb);
        numLoadedSamples.fetch_add(1);
        loadedFilePaths.add(f.getFullPathName());
    }
}

void SampleFieldAudioProcessor::unloadAllSamples()
{
    {
        juce::ScopedLock sl(voiceLock);
        for (auto& v : voices) v.reset();
    }

    int n = numLoadedSamples.exchange(0);
    for (int i = 0; i < n; ++i)
        samples[i].reset();

    juce::ScopedLock pl(pathLock);
    loadedFilePaths.clear();
}

//==============================================================================
void SampleFieldAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Save tempo-lock state as an attribute on the root
    state.setProperty("tempoLockSteps", tempoLockStepsValue.load(), nullptr);

    auto pathsXml = std::make_unique<juce::XmlElement>("SAMPLE_PATHS");
    {
        juce::ScopedLock pl(pathLock);
        for (const auto& p : loadedFilePaths)
        {
            auto* child = pathsXml->createNewChildElement("PATH");
            child->setAttribute("v", p);
        }
    }
    state.appendChild(juce::ValueTree::fromXml(*pathsXml), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SampleFieldAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml) return;

    auto state = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(state);

    // Restore cached param values
    p_pan = apvts.getRawParameterValue("pan")->load();
    p_rate = apvts.getRawParameterValue("rate")->load();
    p_vol = apvts.getRawParameterValue("vol")->load();
    p_panRnd = apvts.getRawParameterValue("panRnd")->load();
    p_rateRnd = apvts.getRawParameterValue("rateRnd")->load();
    p_volRnd = apvts.getRawParameterValue("volRnd")->load();
    p_skip = apvts.getRawParameterValue("skip")->load();
    p_time = apvts.getRawParameterValue("time")->load();

    // Restore tempo lock
    int tls = (int)state.getProperty("tempoLockSteps", 0);
    tempoLockStepsValue.store(tls);

    // Re-load files
    auto pathsTree = state.getChildWithName("SAMPLE_PATHS");
    if (pathsTree.isValid())
    {
        juce::Array<juce::File> filesToLoad;
        for (int i = 0; i < pathsTree.getNumChildren(); ++i)
        {
            juce::String p = pathsTree.getChild(i).getProperty("v").toString();
            if (p.isNotEmpty())
            {
                juce::File f(p);
                if (f.existsAsFile()) filesToLoad.add(f);
            }
        }
        if (!filesToLoad.isEmpty())
            loadSamples(filesToLoad);
    }
}

//==============================================================================
juce::AudioProcessorEditor* SampleFieldAudioProcessor::createEditor()
{
    return new SampleFieldAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SampleFieldAudioProcessor();
}