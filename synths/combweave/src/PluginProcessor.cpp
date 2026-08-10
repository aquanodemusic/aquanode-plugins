#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================
//  Constructor / Destructor
// ============================================================
CombWeaveAudioProcessor::CombWeaveAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (int i = 0; i <= kTableSize; ++i)
        sineTable[i] = std::sin(juce::MathConstants<float>::twoPi
            * (float)i / (float)kTableSize);
}

CombWeaveAudioProcessor::~CombWeaveAudioProcessor() {}

// ============================================================
//  Parameter Layout
// ============================================================
juce::AudioProcessorValueTreeState::ParameterLayout
CombWeaveAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ── Helper lambdas to avoid repetition ──────────────────────────────
    auto addOscParams = [&](const juce::String& sfx,
        float volumeDefault,
        float tuneDefault)
        {
            const bool is2 = (sfx == "2");

            layout.add(std::make_unique<juce::AudioParameterInt>(
                "amount" + sfx, "Harmonics" + sfx, 0, 128, 7));

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "hpFreq" + sfx, "HP Cutoff" + sfx,
                juce::NormalisableRange<float>(20.0f, 2000.0f, 0.1f, 0.3f), 20.0f));

            layout.add(std::make_unique<juce::AudioParameterChoice>(
                "spreadMode" + sfx, "Spread Mode" + sfx,
                juce::StringArray{ "Linear", "Exponential", "Harmonics" }, 0));

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "spreadHz" + sfx, "Spread (Hz)" + sfx,
                juce::NormalisableRange<float>(1.0f, 5000.0f, 0.1f, 0.4f), 110.0f));

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "spreadRatio" + sfx, "Spread (Ratio)" + sfx,
                juce::NormalisableRange<float>(1.001f, 4.0f, 0.0001f, 0.4f), 2.0f));

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "volume" + sfx, "Volume (dB)" + sfx,
                -60.0f, 12.0f, volumeDefault));

            layout.add(std::make_unique<juce::AudioParameterBool>(
                "bidirectional" + sfx, "Bidirectional" + sfx, false));

            layout.add(std::make_unique<juce::AudioParameterChoice>(
                "freqMode" + sfx, "Frequency Mode" + sfx,
                juce::StringArray{ "Regular", "Wrap", "Mirror" }, 0));

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "attack" + sfx, "Attack (ms)" + sfx,
                juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f, 0.3f), 10.0f));

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "release" + sfx, "Release (ms)" + sfx,
                juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f, 0.3f), 300.0f));

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "rolloff" + sfx, "Harmonic Rolloff" + sfx, 0.0f, 1.0f, 0.0f));

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "tune" + sfx, "Fine Tune (cents)" + sfx,
                -100.0f, 100.0f, tuneDefault));

            layout.add(std::make_unique<juce::AudioParameterBool>(
                "noteLock" + sfx, "Note Lock" + sfx, false));

            layout.add(std::make_unique<juce::AudioParameterChoice>(
                "harmonicFilter" + sfx, "Harmonic Filter" + sfx,
                juce::StringArray{ "All", "Evens Only", "Odds Only" }, 0));
        };

    addOscParams("", -12.0f, 0.0f);   // Osc 1: volume -12 dB, tune 0 cents
    addOscParams("2", -12.0f, 10.0f);   // Osc 2: volume -12 dB, tune +10 cents

    return layout;
}

// ============================================================
//  loadOscParams  — fills an OscParams from APVTS atomics
// ============================================================
CombWeaveAudioProcessor::OscParams
CombWeaveAudioProcessor::loadOscParams(int oscIndex) const noexcept
{
    const juce::String s = (oscIndex == 0) ? "" : "2";

    OscParams p;
    p.amount = (int)apvts.getRawParameterValue("amount" + s)->load();
    p.hpFreq = apvts.getRawParameterValue("hpFreq" + s)->load();
    p.spreadMode = (int)apvts.getRawParameterValue("spreadMode" + s)->load();
    p.spread = (p.spreadMode == 1)
        ? apvts.getRawParameterValue("spreadRatio" + s)->load()
        : apvts.getRawParameterValue("spreadHz" + s)->load();
    p.volumeLinear = juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue("volume" + s)->load());
    p.bidirectional = *apvts.getRawParameterValue("bidirectional" + s) > 0.5f;
    p.freqMode = (int)apvts.getRawParameterValue("freqMode" + s)->load();
    p.attackMs = apvts.getRawParameterValue("attack" + s)->load();
    p.releaseMs = apvts.getRawParameterValue("release" + s)->load();
    p.rolloff = apvts.getRawParameterValue("rolloff" + s)->load();
    p.tuneCents = apvts.getRawParameterValue("tune" + s)->load();
    p.noteLock = *apvts.getRawParameterValue("noteLock" + s) > 0.5f;
    p.harmonicFilter = (int)apvts.getRawParameterValue("harmonicFilter" + s)->load();
    return p;
}

