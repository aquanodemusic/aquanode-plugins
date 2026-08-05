#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr float kBeatMult[] =
    { 4.0f, 3.0f, 2.0f, 1.0f, 0.5f, 2.0f/3.0f, 1.0f/3.0f };

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ADelaySRProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "delayTimeMs", 1 }, "Delay Time",
        NormalisableRange<float> (1.0f, 5000.0f, 0.1f, 0.35f), 500.0f,
        AudioParameterFloatAttributes{}.withLabel ("ms")));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "timeSyncEnabled", 1 }, "Time Sync", false));

    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "syncDivision", 1 }, "Sync Division",
        StringArray { "1/1","3/4","1/2","1/4","1/8","1/4T","1/8T" }, 3));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "attack", 1 }, "Attack",
        NormalisableRange<float> (0.0f, 50.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes{}.withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Release",
        NormalisableRange<float> (0.0f, 50.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes{}.withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "tapVolume", 1 }, "Tap Volume",
        NormalisableRange<float> (0.0f, 120.0f, 0.1f), 50.0f,
        AudioParameterFloatAttributes{}.withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "tapVolBoost", 1 }, "Tap Vol Boost", false));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "noteDelayEnabled", 1 }, "Note Delay", false));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "noteDelayTimeMs", 1 }, "Note Delay Time",
        NormalisableRange<float> (0.00001f, 1.0f, 0.00001f), 0.5f,
        AudioParameterFloatAttributes{}.withLabel ("ms")));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "triggerEnabled", 1 }, "Trigger", true));

    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "delayMode", 1 }, "Delay Mode",
        StringArray { "Mono", "Stereo", "Ping Pong", "Hard PP" }, 1));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "wetOnly", 1 }, "Wet Only", false));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "delaySmoothMs", 1 }, "Delay Smooth",
        NormalisableRange<float> (0.0f, 1000.0f, 1.0f), 50.0f,
        AudioParameterFloatAttributes{}.withLabel ("ms")));

    return { p.begin(), p.end() };
}

//==============================================================================
ADelaySRProcessor::ADelaySRProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ADelaySRState", createParameterLayout())
{}

ADelaySRProcessor::~ADelaySRProcessor() {}

//==============================================================================
void ADelaySRProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    delayBuffer.setSize (2, kMaxDelaySamples);
    delayBuffer.clear();
    writePos     = 0;
    cyclePhase   = 0.0;
    inputEnv     = 0.0f;
    wasTriggered = false;
    hardPPTap    = 0;
    isRecording.store (false);
    recordPos.store (0);
    atkCoeff = std::exp (-1.0f / (0.001f * (float) sampleRate));
    relCoeff = std::exp (-1.0f / (0.100f * (float) sampleRate));
    const float smoothMs = apvts.getRawParameterValue ("delaySmoothMs")->load();
    lastSmoothingMs = smoothMs;
    smoothDelay.reset (sampleRate, juce::jmax (0.0f, smoothMs) * 0.001f);
    smoothDelay.setCurrentAndTargetValue (targetDelayTimeSamples());
}

void ADelaySRProcessor::releaseResources() {}

bool ADelaySRProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() &&
        out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

//==============================================================================
float ADelaySRProcessor::targetDelayTimeSamples() const
{
    if (apvts.getRawParameterValue ("noteDelayEnabled")->load() > 0.5f)
    {
        const float ms = apvts.getRawParameterValue ("noteDelayTimeMs")->load();
        return juce::jmax (1.0f, ms * 0.001f * (float) currentSampleRate);
    }

    const bool synced = apvts.getRawParameterValue ("timeSyncEnabled")->load() > 0.5f;
    float timeMs;

    if (synced)
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    bpm = juce::jmax (1.0, *b);

        const int idx = juce::jlimit (0, 6,
            (int) apvts.getRawParameterValue ("syncDivision")->load());
        timeMs = kBeatMult[idx] * 60000.0f / (float) bpm;
    }
    else
    {
        timeMs = apvts.getRawParameterValue ("delayTimeMs")->load();
    }

    return juce::jlimit (1.0f, (float) kMaxDelaySamples,
                         timeMs * 0.001f * (float) currentSampleRate);
}

