#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
static float randFloat01(std::mt19937& rng)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

static float randFloatSym(std::mt19937& rng)
{
    return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SampleFieldAudioProcessor::createParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pan", "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rate", "Sample Rate", juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.4f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "vol", "Volume", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "panRnd", "Pan Random", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rateRnd", "Rate Random", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "volRnd", "Vol Random", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "skip", "Skip Prob", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "time", "Time", juce::NormalisableRange<float>(0.1f, 10.0f, 0.001f, 0.4f), 10.0f));

    // New Delay Parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayTime", "Delay Time", juce::NormalisableRange<float>(0.1f, 10.0f, 0.001f, 0.4f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayVol", "Delay Volume", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayProb", "Delay Prob", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
SampleFieldAudioProcessor::SampleFieldAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
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
    apvts.addParameterListener("delayTime", this);
    apvts.addParameterListener("delayVol", this);
    apvts.addParameterListener("delayProb", this);

    p_pan = apvts.getRawParameterValue("pan")->load();
    p_rate = apvts.getRawParameterValue("rate")->load();
    p_vol = apvts.getRawParameterValue("vol")->load();
    p_panRnd = apvts.getRawParameterValue("panRnd")->load();
    p_rateRnd = apvts.getRawParameterValue("rateRnd")->load();
    p_volRnd = apvts.getRawParameterValue("volRnd")->load();
    p_skip = apvts.getRawParameterValue("skip")->load();
    p_time = apvts.getRawParameterValue("time")->load();
    p_delayTime = apvts.getRawParameterValue("delayTime")->load();
    p_delayVol = apvts.getRawParameterValue("delayVol")->load();
    p_delayProb = apvts.getRawParameterValue("delayProb")->load();
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
    apvts.removeParameterListener("delayTime", this);
    apvts.removeParameterListener("delayVol", this);
    apvts.removeParameterListener("delayProb", this);
}

//==============================================================================
void SampleFieldAudioProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id == "pan")          p_pan = v;
    else if (id == "rate")    p_rate = v;
    else if (id == "vol")     p_vol = v;
    else if (id == "panRnd")  p_panRnd = v;
    else if (id == "rateRnd") p_rateRnd = v;
    else if (id == "volRnd")  p_volRnd = v;
    else if (id == "skip")    p_skip = v;
    else if (id == "time")    p_time = v;
    else if (id == "delayTime") p_delayTime = v;
    else if (id == "delayVol")  p_delayVol = v;
    else if (id == "delayProb") p_delayProb = v;
}

void SampleFieldAudioProcessor::setTempoLock(int steps) { tempoLockStepsValue.store(steps); }
void SampleFieldAudioProcessor::setDelayTempoLock(int steps) { delayTempoLockStepsValue.store(steps); }

//==============================================================================
void SampleFieldAudioProcessor::prepareToPlay(double sr, int /*block*/)
{
    hostSampleRate = sr;
    juce::ScopedLock sl(voiceLock);

    // Allocate max possible size for 10 seconds worth of delay samples per voice
    int maxDelaySamples = juce::roundToInt(10.0 * sr);
    for (auto& v : voices)
    {
        v.delayBufferL.assign(maxDelaySamples, 0.0f);
        v.delayBufferR.assign(maxDelaySamples, 0.0f);
        v.reset();
    }
}

void SampleFieldAudioProcessor::releaseResources() {}

bool SampleFieldAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
int SampleFieldAudioProcessor::computeTimeLimitSamples()
{
    float timeSec = p_time.load();
    if (timeSec >= 9.999f) return -1;

    int steps = tempoLockStepsValue.load();
    if (steps > 0)
    {
        double bpm = hostBpm.load();
        double beatSec = 60.0 / bpm;
        double eighthSec = beatSec * 0.5;
        timeSec = (float)(steps * eighthSec);
    }
    return juce::roundToInt(timeSec * (float)hostSampleRate);
}