// ============================================================
//  Prepare / Release
// ============================================================
void CombWeaveAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 1 };
    hpL.prepare(spec);   hpR.prepare(spec);
    hpL2.prepare(spec);  hpR2.prepare(spec);

    lastHpFreq = -1.0f;
    lastHpFreq2 = -1.0f;

    const float hf1 = apvts.getRawParameterValue("hpFreq")->load();
    const float hf2 = apvts.getRawParameterValue("hpFreq2")->load();
    rebuildHpFilter(hf1);
    rebuildHpFilter2(hf2);

    scratchBuffer.setSize(2, samplesPerBlock);
    scratchBuffer.clear();

    for (auto& v : voices)  v.kill();
    for (auto& v : voices2) v.kill();
}

void CombWeaveAudioProcessor::releaseResources() {}

bool CombWeaveAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled();
}

// ============================================================
//  Frequency helpers
// ============================================================
float CombWeaveAudioProcessor::wrapFreq(float f, float lo, float hi) const noexcept
{
    float range = hi - lo;
    float x = std::fmod(f - lo, range);
    if (x < 0.0f) x += range;
    return lo + x;
}

float CombWeaveAudioProcessor::mirrorFreq(float f, float lo, float hi) const noexcept
{
    float range = hi - lo;
    float period = 2.0f * range;
    float x = std::fmod(f - lo, period);
    if (x < 0.0f) x += period;
    if (x > range) x = period - x;
    return lo + x;
}

// ============================================================
//  Core harmonic computation — audio-thread-safe, no heap alloc
// ============================================================
int CombWeaveAudioProcessor::computeHarmonicFreqsFixed(const OscParams& p,
    float fundamental,
    float* outFreqs,
    float* outGains,
    int    maxSize) const noexcept
{
    fundamental *= std::pow(2.0f, p.tuneCents / 1200.0f);

    const float lo = 20.0f;
    const float hi = (float)(lastSampleRate * 0.475);

    auto snapToNote = [](float f) -> float
        {
            const float midi = 69.0f + 12.0f * (std::log(f / 440.0f) / std::log(2.0f));
            return 440.0f * std::pow(2.0f, (std::round(midi) - 69.0f) / 12.0f);
        };

    int count = 0;

    // harmIdx is 0-based partial index; partial number n = harmIdx + 1.
    auto tryAdd = [&](float f, int harmIdx)
        {
            if (count >= maxSize) return;
            if (!std::isfinite(f) || f < lo || f > hi) return;

            const int n = harmIdx + 1;  // 1-indexed partial number
            if (p.harmonicFilter == 1 && (n % 2 != 0)) return;  // Evens Only: skip odd partials
            if (p.harmonicFilter == 2 && (n % 2 == 0)) return;  // Odds Only:  skip even partials

            const float w = std::pow(1.0f - p.rolloff * 0.95f, (float)harmIdx);
            outFreqs[count] = f;
            outGains[count] = w;
            ++count;
        };

    // Fundamental (partial 1)
    tryAdd(p.noteLock ? snapToNote(fundamental) : fundamental, 0);

    // Upward harmonics
    for (int i = 1; i <= p.amount && count < maxSize; ++i)
    {
        float f;
        if (p.spreadMode == 0) f = fundamental + (float)i * p.spread;
        else if (p.spreadMode == 1) f = fundamental * std::pow(p.spread, (float)i);
        else                        f = fundamental * (float)(i + 1);   // harmonic series

        if (p.freqMode == 1) f = wrapFreq(f, lo, hi);
        else if (p.freqMode == 2) f = mirrorFreq(f, lo, hi);
        else if (f < lo || f > hi) continue;   // Regular: cull OOB

        if (p.noteLock) f = snapToNote(f);
        tryAdd(f, i);
    }

    // Downward harmonics (bidirectional)
    if (p.bidirectional)
    {
        for (int i = 1; i <= p.amount && count < maxSize; ++i)
        {
            float f;
            if (p.spreadMode == 0) f = fundamental - (float)i * p.spread;
            else if (p.spreadMode == 1) f = fundamental / std::pow(p.spread, (float)i);
            else                        f = fundamental / (float)(i + 1);   // sub-harmonic series

            if (p.freqMode == 1) f = wrapFreq(f, lo, hi);
            else if (p.freqMode == 2) f = mirrorFreq(f, lo, hi);
            else if (f < lo || f > hi) continue;

            if (p.noteLock) f = snapToNote(f);
            tryAdd(f, i);
        }
    }

    return count;
}

