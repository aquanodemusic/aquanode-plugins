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
    // Build sine wavetable – extra entry [kTableSize] = sin(2π) = 0 for lerp wrap
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

    // Row 1
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "amount", "Harmonics", 0, 128, 7));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "hpFreq", "HP Cutoff",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 0.1f, 0.3f), 20.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "spreadMode", "Spread Mode", false));   // false = linear, true = exponential

    // Row 2
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "spreadHz", "Spread (Hz)",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 0.1f, 0.4f), 110.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "spreadRatio", "Spread (Ratio)",
        juce::NormalisableRange<float>(1.001f, 4.0f, 0.0001f, 0.4f), 2.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "volume", "Volume (dB)", -60.0f, 12.0f, -6.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "bidirectional", "Bidirectional", false));

    // Row 3
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "freqMode", "Frequency Mode",
        juce::StringArray{ "Regular", "Wrap", "Mirror" }, 0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack (ms)",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f, 0.3f), 10.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release (ms)",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f, 0.3f), 300.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "rolloff", "Harmonic Rolloff", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "tune", "Fine Tune (cents)", -100.0f, 100.0f, 0.0f));

    return layout;
}

// ============================================================
//  Prepare / Release
// ============================================================
void CombWeaveAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 1 };
    hpL.prepare(spec);
    hpR.prepare(spec);

    lastHpFreq = -1.0f;     // force rebuild
    rebuildHpFilter();

    for (auto& v : voices) v.kill();
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
//  Harmonic frequency computation – audio-thread-safe (no heap alloc)
// ============================================================
int CombWeaveAudioProcessor::computeHarmonicFreqsFixed(float fundamental,
    float* outFreqs,
    float* outGains,
    int    maxSize) const noexcept
{
    const int   amount = (int)apvts.getRawParameterValue("amount")->load();
    const bool  bidir = *apvts.getRawParameterValue("bidirectional") > 0.5f;
    const bool  expMode = *apvts.getRawParameterValue("spreadMode") > 0.5f;
    const int   freqMode = (int)apvts.getRawParameterValue("freqMode")->load();
    const float tuneCts = apvts.getRawParameterValue("tune")->load();
    const float rolloff = apvts.getRawParameterValue("rolloff")->load();
    const float spread = expMode ? apvts.getRawParameterValue("spreadRatio")->load()
        : apvts.getRawParameterValue("spreadHz")->load();

    fundamental *= std::pow(2.0f, tuneCts / 1200.0f);

    const float lo = 20.0f;
    const float hi = (float)(lastSampleRate * 0.475);

    int count = 0;

    auto tryAdd = [&](float f, int harmIdx)
        {
            if (count >= maxSize) return;
            if (!std::isfinite(f) || f < lo || f > hi) return;
            const float w = std::pow(1.0f - rolloff * 0.95f, (float)harmIdx);
            outFreqs[count] = f;
            outGains[count] = w;
            ++count;
        };

    // Fundamental
    tryAdd(fundamental, 0);

    // Upward harmonics
    for (int i = 1; i <= amount && count < maxSize; ++i)
    {
        float f = expMode ? fundamental * std::pow(spread, (float)i)
            : fundamental + (float)i * spread;

        if (freqMode == 1) f = wrapFreq(f, lo, hi);
        else if (freqMode == 2) f = mirrorFreq(f, lo, hi);
        else if (f < lo || f > hi) continue;   // Regular: cull OOB

        tryAdd(f, i);
    }

    // Downward harmonics (bidirectional)
    if (bidir)
    {
        for (int i = 1; i <= amount && count < maxSize; ++i)
        {
            float f = expMode ? fundamental / std::pow(spread, (float)i)
                : fundamental - (float)i * spread;

            if (freqMode == 1) f = wrapFreq(f, lo, hi);
            else if (freqMode == 2) f = mirrorFreq(f, lo, hi);
            else if (f < lo || f > hi) continue;

            tryAdd(f, i);
        }
    }

    return count;
}

// Message-thread version – returns a vector (fine on GUI thread)
std::vector<float> CombWeaveAudioProcessor::computeHarmonicFreqsVec(float fundamental) const
{
    static float tmpF[kMaxOscs], tmpG[kMaxOscs];
    int n = computeHarmonicFreqsFixed(fundamental, tmpF, tmpG, kMaxOscs);
    return std::vector<float>(tmpF, tmpF + n);
}

// ============================================================
//  HP filter rebuild
// ============================================================
void CombWeaveAudioProcessor::rebuildHpFilter()
{
    float hpFreq = apvts.getRawParameterValue("hpFreq")->load();
    lastHpFreq = hpFreq;
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(lastSampleRate, hpFreq, 0.707f);
    hpL.coefficients = coeffs;
    hpR.coefficients = coeffs;
}

