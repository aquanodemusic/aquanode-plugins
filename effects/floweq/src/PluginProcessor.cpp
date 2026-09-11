/*
    FlowEQ - PluginProcessor.cpp
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
const std::vector<SyncDivision>& getSyncDivisions()
{
    // 1/32 .. 8/1, each normal / triplet (x2/3) / dotted (x1.5)
    static const std::vector<SyncDivision> divisions = [] {
        struct Base { juce::String name; double quarters; };
        static const Base bases[] = {
            { "1/32", 0.125 }, { "1/16", 0.25 }, { "1/8", 0.5 }, { "1/4", 1.0 },
            { "1/2",  2.0   }, { "1/1",  4.0  }, { "2/1", 8.0 }, { "4/1", 16.0 },
            { "8/1",  32.0  }
        };
        std::vector<SyncDivision> out;
        for (auto& b : bases)
        {
            out.push_back({ b.name,            b.quarters });
            out.push_back({ b.name + "T",       b.quarters * (2.0 / 3.0) });
            out.push_back({ b.name + "D",       b.quarters * 1.5 });
        }
        return out;
        }();
    return divisions;
}

double syncDivisionToSeconds(int choiceIndex, double bpm)
{
    const auto& divs = getSyncDivisions();
    choiceIndex = juce::jlimit(0, (int)divs.size() - 1, choiceIndex);
    double secondsPerQuarter = 60.0 / juce::jmax(1.0, bpm);
    return divs[(size_t)choiceIndex].beatsInQuarterNotes * secondsPerQuarter;
}

//==============================================================================
juce::String FlowEQAudioProcessor::paramIdBase(int i, CurveTarget t)
{
    juce::String tName = t == CurveTarget::Frequency ? "freq" : (t == CurveTarget::Gain ? "gain" : "q");
    return "f" + juce::String(i) + "_" + tName;
}

juce::AudioProcessorValueTreeState::ParameterLayout FlowEQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::StringArray syncChoices;
    for (auto& d : getSyncDivisions())
        syncChoices.add(d.label);

    for (int i = 0; i < kNumFilters; ++i)
    {
        auto idx = juce::String(i);

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "f" + idx + "_freq", "Filter " + idx + " Frequency",
            juce::NormalisableRange<float>(kMinFreq, kMaxFreq, 0.01f, 0.3f), kDefaultFreqs[i],
            juce::AudioParameterFloatAttributes().withLabel("Hz")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "f" + idx + "_gain", "Filter " + idx + " Gain",
            juce::NormalisableRange<float>(kMinGainDb, kMaxGainDb, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "f" + idx + "_q", "Filter " + idx + " Q",
            juce::NormalisableRange<float>(kMinQ, kMaxQ, 0.001f, 0.4f), 0.707f, juce::AudioParameterFloatAttributes()));

        for (auto t : { CurveTarget::Frequency, CurveTarget::Gain, CurveTarget::Q })
        {
            auto base = paramIdBase(i, t);
            juce::String suffix = t == CurveTarget::Frequency ? " Freq" : (t == CurveTarget::Gain ? " Gain" : " Q");

            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                base + "_time", "Filter " + idx + suffix + " Time",
                juce::NormalisableRange<float>(kMinTimeSec, kMaxTimeSec, 0.01f, 0.3f), 2.0f,
                juce::AudioParameterFloatAttributes().withLabel("s")));

            params.push_back(std::make_unique<juce::AudioParameterBool>(
                base + "_sync", "Filter " + idx + suffix + " Sync", false));

            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                base + "_syncdiv", "Filter " + idx + suffix + " Sync Div", syncChoices, 6)); // default 1/4
        }
    }

    return { params.begin(), params.end() };
}

juce::AudioParameterFloat* FlowEQAudioProcessor::getFreqParam(int i) const
{
    return (juce::AudioParameterFloat*)apvts.getParameter("f" + juce::String(i) + "_freq");
}

juce::AudioParameterFloat* FlowEQAudioProcessor::getGainParam(int i) const
{
    return (juce::AudioParameterFloat*)apvts.getParameter("f" + juce::String(i) + "_gain");
}

juce::AudioParameterFloat* FlowEQAudioProcessor::getQParam(int i) const
{
    return (juce::AudioParameterFloat*)apvts.getParameter("f" + juce::String(i) + "_q");
}

juce::AudioParameterFloat* FlowEQAudioProcessor::getTimeParam(int i, CurveTarget t) const
{
    return (juce::AudioParameterFloat*)apvts.getParameter(paramIdBase(i, t) + "_time");
}

juce::AudioParameterBool* FlowEQAudioProcessor::getSyncParam(int i, CurveTarget t) const
{
    return (juce::AudioParameterBool*)apvts.getParameter(paramIdBase(i, t) + "_sync");
}

juce::AudioParameterChoice* FlowEQAudioProcessor::getSyncDivParam(int i, CurveTarget t) const
{
    return (juce::AudioParameterChoice*)apvts.getParameter(paramIdBase(i, t) + "_syncdiv");
}

//==============================================================================
FlowEQAudioProcessor::FlowEQAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < kNumFilters; ++i)
        for (auto& c : curves[(size_t)i])
            c.reset();
}

FlowEQAudioProcessor::~FlowEQAudioProcessor() {}

//==============================================================================
void FlowEQAudioProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    for (auto& f : filters)
    {
        f.prepare(spec);
        f.reset();
    }

    for (auto& perFilter : modEngines)
        for (auto& eng : perFilter)
            eng.prepare(sr);

    for (auto& perFilter : valueSmoothers)
        for (auto& s : perFilter)
            s.reset();
}

void FlowEQAudioProcessor::releaseResources() {}

bool FlowEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

double FlowEQAudioProcessor::resolveCycleSeconds(int filterIndex, int curveIdx)
{
    auto target = (CurveTarget)curveIdx;
    bool sync = getSyncParam(filterIndex, target)->get();
    if (sync)
    {
        int choice = getSyncDivParam(filterIndex, target)->getIndex();
        return syncDivisionToSeconds(choice, currentBpm.load());
    }
    return (double)getTimeParam(filterIndex, target)->get();
}

void FlowEQAudioProcessor::updateFilterCoefficients(int i, float liveFreq, float liveGainDb, float liveQ)
{
    liveFreq = juce::jlimit(kMinFreq, (float)(sampleRate * 0.45), liveFreq);
    liveQ = juce::jlimit(kMinQ, kMaxQ, liveQ);
    float gainLinear = juce::Decibels::decibelsToGain(liveGainDb);

    *filters[(size_t)i].state = *FilterCoeffs::makePeakFilter(sampleRate, liveFreq, liveQ, gainLinear);
}

void FlowEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    // Read host tempo (fallback 120 BPM)
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                currentBpm.store(*bpm);
    }

    int numSamples = buffer.getNumSamples();

    for (int i = 0; i < kNumFilters; ++i)
    {
        float baseFreq = getFreqParam(i)->get();
        float baseGain = getGainParam(i)->get();
        float baseQ = getQParam(i)->get();

        // Freq (0), Gain (1), Q (2)
        double freqSeconds = resolveCycleSeconds(i, 0);
        double gainSeconds = resolveCycleSeconds(i, 1);
        double qSeconds = resolveCycleSeconds(i, 2);

        modEngines[(size_t)i][0].advance(numSamples, freqSeconds);
        modEngines[(size_t)i][1].advance(numSamples, gainSeconds);
        modEngines[(size_t)i][2].advance(numSamples, qSeconds);

        // Get the normalised modulation value (0..1) - whether from the 128
        // freehand points or from the active 2048-point wavetable frame.
        // Both use the same periodic Catmull-Rom interpolation, so the
        // cycle wrap is structurally click-free in either case.
        float freqNorm = getModulatedNormalized(i, 0, modEngines[(size_t)i][0].getPhase());
        float gainNorm = getModulatedNormalized(i, 1, modEngines[(size_t)i][1].getPhase());
        float qNorm = getModulatedNormalized(i, 2, modEngines[(size_t)i][2].getPhase());

        // "Undrawn" (== completely flat at 0.5) only applies in freehand
        // mode; in wavetable mode the curve is by definition always "active".
        bool freqUsesCurve = isUsingWavetable(i, CurveTarget::Frequency);
        bool gainUsesCurve = isUsingWavetable(i, CurveTarget::Gain);
        bool qUsesCurve = isUsingWavetable(i, CurveTarget::Q);

        if (!freqUsesCurve)
            for (auto v : curves[(size_t)i][0].points) if (std::abs(v - 0.5f) > 0.0001f) { freqUsesCurve = true; break; }
        if (!gainUsesCurve)
            for (auto v : curves[(size_t)i][1].points) if (std::abs(v - 0.5f) > 0.0001f) { gainUsesCurve = true; break; }
        if (!qUsesCurve)
            for (auto v : curves[(size_t)i][2].points) if (std::abs(v - 0.5f) > 0.0001f) { qUsesCurve = true; break; }

        // Mapping: the Freq curve is ABSOLUTE 20-20000Hz (log scale) -
        // regardless of whether the points were hand-drawn or come from
        // the wavetable.
        float rawFreq = freqUsesCurve ? kMinFreq * std::pow(kMaxFreq / kMinFreq, freqNorm) : baseFreq;

        // Gain curve: 0.5 == 0dB change -> additive to the base, -24..+24
        float gainDelta = (gainNorm - 0.5f) * 2.0f * kMaxGainDb;
        float rawGain = juce::jlimit(kMinGainDb, kMaxGainDb, baseGain + (gainUsesCurve ? gainDelta : 0.0f));

        // Q curve: 0.5 == no influence, otherwise multiplicative by +-2 octaves
        float qMult = qUsesCurve ? std::pow(2.0f, (qNorm - 0.5f) * 4.0f) : 1.0f;
        float rawQ = juce::jlimit(kMinQ, kMaxQ, baseQ * qMult);

        // Block-rate smoothing against clicks (wrap, hard-edged curves,
        // wavetable frame changes during playback).
        float blockDurationSec = (float)numSamples / (float)sampleRate;
        float alpha = 1.0f - std::exp(-blockDurationSec / kSmoothingTimeSeconds);

        float liveFreq = valueSmoothers[(size_t)i][0].process(rawFreq, alpha);
        float liveGain = valueSmoothers[(size_t)i][1].process(rawGain, alpha);
        float liveQ = valueSmoothers[(size_t)i][2].process(rawQ, alpha);

        updateFilterCoefficients(i, liveFreq, liveGain, liveQ);

        lastLiveValue[(size_t)i][0].store(rawFreq); // UI shows the "raw" curve value, not smoothed
        lastLiveValue[(size_t)i][1].store(rawGain);
        lastLiveValue[(size_t)i][2].store(rawQ);
        lastLivePhase[(size_t)i][0].store(modEngines[(size_t)i][0].getPhase());
        lastLivePhase[(size_t)i][1].store(modEngines[(size_t)i][1].getPhase());
        lastLivePhase[(size_t)i][2].store(modEngines[(size_t)i][2].getPhase());

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        filters[(size_t)i].process(context);
    }
}

//==============================================================================
FlowCurveData& FlowEQAudioProcessor::getCurve(int filterIndex, CurveTarget target)
{
    return curves[(size_t)filterIndex][(size_t)target];
}

const FlowCurveData& FlowEQAudioProcessor::getCurve(int filterIndex, CurveTarget target) const
{
    return curves[(size_t)filterIndex][(size_t)target];
}

void FlowEQAudioProcessor::smoothCurve(int filterIndex, CurveTarget target)
{
    if (isUsingWavetable(filterIndex, target))
        return; // wavetable data is read-only, nothing to smooth

    auto& curve = curves[(size_t)filterIndex][(size_t)target];

    // Small periodic weighted moving average (5-tap: 1-2-3-2-1) to soften
    // hard edges and jitter in a user-drawn curve, without flattening it out.
    // Periodic wrap keeps the cycle seamless, matching catmullRomPeriodic.
    std::array<float, kNumCurvePoints> smoothed;
    for (int i = 0; i < kNumCurvePoints; ++i)
    {
        auto wrap = [](int idx) { return ((idx % kNumCurvePoints) + kNumCurvePoints) % kNumCurvePoints; };
        float sum = curve.points[(size_t)wrap(i - 2)] * 1.0f
            + curve.points[(size_t)wrap(i - 1)] * 2.0f
            + curve.points[(size_t)i] * 3.0f
            + curve.points[(size_t)wrap(i + 1)] * 2.0f
            + curve.points[(size_t)wrap(i + 2)] * 1.0f;
        smoothed[(size_t)i] = sum / 9.0f;
    }
    curve.points = smoothed;
}

//==============================================================================
bool FlowEQAudioProcessor::loadWavetable(int i, CurveTarget target, const juce::File& file)
{
    auto newSet = std::make_shared<WavetableSet>();
    if (!newSet->loadFromFile(file))
        return false;

    auto t = (size_t)target;
    {
        const juce::SpinLock::ScopedLockType sl(wavetableSwapLock);
        wavetables[(size_t)i][t] = newSet; // atomic swap of the whole object
    }
    wavetableFrame[(size_t)i][t].store(0);
    useWavetable[(size_t)i][t].store(true);
    return true;
}

void FlowEQAudioProcessor::setUseWavetable(int i, CurveTarget target, bool shouldUse)
{
    useWavetable[(size_t)i][(size_t)target].store(shouldUse);
}

bool FlowEQAudioProcessor::isUsingWavetable(int i, CurveTarget target) const
{
    return useWavetable[(size_t)i][(size_t)target].load();
}

void FlowEQAudioProcessor::setWavetableFrame(int i, CurveTarget target, int frameIndex)
{
    int numFrames = getWavetableNumFrames(i, target);
    if (numFrames <= 0) return;
    wavetableFrame[(size_t)i][(size_t)target].store(juce::jlimit(0, numFrames - 1, frameIndex));
}

int FlowEQAudioProcessor::getWavetableFrame(int i, CurveTarget target) const
{
    return wavetableFrame[(size_t)i][(size_t)target].load();
}

int FlowEQAudioProcessor::getWavetableNumFrames(int i, CurveTarget target) const
{
    const juce::SpinLock::ScopedLockType sl(wavetableSwapLock);
    auto wt = wavetables[(size_t)i][(size_t)target];
    return wt != nullptr ? wt->numFrames : 0;
}

juce::String FlowEQAudioProcessor::getWavetableName(int i, CurveTarget target) const
{
    const juce::SpinLock::ScopedLockType sl(wavetableSwapLock);
    auto wt = wavetables[(size_t)i][(size_t)target];
    return wt != nullptr ? wt->name : juce::String();
}

int FlowEQAudioProcessor::getActiveNumPoints(int i, CurveTarget target) const
{
    if (isUsingWavetable(i, target) && getWavetableNumFrames(i, target) > 0)
        return WavetableSet::kFrameSize;
    return kNumCurvePoints;
}

float FlowEQAudioProcessor::getActivePointNormalized(int i, CurveTarget target, int pointIndex) const
{
    if (isUsingWavetable(i, target))
    {
        std::shared_ptr<WavetableSet> wt;
        { const juce::SpinLock::ScopedLockType sl(wavetableSwapLock); wt = wavetables[(size_t)i][(size_t)target]; }
        if (wt != nullptr && wt->numFrames > 0)
        {
            const float* frame = wt->getFramePointer(wavetableFrame[(size_t)i][(size_t)target].load());
            pointIndex = juce::jlimit(0, WavetableSet::kFrameSize - 1, pointIndex);
            return frame[pointIndex] * 0.5f + 0.5f; // bipolar -1..1 -> 0..1
        }
    }
    pointIndex = juce::jlimit(0, kNumCurvePoints - 1, pointIndex);
    return curves[(size_t)i][(size_t)target].points[(size_t)pointIndex];
}

float FlowEQAudioProcessor::getModulatedNormalized(int i, int curveIdx, double phase) const
{
    auto target = (CurveTarget)curveIdx;
    if (isUsingWavetable(i, target))
    {
        std::shared_ptr<WavetableSet> wt;
        { const juce::SpinLock::ScopedLockType sl(wavetableSwapLock); wt = wavetables[(size_t)i][(size_t)curveIdx]; }
        if (wt != nullptr && wt->numFrames > 0)
        {
            const float* frame = wt->getFramePointer(wavetableFrame[(size_t)i][(size_t)curveIdx].load());
            // Same periodic interpolation as for the 128 freehand points,
            // just over 2048 points and with bipolar->0..1 mapping.
            return catmullRomPeriodic(WavetableSet::kFrameSize, (float)phase,
                [frame](int idx) { return frame[idx] * 0.5f + 0.5f; });
        }
    }
    return curves[(size_t)i][(size_t)curveIdx].getInterpolated((float)phase);
}

FlowEQAudioProcessor::LiveModState FlowEQAudioProcessor::getLiveState(int filterIndex, CurveTarget target) const
{
    LiveModState s;
    s.phase = lastLivePhase[(size_t)filterIndex][(size_t)target].load();
    s.value = lastLiveValue[(size_t)filterIndex][(size_t)target].load();
    return s;
}

void FlowEQAudioProcessor::resetAll()
{
    for (int i = 0; i < kNumFilters; ++i)
    {
        getFreqParam(i)->setValueNotifyingHost(getFreqParam(i)->convertTo0to1(kDefaultFreqs[i]));
        getGainParam(i)->setValueNotifyingHost(getGainParam(i)->convertTo0to1(0.0f));
        getQParam(i)->setValueNotifyingHost(getQParam(i)->convertTo0to1(0.707f));

        for (auto t : { CurveTarget::Frequency, CurveTarget::Gain, CurveTarget::Q })
        {
            getTimeParam(i, t)->setValueNotifyingHost(getTimeParam(i, t)->convertTo0to1(2.0f));
            getSyncParam(i, t)->setValueNotifyingHost(0.0f);
            getSyncDivParam(i, t)->setValueNotifyingHost(getSyncDivParam(i, t)->convertTo0to1(6));
        }

        curves[(size_t)i][0].reset();
        curves[(size_t)i][1].reset();
        curves[(size_t)i][2].reset();

        modEngines[(size_t)i][0].reset();
        modEngines[(size_t)i][1].reset();
        modEngines[(size_t)i][2].reset();

        for (int t = 0; t < 3; ++t)
        {
            useWavetable[(size_t)i][(size_t)t].store(false);
            wavetableFrame[(size_t)i][(size_t)t].store(0);
            { const juce::SpinLock::ScopedLockType sl(wavetableSwapLock); wavetables[(size_t)i][(size_t)t] = nullptr; }
            valueSmoothers[(size_t)i][(size_t)t].reset();
        }
    }
}

//==============================================================================
void FlowEQAudioProcessor::getFilterMagnitudeResponse(int i, const std::vector<double>& freqs,
    std::vector<double>& magsDb) const
{
    magsDb.resize(freqs.size());
    auto coeffs = filters[(size_t)i].state;
    for (size_t k = 0; k < freqs.size(); ++k)
        magsDb[k] = juce::Decibels::gainToDecibels(
            std::abs(coeffs->getMagnitudeForFrequency(freqs[k], sampleRate)));
}

void FlowEQAudioProcessor::getTotalMagnitudeResponse(const std::vector<double>& freqs,
    std::vector<double>& magsDb) const
{
    magsDb.assign(freqs.size(), 0.0);
    std::vector<double> tmp;
    for (int i = 0; i < kNumFilters; ++i)
    {
        getFilterMagnitudeResponse(i, freqs, tmp);
        for (size_t k = 0; k < freqs.size(); ++k)
            magsDb[k] += tmp[k];
    }
}

//==============================================================================
static juce::String curveToBase64(const FlowCurveData& c)
{
    juce::MemoryBlock mb(c.points.data(), sizeof(float) * kNumCurvePoints);
    return mb.toBase64Encoding();
}

static void base64ToCurve(const juce::String& s, FlowCurveData& c)
{
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding(s) && mb.getSize() == sizeof(float) * kNumCurvePoints)
        std::memcpy(c.points.data(), mb.getData(), mb.getSize());
    else
        c.reset();
}

void FlowEQAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();

    xml->setAttribute("selectedFilter", selectedFilter.load());

    auto* curvesXml = xml->createNewChildElement("CURVES");
    static const char* names[3] = { "freq", "gain", "q" };
    for (int i = 0; i < kNumFilters; ++i)
        for (int t = 0; t < 3; ++t)
            curvesXml->setAttribute("f" + juce::String(i) + "_" + names[t],
                curveToBase64(curves[(size_t)i][(size_t)t]));

    // Store wavetable references (path + frame + mode) per curve as well.
    // The sample data itself is NOT embedded, but re-read from the
    // original file when the state is loaded (keeps presets small).
    auto* wtXml = xml->createNewChildElement("WAVETABLES");
    for (int i = 0; i < kNumFilters; ++i)
        for (int t = 0; t < 3; ++t)
        {
            auto key = "f" + juce::String(i) + "_" + names[t];
            wtXml->setAttribute(key + "_use", useWavetable[(size_t)i][(size_t)t].load());
            wtXml->setAttribute(key + "_frame", wavetableFrame[(size_t)i][(size_t)t].load());
            wtXml->setAttribute(key + "_path", getWavetableName(i, (CurveTarget)t).isEmpty()
                ? juce::String()
                : [this, i, t] {
                    const juce::SpinLock::ScopedLockType sl(wavetableSwapLock);
                    auto wt = wavetables[(size_t)i][(size_t)t];
                    return wt != nullptr ? wt->fullPath : juce::String();
                }());
        }

    copyXmlToBinary(*xml, destData);
}

void FlowEQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr) return;

    auto state = juce::ValueTree::fromXml(*xml);
    if (state.isValid())
        apvts.replaceState(state);

    selectedFilter.store(xml->getIntAttribute("selectedFilter", 0));

    if (auto* curvesXml = xml->getChildByName("CURVES"))
    {
        static const char* names[3] = { "freq", "gain", "q" };
        for (int i = 0; i < kNumFilters; ++i)
            for (int t = 0; t < 3; ++t)
            {
                auto key = "f" + juce::String(i) + "_" + names[t];
                if (curvesXml->hasAttribute(key))
                    base64ToCurve(curvesXml->getStringAttribute(key), curves[(size_t)i][(size_t)t]);
            }
    }

    if (auto* wtXml = xml->getChildByName("WAVETABLES"))
    {
        static const char* names[3] = { "freq", "gain", "q" };
        for (int i = 0; i < kNumFilters; ++i)
            for (int t = 0; t < 3; ++t)
            {
                auto key = "f" + juce::String(i) + "_" + names[t];
                auto path = wtXml->getStringAttribute(key + "_path");
                bool use = wtXml->getBoolAttribute(key + "_use", false);
                int frame = wtXml->getIntAttribute(key + "_frame", 0);

                if (path.isNotEmpty())
                {
                    juce::File f(path);
                    if (f.existsAsFile() && loadWavetable(i, (CurveTarget)t, f))
                        setWavetableFrame(i, (CurveTarget)t, frame);
                }
                setUseWavetable(i, (CurveTarget)t, use && getWavetableNumFrames(i, (CurveTarget)t) > 0);
            }
    }
}

//==============================================================================
juce::AudioProcessorEditor* FlowEQAudioProcessor::createEditor()
{
    return new FlowEQAudioProcessorEditor(*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FlowEQAudioProcessor();
}