// Message-thread version — returns a vector (heap alloc fine on message thread)
std::vector<float> CombWeaveAudioProcessor::computeHarmonicFreqsVec(float fundamental,
    int   oscIndex) const
{
    float tmpF[kMaxOscs], tmpG[kMaxOscs];
    const OscParams p = loadOscParams(oscIndex);
    const int n = computeHarmonicFreqsFixed(p, fundamental, tmpF, tmpG, kMaxOscs);
    return std::vector<float>(tmpF, tmpF + n);
}

// ============================================================
//  HP filter rebuild
// ============================================================
void CombWeaveAudioProcessor::rebuildHpFilter(float hpFreq)
{
    lastHpFreq = hpFreq;
    auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass(lastSampleRate, hpFreq, 0.707f);
    hpL.coefficients = c;
    hpR.coefficients = c;
}

void CombWeaveAudioProcessor::rebuildHpFilter2(float hpFreq)
{
    lastHpFreq2 = hpFreq;
    auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass(lastSampleRate, hpFreq, 0.707f);
    hpL2.coefficients = c;
    hpR2.coefficients = c;
}

// ============================================================
//  Voice oscillator setup
// ============================================================
void CombWeaveAudioProcessor::updateVoice(Voice& v, const OscParams& p) noexcept
{
    if (!v.active || v.midiNote < 0) { v.numOscs = 0; return; }

    const float fundamental = 440.0f * std::pow(2.0f, (float)(v.midiNote - 69) / 12.0f);

    float freqs[kMaxOscs], gains[kMaxOscs];
    v.numOscs = computeHarmonicFreqsFixed(p, fundamental, freqs, gains, kMaxOscs);

    // Normalise gains to unit RMS
    float energy = 0.0f;
    for (int i = 0; i < v.numOscs; ++i) energy += gains[i] * gains[i];
    const float norm = (energy > 1e-9f) ? 1.0f / std::sqrt(energy) : 0.0f;

    for (int i = 0; i < v.numOscs; ++i)
    {
        v.oscGains[i] = gains[i] * norm;
        v.oscs[i].setFreq(freqs[i], lastSampleRate);
    }
}

// ============================================================
//  MIDI event handlers (lockstep: same voice slot for both oscs)
// ============================================================
void CombWeaveAudioProcessor::handleNoteOn(int note, float vel,
    const OscParams& p1,
    const OscParams& p2) noexcept
{
    // 1. Find a free slot
    int targetIdx = -1;
    for (int i = 0; i < kNumVoices; ++i)
        if (!voices[i].active) { targetIdx = i; break; }

    // 2. Voice steal: oldest active voice
    if (targetIdx < 0)
    {
        int oldest = INT_MAX;
        for (int i = 0; i < kNumVoices; ++i)
            if (voices[i].age < oldest) { oldest = voices[i].age; targetIdx = i; }
    }
    if (targetIdx < 0) return;

    // Reset oscillator phases for both oscillators at the chosen slot
    for (auto& o : voices[targetIdx].oscs) o.reset();
    for (auto& o : voices2[targetIdx].oscs) o.reset();

    voices[targetIdx].noteOn(note, vel, lastSampleRate, p1.attackMs, p1.releaseMs);
    voices2[targetIdx].noteOn(note, vel, lastSampleRate, p2.attackMs, p2.releaseMs);
    voices[targetIdx].age = ++ageCounter;
    voices2[targetIdx].age = voices[targetIdx].age;

    updateVoice(voices[targetIdx], p1);
    updateVoice(voices2[targetIdx], p2);

    displayFundamental.store(440.0f * std::pow(2.0f, (float)(note - 69) / 12.0f));
}

void CombWeaveAudioProcessor::handleNoteOff(int note,
    const OscParams& p1,
    const OscParams& p2) noexcept
{
    for (int i = 0; i < kNumVoices; ++i)
    {
        if (voices[i].active && !voices[i].releasing && voices[i].midiNote == note)
        {
            voices[i].noteOff(lastSampleRate, p1.releaseMs);
            if (voices2[i].active && !voices2[i].releasing)
                voices2[i].noteOff(lastSampleRate, p2.releaseMs);
        }
    }
}