int SampleFieldAudioProcessor::computeDelayTimeSamples()
{
    float timeSec = p_delayTime.load();
    int steps = delayTempoLockStepsValue.load();
    if (steps > 0)
    {
        double bpm = hostBpm.load();
        double beatSec = 60.0 / bpm;
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
    if (randFloat01(rng) < p_skip.load()) return -1;

    return std::uniform_int_distribution<int>(0, n - 1)(rng);
}

void SampleFieldAudioProcessor::assignSampleToVoice(Voice& v)
{
    v.fadeoutSamplesTotal = 0;
    v.fadeoutSamplesLeft = 0;
    v.timeLimitSamples = -1;

    // If note off has already occurred, do not assign a new sample sequence
    if (v.noteOffReceived)
    {
        v.dryActive = false;
        v.sampleIndex = -1;
        v.skipSamplesRemaining = 0;
        return;
    }

    int idx = pickRandomSample();

    if (idx < 0)
    {
        v.sampleIndex = -1;
        int n = numLoadedSamples.load();
        if (n > 0)
        {
            int refIdx = std::uniform_int_distribution<int>(0, n - 1)(rng);
            auto& sb = samples[refIdx];
            if (sb)
            {
                double baseSpeed = sb->originalSampleRate / hostSampleRate * (double)p_rate.load();
                if (baseSpeed > 0.0)
                {
                    int timeLimitSmp = computeTimeLimitSamples();
                    int sampleFrames = sb->buffer.getNumSamples();
                    int playFrames = (timeLimitSmp >= 0) ? juce::jmin(timeLimitSmp, sampleFrames) : sampleFrames;
                    v.skipSamplesRemaining = juce::roundToInt(playFrames / baseSpeed);
                }
                else
                {
                    v.skipSamplesRemaining = juce::roundToInt(0.5 * hostSampleRate);
                }
            }
            else
            {
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

    // Roll probability check to see if this specific sample instance triggers the delay line
    v.feedDelay = (randFloat01(rng) < p_delayProb.load());

    // --- Rate / pitch ----------------------------------------------------------
    double baseSpeed = sb->originalSampleRate / hostSampleRate;
    baseSpeed *= (double)p_rate.load();

    float rateRnd = p_rateRnd.load();
    if (rateRnd > 0.0f)
    {
        float semis = randFloatSym(rng) * 24.0f * rateRnd;
        baseSpeed *= std::pow(2.0, (double)semis / 12.0);
    }
    v.readSpeed = baseSpeed;

    // --- Gain / pan ------------------------------------------------------------
    float pan = juce::jlimit(-1.0f, 1.0f, p_pan.load() + randFloatSym(rng) * p_panRnd.load());
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
        v.fadeoutSamplesTotal = juce::roundToInt(0.010 * hostSampleRate);
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

    if (freeIdx < 0) freeIdx = 0;

    Voice& v = voices[freeIdx];
    v.reset();
    v.active = true;
    v.dryActive = true;
    v.noteOffReceived = false;
    v.midiNote = note;
    assignSampleToVoice(v);
}

void SampleFieldAudioProcessor::handleNoteOff(int note)
{
    juce::ScopedLock sl(voiceLock);
    for (auto& v : voices)
    {
        if (v.active && v.midiNote == note)
        {
            v.noteOffReceived = true; // Signal the voice to wind down, letting tails clear
        }
    }
}

//==============================================================================
void SampleFieldAudioProcessor::renderVoiceDry(Voice& v,
    juce::AudioBuffer<float>& out,
    int startSample,
    int numSamples)
{
    if (!v.dryActive || numSamples <= 0) return;

    auto* outL = out.getWritePointer(0, startSample);
    auto* outR = out.getWritePointer(1, startSample);

    if (v.sampleIndex < 0)
    {
        if (v.skipSamplesRemaining > 0)
        {
            int consume = juce::jmin(numSamples, v.skipSamplesRemaining);
            v.skipSamplesRemaining -= consume;

            if (v.skipSamplesRemaining == 0 && consume < numSamples)
            {
                assignSampleToVoice(v);
                if (v.sampleIndex < 0 && v.skipSamplesRemaining == 0) return;
                renderVoiceDry(v, out, startSample + consume, numSamples - consume);
            }
            return;
        }
        else
        {
            v.dryActive = false;
            return;
        }
    }

    auto& sb = samples[v.sampleIndex];
    if (!sb) { v.dryActive = false; return; }

    const int totalFrames = sb->buffer.getNumSamples();
    const int numCh = sb->buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.timeLimitSamples >= 0)
        {
            if (v.timeLimitSamples == 0)
            {
                assignSampleToVoice(v);
                if (!v.dryActive) return;

                if (v.sampleIndex < 0)
                {
                    renderVoiceDry(v, out, startSample + i, numSamples - i);
                    return;
                }

                auto& sb2 = samples[v.sampleIndex];
                if (!sb2) { v.dryActive = false; return; }

                renderVoiceDry(v, out, startSample + i, numSamples - i);
                return;
            }

            if (v.fadeoutSamplesTotal > 0 && v.fadeoutSamplesLeft == 0
                && v.timeLimitSamples <= v.fadeoutSamplesTotal)
            {
                v.fadeoutSamplesLeft = v.timeLimitSamples;
            }

            --v.timeLimitSamples;
        }

        float fadeGain = 1.0f;
        if (v.fadeoutSamplesLeft > 0)
        {
            fadeGain = (float)v.fadeoutSamplesLeft / (float)v.fadeoutSamplesTotal;
            --v.fadeoutSamplesLeft;
        }

        if (v.readPos >= totalFrames)
        {
            assignSampleToVoice(v);
            if (!v.dryActive) return;

            if (v.sampleIndex < 0)
            {
                renderVoiceDry(v, out, startSample + i, numSamples - i);
                return;
            }

            renderVoiceDry(v, out, startSample + i, numSamples - i);
            return;
        }

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

void SampleFieldAudioProcessor::renderVoice(Voice& v,
    juce::AudioBuffer<float>& out,
    int startSample,
    int numSamples)
{
    if (!v.active || numSamples <= 0) return;

    juce::AudioBuffer<float> dryBuffer(2, numSamples);
    dryBuffer.clear();

    bool wasDryActive = v.dryActive;

    // Render original sampler block output into intermediate scratch space
    renderVoiceDry(v, dryBuffer, 0, numSamples);

    if (wasDryActive && !v.dryActive)
    {
        // Sample timeline went dead, cache maximum tail time bounds (10 seconds max)
        v.delayTailSamples = juce::roundToInt(10.0 * hostSampleRate);
    }

    auto* outL = out.getWritePointer(0, startSample);
    auto* outR = out.getWritePointer(1, startSample);
    auto* dryL = dryBuffer.getReadPointer(0);
    auto* dryR = dryBuffer.getReadPointer(1);

    int dt = computeDelayTimeSamples();
    int maxDelaySamples = (int)v.delayBufferL.size();
    float fb = p_delayVol.load();

    for (int i = 0; i < numSamples; ++i)
    {
        float sL = dryL[i];
        float sR = dryR[i];

        float delayOutL = 0.0f;
        float delayOutR = 0.0f;

        if (maxDelaySamples > 0 && dt > 0)
        {
            int readPos = v.delayWritePos - dt;
            if (readPos < 0) readPos += maxDelaySamples;

            delayOutL = v.delayBufferL[readPos];
            delayOutR = v.delayBufferR[readPos];

            // Mix input signal into feedback loop strictly if evaluated positively for this sample instance
            float feedL = v.feedDelay ? sL : 0.0f;
            float feedR = v.feedDelay ? sR : 0.0f;

            v.delayBufferL[v.delayWritePos] = feedL + delayOutL * fb;
            v.delayBufferR[v.delayWritePos] = feedR + delayOutR * fb;

            v.delayWritePos = (v.delayWritePos + 1) % maxDelaySamples;
        }

        outL[i] += sL + delayOutL;
        outR[i] += sR + delayOutR;

        if (!v.dryActive)
        {
            if (v.delayTailSamples > 0)
            {
                --v.delayTailSamples;
                if (v.delayTailSamples == 0)
                {
                    v.active = false;
                    break;
                }
            }
            else
            {
                v.active = false;
                break;
            }
        }
    }
}

//==============================================================================
void SampleFieldAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

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

        std::unique_ptr<juce::AudioFormatReader> reader(formatMgr.createReaderFor(f));
        if (!reader) continue;

        auto sb = std::make_unique<SampleBuffer>();
        sb->originalSampleRate = reader->sampleRate;
        sb->filePath = f.getFullPathName();

        sb->buffer.setSize((int)juce::jmin((int)reader->numChannels, 2), (int)reader->lengthInSamples);
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
    state.setProperty("tempoLockSteps", tempoLockStepsValue.load(), nullptr);
    state.setProperty("delayTempoLockSteps", delayTempoLockStepsValue.load(), nullptr);

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

    p_pan = apvts.getRawParameterValue("pan")->load();
    p_rate = apvts.getRawParameterValue("rate")->load();
    p_vol = apvts.getRawParameterValue("vol")->load();
    p_panRnd = apvts.getRawParameterValue("panRnd")->load();
    p_rateRnd = apvts.getRawParameterValue("rateRnd")->load();
    p_volRnd = apvts.getRawParameterValue("volRnd")->load();
    p_skip = apvts.getRawParameterValue("skip")->load();
    p_time = apvts.getRawParameterValue("time")->load();
    p_delayTime = apvts.getRawParameterValue("delayTime")->load();
    p_delayVol = apvts.getRawParameterValue("delayVol")->load();
    p_delayProb = apvts.getRawParameterValue("delayProb")->load();

    int tls = (int)state.getProperty("tempoLockSteps", 0);
    tempoLockStepsValue.store(tls);

    int dtls = (int)state.getProperty("delayTempoLockSteps", 0);
    delayTempoLockStepsValue.store(dtls);

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

juce::AudioProcessorEditor* SampleFieldAudioProcessor::createEditor()
{
    return new SampleFieldAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SampleFieldAudioProcessor();
}