//==============================================================================
float ADelaySRProcessor::tapEnvelope (float phase, float atkFrac, float relFrac) noexcept
{
    if (atkFrac > 0.0f && phase < atkFrac)        return phase / atkFrac;
    if (relFrac > 0.0f && phase >= 1.0f - relFrac) return (1.0f - phase) / relFrac;
    return 1.0f;
}

//==============================================================================
void ADelaySRProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh  = juce::jmin (buffer.getNumChannels(), 2);
    const int numSmp = buffer.getNumSamples();

    // ── Clear delay buffer if requested from UI thread ─────────────────────
    if (clearBufferRequested.exchange (false, std::memory_order_acq_rel))
    {
        delayBuffer.clear();
        writePos   = 0;
        cyclePhase = 0.0;
        hardPPTap  = 0;
        smoothDelay.setCurrentAndTargetValue (targetDelayTimeSamples());
    }

    // ── Update smoother ramp length if delaySmoothMs parameter changed ─────
    {
        const float smoothMs = apvts.getRawParameterValue ("delaySmoothMs")->load();
        if (smoothMs != lastSmoothingMs)
        {
            lastSmoothingMs = smoothMs;
            smoothDelay.reset (currentSampleRate, juce::jmax (0.0f, smoothMs) * 0.001f);
        }
    }

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSmp);

    // Snap smoother on mode-switch (large jump ratio)
    const float newTarget = targetDelayTimeSamples();
    const float cur       = smoothDelay.getCurrentValue();
    const float ratio     = (cur > 0.0f) ? (newTarget > cur ? newTarget/cur : cur/newTarget) : 1.0f;
    if (ratio > 10.0f) smoothDelay.setCurrentAndTargetValue (newTarget);
    else               smoothDelay.setTargetValue (newTarget);

    const float atkFrac = apvts.getRawParameterValue ("attack")->load()         / 100.0f;
    const float relFrac = apvts.getRawParameterValue ("release")->load()        / 100.0f;
    const float tapVol  = apvts.getRawParameterValue ("tapVolume")->load()      / 100.0f;
    const bool  trigEn  = apvts.getRawParameterValue ("triggerEnabled")->load() > 0.5f;
    const int   modeIdx = (int) apvts.getRawParameterValue ("delayMode")->load();

    const float* rL = delayBuffer.getReadPointer  (0);
    const float* rR = (numCh > 1) ? delayBuffer.getReadPointer  (1) : rL;
          float* wL = delayBuffer.getWritePointer (0);
          float* wR = (numCh > 1) ? delayBuffer.getWritePointer (1) : wL;

    for (int i = 0; i < numSmp; ++i)
    {
        const float inL = buffer.getSample (0, i);
        const float inR = (numCh > 1) ? buffer.getSample (1, i) : inL;

        // Input-onset envelope follower
        const float inPeak = std::max (std::abs (inL), std::abs (inR));
        inputEnv = (inPeak > inputEnv) ? inputEnv * atkCoeff + inPeak * (1.0f - atkCoeff)
                                       : inputEnv * relCoeff;

        const bool nowActive = inputEnv > 0.001f;
        if (trigEn && nowActive && !wasTriggered)
        {
            cyclePhase = 0.0;
            hardPPTap  = 0;
        }
        wasTriggered = nowActive;

        // Read position (fractional, linear interp)
        const float dt = juce::jmax (1.0f, smoothDelay.getNextValue());
        float readF = (float) writePos - dt;
        if (readF < 0.0f) readF += (float) kMaxDelaySamples;

        const float floorF = std::floor (readF);
        const int   rp0    = (int) floorF % kMaxDelaySamples;
        const int   rp1    = (rp0 + 1) % kMaxDelaySamples;
        const float frac   = readF - floorF;

        const float delL = rL[rp0] * (1.0f - frac) + rL[rp1] * frac;
        const float delR = rR[rp0] * (1.0f - frac) + rR[rp1] * frac;

        const float env = tapEnvelope ((float) cyclePhase, atkFrac, relFrac);

        const bool  wetOnly = apvts.getRawParameterValue ("wetOnly")->load() > 0.5f;
        float wetL, wetR, fbL, fbR;
        switch (modeIdx)
        {
            case 0: // Mono
            {
                const float inM  = (inL + inR) * 0.5f;
                const float delM = (delL + delR) * 0.5f;
                wetL = delM * env;  wetR = delM * env;
                fbL  = inM + delM * tapVol;  fbR = fbL;
                break;
            }
            case 2: // Ping Pong
                wetL = delL * env;  wetR = delR * env;
                fbL  = inL + delR * tapVol;
                fbR  = inR + delL * tapVol;
                break;
            case 3: // Hard Ping Pong – each tap is 100% L or 100% R, alternating
            {
                const bool  tapLeft = (hardPPTap & 1) == 0;
                const float delMix  = (delL + delR) * 0.5f;
                const float inMix   = (inL  + inR)  * 0.5f;
                wetL = tapLeft  ? delMix * env : 0.0f;
                wetR = !tapLeft ? delMix * env : 0.0f;
                // Write to BOTH channels so delMix is always full-level on the next read.
                // Writing 0.0f to one channel was silently halving the feedback each cycle.
                const float fb = inMix + delMix * tapVol;
                fbL = fb;
                fbR = fb;
                break;
            }
            default: // Stereo
                wetL = delL * env;  wetR = delR * env;
                fbL  = inL + delL * tapVol;
                fbR  = inR + delR * tapVol;
                break;
        }

        const float outL = wetOnly ? wetL : inL + wetL;
        const float outR = wetOnly ? wetR : inR + wetR;

        buffer.setSample (0, i, outL);
        if (numCh > 1) buffer.setSample (1, i, outR);

        wL[writePos] = fbL;
        if (numCh > 1 && wR != wL) wR[writePos] = fbR;

        writePos = (writePos + 1) % kMaxDelaySamples;
        cyclePhase += 1.0 / (double) dt;
        if (cyclePhase >= 1.0) { cyclePhase -= 1.0; ++hardPPTap; }
    }  // end sample loop

    // ── Recording capture (post-loop, entire block at once) ────────────────
    if (isRecording.load (std::memory_order_acquire))
    {
        const int pos    = recordPos.load (std::memory_order_relaxed);
        const int remain = recordMaxSamples - pos;
        const int toCopy = juce::jmin (numSmp, remain);
        if (toCopy > 0)
        {
            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
                recordBuffer.copyFrom (ch, pos, buffer, ch, 0, toCopy);
            recordPos.fetch_add (toCopy, std::memory_order_release);
        }
        if (remain <= numSmp)
            isRecording.store (false, std::memory_order_release);   // buffer full → auto-stop
    }
}

//==============================================================================
void ADelaySRProcessor::startRecording()
{
    // Allocate up to 2 minutes at the current sample rate, stereo
    recordMaxSamples = (int) (currentSampleRate * 120.0);
    recordBuffer.setSize (2, recordMaxSamples, false, true, false);
    recordPos.store (0, std::memory_order_relaxed);
    isRecording.store (true, std::memory_order_release);
}

void ADelaySRProcessor::stopRecording()
{
    isRecording.store (false, std::memory_order_release);
}

void ADelaySRProcessor::clearDelayBuffer()
{
    clearBufferRequested.store (true, std::memory_order_release);
}

//==============================================================================
juce::AudioProcessorEditor* ADelaySRProcessor::createEditor()
{
    return new ADelaySREditor (*this);
}

void ADelaySRProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ADelaySRProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ADelaySRProcessor();
}