// ============================================================
//  processBlock
// ============================================================
void CombWeaveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    // ── Load both oscillator snapshots once (no repeated string lookups) ──
    const OscParams p1 = loadOscParams(0);
    const OscParams p2 = loadOscParams(1);

    // ── Rebuild HP filters if cutoff changed ──────────────────────────────
    if (std::abs(p1.hpFreq - lastHpFreq) > 0.5f) rebuildHpFilter(p1.hpFreq);
    if (std::abs(p2.hpFreq - lastHpFreq2) > 0.5f) rebuildHpFilter2(p2.hpFreq);

    // ── Update oscillator frequencies for all already-active voices ───────
    for (auto& v : voices)  if (v.active) updateVoice(v, p1);
    for (auto& v : voices2) if (v.active) updateVoice(v, p2);

    // ── Prepare scratch buffer for osc2 ───────────────────────────────────
    if (scratchBuffer.getNumSamples() < numSamples)
        scratchBuffer.setSize(2, numSamples, false, false, true);
    scratchBuffer.clear();

    auto* outL = buffer.getWritePointer(0);
    auto* outR = buffer.getWritePointer(1);
    auto* scrL = scratchBuffer.getWritePointer(0);
    auto* scrR = scratchBuffer.getWritePointer(1);

    auto midiIt = midi.cbegin();

    // ── Sample-accurate synthesis loop ────────────────────────────────────
    for (int s = 0; s < numSamples; ++s)
    {
        // Dispatch MIDI events at this exact sample
        while (midiIt != midi.cend() && (*midiIt).samplePosition <= s)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())  handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity(), p1, p2);
            else if (msg.isNoteOff()) handleNoteOff(msg.getNoteNumber(), p1, p2);
            ++midiIt;
        }

        float samp1 = 0.0f, samp2 = 0.0f;

        for (auto& v : voices)
        {
            if (!v.active) continue;
            const float env = v.tickEnv();
            if (env < 1e-7f) continue;

            float vs = 0.0f;
            for (int i = 0; i < v.numOscs; ++i)
                vs += v.oscs[i].tick(sineTable) * v.oscGains[i];

            samp1 += vs * env * v.velocity;
        }

        for (auto& v : voices2)
        {
            if (!v.active) continue;
            const float env = v.tickEnv();
            if (env < 1e-7f) continue;

            float vs = 0.0f;
            for (int i = 0; i < v.numOscs; ++i)
                vs += v.oscs[i].tick(sineTable) * v.oscGains[i];

            samp2 += vs * env * v.velocity;
        }

        outL[s] = samp1 * p1.volumeLinear;
        outR[s] = samp1 * p1.volumeLinear;
        scrL[s] = samp2 * p2.volumeLinear;
        scrR[s] = samp2 * p2.volumeLinear;
    }

    // ── Apply independent HP filters ──────────────────────────────────────
    {
        juce::dsp::AudioBlock<float> blk1(buffer);
        { auto ch = blk1.getSingleChannelBlock(0); juce::dsp::ProcessContextReplacing<float> ctx(ch); hpL.process(ctx); }
        { auto ch = blk1.getSingleChannelBlock(1); juce::dsp::ProcessContextReplacing<float> ctx(ch); hpR.process(ctx); }
    }
    {
        juce::dsp::AudioBlock<float> blk2(scratchBuffer);
        { auto ch = blk2.getSingleChannelBlock(0); juce::dsp::ProcessContextReplacing<float> ctx(ch); hpL2.process(ctx); }
        { auto ch = blk2.getSingleChannelBlock(1); juce::dsp::ProcessContextReplacing<float> ctx(ch); hpR2.process(ctx); }
    }

    // ── Mix osc2 into output and soft-clip ────────────────────────────────
    for (int s = 0; s < numSamples; ++s)
    {
        outL[s] = juce::jlimit(-1.0f, 1.0f, outL[s] + scrL[s]);
        outR[s] = juce::jlimit(-1.0f, 1.0f, outR[s] + scrR[s]);
    }
}

// ============================================================
//  Boilerplate
// ============================================================
bool CombWeaveAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* CombWeaveAudioProcessor::createEditor()
{
    return new CombWeaveAudioProcessorEditor(*this);
}

const juce::String CombWeaveAudioProcessor::getName()   const { return "CombWeave"; }
bool CombWeaveAudioProcessor::acceptsMidi()  const { return true; }
bool CombWeaveAudioProcessor::producesMidi() const { return false; }
bool CombWeaveAudioProcessor::isMidiEffect() const { return false; }
double CombWeaveAudioProcessor::getTailLengthSeconds() const { return 3.0; }

int  CombWeaveAudioProcessor::getNumPrograms() { return 1; }
int  CombWeaveAudioProcessor::getCurrentProgram() { return 0; }
void CombWeaveAudioProcessor::setCurrentProgram(int) {}
const juce::String CombWeaveAudioProcessor::getProgramName(int) { return {}; }
void CombWeaveAudioProcessor::changeProgramName(int, const juce::String&) {}

void CombWeaveAudioProcessor::getStateInformation(juce::MemoryBlock& d)
{
    copyXmlToBinary(*apvts.copyState().createXml(), d);
}
void CombWeaveAudioProcessor::setStateInformation(const void* d, int s)
{
    auto xml = getXmlFromBinary(d, s);
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CombWeaveAudioProcessor();
}