// ============================================================
//  Voice oscillator setup (called every block for active voices)
//  No heap allocation – uses computeHarmonicFreqsFixed.
// ============================================================
void CombWeaveAudioProcessor::updateVoice(Voice& v) noexcept
{
    if (!v.active || v.midiNote < 0) { v.numOscs = 0; return; }

    const float fundamental = 440.0f * std::pow(2.0f, (float)(v.midiNote - 69) / 12.0f);

    float freqs[kMaxOscs], gains[kMaxOscs];
    v.numOscs = computeHarmonicFreqsFixed(fundamental, freqs, gains, kMaxOscs);

    // Normalise gains to unit RMS so loudness stays constant as harmonics change
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
//  MIDI event handlers
// ============================================================
void CombWeaveAudioProcessor::handleNoteOn(int note, float vel) noexcept
{
    const float atkMs = apvts.getRawParameterValue("attack")->load();
    const float relMs = apvts.getRawParameterValue("release")->load();

    // 1. Find a free voice
    Voice* target = nullptr;
    for (auto& v : voices)
        if (!v.active) { target = &v; break; }

    // 2. Voice steal: take the oldest active voice
    if (!target)
    {
        int oldest = INT_MAX;
        for (auto& v : voices)
            if (v.age < oldest) { oldest = v.age; target = &v; }
    }
    if (!target) return;

    // Reset oscillator phases for clean attack
    for (auto& o : target->oscs) o.reset();

    target->noteOn(note, vel, lastSampleRate, atkMs, relMs);
    target->age = ++ageCounter;
    updateVoice(*target);

    displayFundamental.store(440.0f * std::pow(2.0f, (float)(note - 69) / 12.0f));
}

void CombWeaveAudioProcessor::handleNoteOff(int note) noexcept
{
    const float relMs = apvts.getRawParameterValue("release")->load();
    for (auto& v : voices)
        if (v.active && !v.releasing && v.midiNote == note)
            v.noteOff(lastSampleRate, relMs);
}

// ============================================================
//  processBlock
// ============================================================
void CombWeaveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int    numSamples = buffer.getNumSamples();
    const float  gainLin = juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue("volume")->load());

    // Rebuild HP filter if cutoff changed
    {
        float hp = apvts.getRawParameterValue("hpFreq")->load();
        if (std::abs(hp - lastHpFreq) > 0.5f) rebuildHpFilter();
    }

    // Update oscillator frequencies for all active voices (cheap: no heap alloc)
    for (auto& v : voices)
        if (v.active) updateVoice(v);

    // ---- Synthesis loop (sample-accurate MIDI) ----
    auto* outL = buffer.getWritePointer(0);
    auto* outR = buffer.getWritePointer(1);
    auto  midiIt = midi.cbegin();

    for (int s = 0; s < numSamples; ++s)
    {
        // Dispatch MIDI events at this exact sample
        while (midiIt != midi.cend() && (*midiIt).samplePosition <= s)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())  handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff()) handleNoteOff(msg.getNoteNumber());
            ++midiIt;
        }

        float samp = 0.0f;

        for (auto& v : voices)
        {
            if (!v.active) continue;
            const float env = v.tickEnv();
            if (env < 1e-7f) continue;

            float vs = 0.0f;
            for (int i = 0; i < v.numOscs; ++i)
                vs += v.oscs[i].tick(sineTable) * v.oscGains[i];

            samp += vs * env * v.velocity;
        }

        samp *= gainLin;
        samp = juce::jlimit(-1.0f, 1.0f, samp);   // soft clip safety

        outL[s] = samp;
        outR[s] = samp;
    }

    // Apply HP filter over the filled buffer (block-based; more efficient)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        {
            auto ch = block.getSingleChannelBlock(0);
            juce::dsp::ProcessContextReplacing<float> ctx(ch);
            hpL.process(ctx);
        }
        {
            auto ch = block.getSingleChannelBlock(1);
            juce::dsp::ProcessContextReplacing<float> ctx(ch);
            hpR.process(ctx);
        }
    }
}

// ============================================================
//  Boilerplate
// ============================================================
bool CombWeaveAudioProcessor::hasEditor()  const { return true; }
juce::AudioProcessorEditor* CombWeaveAudioProcessor::createEditor()
{
    return new CombWeaveAudioProcessorEditor(*this);
}

const juce::String CombWeaveAudioProcessor::getName()    const { return "CombWeave"; }
bool CombWeaveAudioProcessor::acceptsMidi()   const { return true; }
bool CombWeaveAudioProcessor::producesMidi()  const { return false; }
bool CombWeaveAudioProcessor::isMidiEffect()  const { return false; }
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