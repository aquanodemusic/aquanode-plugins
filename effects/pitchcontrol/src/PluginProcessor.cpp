/*
  ==============================================================================
    PitchControl – IIR Bell Filter + FFT Spectral Shift Plugin
    Processor Implementation
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//==============================================================================
Biquad makePeakingEQ(double fc, double sampleRate, double gainDB, double Q) noexcept
{
    double nyquist = sampleRate * 0.5;
    fc = juce::jlimit(1.0, nyquist - 1.0, fc);

    double A = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * fc / sampleRate;
    double cosw0 = std::cos(w0);
    double sinw0 = std::sin(w0);
    double alpha = sinw0 / (2.0 * Q);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * cosw0;
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha / A;

    Biquad bq;
    bq.b0 = b0 / a0;  bq.b1 = b1 / a0;  bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;  bq.a2 = a2 / a0;
    return bq;
}

static inline double midiNoteToHz(double note) noexcept
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

static inline double hzToMidi(double hz) noexcept
{
    if (hz <= 0.0) return -999.0;
    return 69.0 + 12.0 * std::log2(hz / 440.0);
}

//==============================================================================
juce::String PitchControlAudioProcessor::noteActiveParamID(int i)
{
    return "note_active_" + juce::String(i);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
PitchControlAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 0; i < kNumNotes; ++i)
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            noteActiveParamID(i), kNoteNames[i] + " protected", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        depthParamID(), "Depth (dB)",
        juce::NormalisableRange<float>(-64.0f, 0.0f, 0.1f), -12.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        qParamID(), "Q",
        juce::NormalisableRange<float>(1.0f, 100.0f, 0.1f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        rangeFromParamID(), "Range From", 0, kTotalNotes - 1, 0));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        rangeToParamID(), "Range To", 0, kTotalNotes - 1, kTotalNotes - 1));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        wetOnlyParamID(), "Wet Only", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        boostDBParamID(), "Boost (dB)",
        juce::NormalisableRange<float>(0.0f, 6.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        boostQParamID(), "Boost Q",
        juce::NormalisableRange<float>(10.0f, 500.0f, 0.1f), 30.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        fftModeParamID(), "FFT Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        fftMixParamID(), "FFT Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        shiftStrengthParamID(), "Shift Strength",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        dampenUnprotectedParamID(), "Dampen Unprotected", true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        electrifyParamID(), "Electrify",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        pullParamID(), "Pull",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        outputGainParamID(), "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
PitchControlAudioProcessor::PitchControlAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#else
    :
#endif
apvts(*this, nullptr, "PitchControl", createParameterLayout()),
m_paramListener(*this)
{
    for (int i = 0; i < kNumNotes; ++i)
        apvts.addParameterListener(noteActiveParamID(i), &m_paramListener);
    apvts.addParameterListener(depthParamID(), &m_paramListener);
    apvts.addParameterListener(qParamID(), &m_paramListener);
    apvts.addParameterListener(rangeFromParamID(), &m_paramListener);
    apvts.addParameterListener(rangeToParamID(), &m_paramListener);
    apvts.addParameterListener(wetOnlyParamID(), &m_paramListener);
    apvts.addParameterListener(boostDBParamID(), &m_paramListener);
    apvts.addParameterListener(boostQParamID(), &m_paramListener);
    apvts.addParameterListener(fftModeParamID(), &m_paramListener);
    apvts.addParameterListener(fftMixParamID(), &m_paramListener);
    apvts.addParameterListener(shiftStrengthParamID(), &m_paramListener);
    apvts.addParameterListener(dampenUnprotectedParamID(), &m_paramListener);
    apvts.addParameterListener(electrifyParamID(), &m_paramListener);
    apvts.addParameterListener(pullParamID(), &m_paramListener);
    apvts.addParameterListener(outputGainParamID(), &m_paramListener);

    for (auto& ch : m_states)      for (auto& s : ch) s = Biquad::State{};
    for (auto& ch : m_boostStates) for (auto& s : ch) s = Biquad::State{};

    m_fft = std::make_unique<juce::dsp::FFT>(kFFTOrder);
    m_analyserFft = std::make_unique<juce::dsp::FFT>(kFFTOrder);
    buildHannWindow();

    m_spectrumSmooth.fill(0.0f);
    m_spectrumWrite.fill(0.0f);
    m_spectrumRead.fill(0.0f);
}

PitchControlAudioProcessor::~PitchControlAudioProcessor()
{
    for (int i = 0; i < kNumNotes; ++i)
        apvts.removeParameterListener(noteActiveParamID(i), &m_paramListener);
    apvts.removeParameterListener(depthParamID(), &m_paramListener);
    apvts.removeParameterListener(qParamID(), &m_paramListener);
    apvts.removeParameterListener(rangeFromParamID(), &m_paramListener);
    apvts.removeParameterListener(rangeToParamID(), &m_paramListener);
    apvts.removeParameterListener(wetOnlyParamID(), &m_paramListener);
    apvts.removeParameterListener(boostDBParamID(), &m_paramListener);
    apvts.removeParameterListener(boostQParamID(), &m_paramListener);
    apvts.removeParameterListener(fftModeParamID(), &m_paramListener);
    apvts.removeParameterListener(fftMixParamID(), &m_paramListener);
    apvts.removeParameterListener(shiftStrengthParamID(), &m_paramListener);
    apvts.removeParameterListener(dampenUnprotectedParamID(), &m_paramListener);
    apvts.removeParameterListener(electrifyParamID(), &m_paramListener);
    apvts.removeParameterListener(pullParamID(), &m_paramListener);
    apvts.removeParameterListener(outputGainParamID(), &m_paramListener);
}

//==============================================================================
const juce::String PitchControlAudioProcessor::getName() const { return JucePlugin_Name; }
bool PitchControlAudioProcessor::acceptsMidi()   const { return false; }
bool PitchControlAudioProcessor::producesMidi()  const { return false; }
bool PitchControlAudioProcessor::isMidiEffect()  const { return false; }
double PitchControlAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int PitchControlAudioProcessor::getNumPrograms() { return 1; }
int PitchControlAudioProcessor::getCurrentProgram() { return 0; }
void PitchControlAudioProcessor::setCurrentProgram(int) {}
const juce::String PitchControlAudioProcessor::getProgramName(int) { return {}; }
void PitchControlAudioProcessor::changeProgramName(int, const juce::String&) {}
bool PitchControlAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* PitchControlAudioProcessor::createEditor()
{
    return new PitchControlAudioProcessorEditor(*this);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PitchControlAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts); return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif
    return true;
#endif
}
#endif

//==============================================================================
void PitchControlAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    const int numCh = std::min(getTotalNumInputChannels(), kMaxChannels);

    for (int ch = 0; ch < kMaxChannels; ++ch)
        for (int n = 0; n < kTotalNotes; ++n)
        {
            m_states[ch][n] = Biquad::State{};
            m_boostStates[ch][n] = Biquad::State{};
        }

    for (int ch = 0; ch < numCh; ++ch)
    {
        m_inputFifo[ch].fill(0.0f);
        m_outputAccum[ch].fill(0.0f);
        m_fftWorkBuf[ch].fill(0.0f);
        m_fifoWritePos[ch] = 0;
        m_outputReadPos[ch] = 0;
    }
    m_samplesInHop = 0;
    m_outputHopsReady = 0;

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        m_prevInputPhase[ch].fill(0.0f);
        m_prevOutputPhase[ch].fill(0.0f);
    }

    // Spectrum analyser
    m_analyserFifo.fill(0.0f);
    m_analyserWorkBuf.fill(0.0f);
    m_analyserFifoPos = 0;
    m_analyserReady = false;
    m_spectrumSmooth.fill(0.0f);

    setLatencySamples(kFFTSize - kHopSize);

    m_dirty.store(true);
    m_binMapDirty.store(true);
}

void PitchControlAudioProcessor::releaseResources() {}

//==============================================================================
void PitchControlAudioProcessor::buildHannWindow()
{
    for (int i = 0; i < kFFTSize; ++i)
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (float)(kFFTSize - 1)));

    const float ola_gain = 1.5f;  // 75% overlap normalisation
    for (auto& w : m_window) w /= ola_gain;
}

//==============================================================================
// Spectrum analyser helpers (audio thread)
//==============================================================================
void PitchControlAudioProcessor::pushSampleToAnalyser(float sample)
{
    m_analyserFifo[m_analyserFifoPos++] = sample;
    if (m_analyserFifoPos >= kFFTSize)
    {
        m_analyserFifoPos = 0;
        m_analyserReady = true;
    }
}

void PitchControlAudioProcessor::runAnalyserIfReady()
{
    if (!m_analyserReady) return;
    m_analyserReady = false;

    // Window the fifo
    for (int i = 0; i < kFFTSize; ++i)
    {
        float win = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (float)(kFFTSize - 1)));
        m_analyserWorkBuf[i] = m_analyserFifo[i] * win;
        m_analyserWorkBuf[i + kFFTSize] = 0.0f;
    }

    m_analyserFft->performFrequencyOnlyForwardTransform(m_analyserWorkBuf.data());

    // Smooth with leaky integrator (alpha ~ 0.15 for fast response)
    constexpr float alpha = 0.15f;
    float peak = 0.001f;
    for (int k = 0; k < kNumFFTBins; ++k)
        peak = std::max(peak, m_analyserWorkBuf[k]);

    for (int k = 0; k < kNumFFTBins; ++k)
    {
        float mag = m_analyserWorkBuf[k] / peak;
        m_spectrumSmooth[k] += alpha * (mag - m_spectrumSmooth[k]);
    }

    // Copy to write buffer, then swap under lock
    {
        juce::ScopedLock sl(m_spectrumLock);
        m_spectrumWrite = m_spectrumSmooth;
    }
}

//==============================================================================
// Public getter — message thread safe
//==============================================================================
void PitchControlAudioProcessor::getSpectrumData(float* dest, int numBins) const
{
    juce::ScopedLock sl(m_spectrumLock);
    const int n = std::min(numBins, kNumFFTBins);
    std::copy(m_spectrumWrite.begin(), m_spectrumWrite.begin() + n, dest);
}

//==============================================================================
void PitchControlAudioProcessor::rebuildFilters()
{
    m_dirty.store(false, std::memory_order_relaxed);

    m_wetOnly = apvts.getRawParameterValue(wetOnlyParamID())->load() > 0.5f;

    const float depthDB = apvts.getRawParameterValue(depthParamID())->load();
    const float Q = apvts.getRawParameterValue(qParamID())->load();
    const float boostDB = apvts.getRawParameterValue(boostDBParamID())->load();
    const float boostQ = apvts.getRawParameterValue(boostQParamID())->load();

    int rangeFrom = (int)apvts.getRawParameterValue(rangeFromParamID())->load();
    int rangeTo = (int)apvts.getRawParameterValue(rangeToParamID())->load();
    if (rangeFrom > rangeTo) std::swap(rangeFrom, rangeTo);

    bool noteProtected[kNumNotes]{};
    int  numProtected = 0;
    for (int i = 0; i < kNumNotes; ++i)
    {
        noteProtected[i] = apvts.getRawParameterValue(noteActiveParamID(i))->load() > 0.5f;
        if (noteProtected[i]) ++numProtected;
    }

    const bool anyProtected = (numProtected > 0);
    m_anyActive = false;

    for (int n = 0; n < kTotalNotes; ++n)
    {
        int  noteClass = n % kNumNotes;
        bool isNoteProtected = noteProtected[noteClass];
        bool shouldFilter = m_wetOnly ? isNoteProtected : !isNoteProtected;

        bool active = anyProtected && (n >= rangeFrom && n <= rangeTo) && shouldFilter;
        m_filterActive[n] = active;
        if (active)
        {
            m_anyActive = true;
            m_filters[n] = makePeakingEQ(midiNoteToHz((double)n), currentSampleRate,
                (double)depthDB, (double)Q);
        }
        else m_filters[n] = Biquad{};

        bool shouldBoost = m_wetOnly ? !isNoteProtected : isNoteProtected;
        bool boostActive = anyProtected && (n >= rangeFrom && n <= rangeTo)
            && shouldBoost && (boostDB > 0.01f);
        m_boostActive[n] = boostActive;
        if (boostActive)
            m_boostFilters[n] = makePeakingEQ(midiNoteToHz((double)n), currentSampleRate,
                (double)boostDB, (double)boostQ);
        else m_boostFilters[n] = Biquad{};
    }

    for (int ch = 0; ch < kMaxChannels; ++ch)
        for (int n = 0; n < kTotalNotes; ++n)
        {
            if (m_filterActive[n]) m_states[ch][n] = Biquad::State{};
            if (m_boostActive[n])  m_boostStates[ch][n] = Biquad::State{};
        }
}

//==============================================================================
void PitchControlAudioProcessor::rebuildBinMap()
{
    m_binMapDirty.store(false, std::memory_order_relaxed);

    bool noteProtected[kNumNotes]{};
    int  numProtected = 0;
    for (int i = 0; i < kNumNotes; ++i)
    {
        noteProtected[i] = apvts.getRawParameterValue(noteActiveParamID(i))->load() > 0.5f;
        if (noteProtected[i]) ++numProtected;
    }

    int rangeFrom = (int)apvts.getRawParameterValue(rangeFromParamID())->load();
    int rangeTo = (int)apvts.getRawParameterValue(rangeToParamID())->load();
    if (rangeFrom > rangeTo) std::swap(rangeFrom, rangeTo);

    const float Q = apvts.getRawParameterValue(qParamID())->load();
    // Q=1 → sharpest attraction (exp=10), Q=100 → most gradual (exp=0.1)
    const float curveExp = juce::jlimit(0.1f, 10.0f, 10.0f * (1.0f - (Q - 1.0f) / 99.0f));

    // Pull=0.5 → pullScale=1.0 (unchanged from original hardcoded radius)
    // Pull=0.0 → pullScale=0.1 (very tight, only near-exact matches shift)
    // Pull=1.0 → pullScale=10.0 (wide, pulls bins from nearly a full tone away)
    const float pull = apvts.getRawParameterValue(pullParamID())->load();
    const float pullScale = std::pow(10.0f, (pull - 0.5f) * 2.0f);

    if (numProtected == 0)
    {
        for (int k = 0; k < kNumFFTBins; ++k)
            m_binMap[k] = { k, 0.0f, false };
        return;
    }

    std::vector<int> pnotes;
    pnotes.reserve(numProtected * (kNumOctaves + 2));
    for (int oct = -1; oct <= kNumOctaves; ++oct)
        for (int nc = 0; nc < kNumNotes; ++nc)
            if (noteProtected[nc])
                pnotes.push_back(nc + oct * kNumNotes);
    std::sort(pnotes.begin(), pnotes.end());

    for (int k = 0; k < kNumFFTBins; ++k)
    {
        const double binHz = (double)k * currentSampleRate / (double)kFFTSize;
        if (k == 0 || binHz < 20.0) { m_binMap[k] = { k, 0.0f, false }; continue; }

        const double binMidi = hzToMidi(binHz);
        if (binMidi < (double)rangeFrom || binMidi >(double)rangeTo)
        {
            m_binMap[k] = { k, 0.0f, false }; continue;
        }

        int   bestNote = pnotes[0];
        float bestCentDist = 1e9f;
        for (int mn : pnotes)
        {
            float dist = std::abs((float)(binMidi - (double)mn)) * 100.0f;
            if (dist < bestCentDist) { bestCentDist = dist; bestNote = mn; }
        }

        float secondCentDist = 1e9f;
        for (int mn : pnotes)
        {
            if (mn == bestNote) continue;
            float dist = std::abs((float)(binMidi - (double)mn)) * 100.0f;
            if (dist < secondCentDist) secondCentDist = dist;
        }

        const float attractionRadius = pullScale * juce::jmax(50.0f, secondCentDist * 0.5f);
        const double targetHz = midiNoteToHz((double)bestNote);
        const int    targetBin = juce::jlimit(0, kNumFFTBins - 1,
            (int)std::round(targetHz * (double)kFFTSize / currentSampleRate));

        const bool  isProtected = (bestCentDist < 5.0f);
        const float normDist = juce::jlimit(0.0f, 1.0f, bestCentDist / attractionRadius);
        const float weight = std::pow(1.0f - normDist, curveExp);

        m_binMap[k] = { targetBin, weight, isProtected };
    }
}

//==============================================================================
void PitchControlAudioProcessor::processFFTHop(int numChannels)
{
    const float shiftStrength = apvts.getRawParameterValue(shiftStrengthParamID())->load();
    const float fftMix = apvts.getRawParameterValue(fftMixParamID())->load();
    const bool  dampenUnprotected = apvts.getRawParameterValue(dampenUnprotectedParamID())->load() > 0.5f;
    const float boostDB = apvts.getRawParameterValue(boostDBParamID())->load();
    const float boostLin = (boostDB > 0.01f) ? std::pow(10.0f, boostDB / 20.0f) : 1.0f;
    // 0 = fully coherent (clean/resolved), 1 = raw phase (electric) — same default as before
    const float electrify = apvts.getRawParameterValue(electrifyParamID())->load();

    // Expected phase advance per bin per hop (phase vocoder constant)
    const float twoPi = 2.0f * (float)M_PI;
    const float hopPhaseIncr = twoPi * (float)kHopSize / (float)kFFTSize;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& fifo = m_inputFifo[ch];
        auto& workBuf = m_fftWorkBuf[ch];
        workBuf.fill(0.0f);

        const int wp = m_fifoWritePos[ch];
        for (int i = 0; i < kFFTSize; ++i)
        {
            int idx = (wp - kFFTSize + i + kFFTSize * 2) % (kFFTSize * 2);
            workBuf[i] = fifo[idx] * m_window[i];
        }

        m_fft->performRealOnlyForwardTransform(workBuf.data());

        // ---------------------------------------------------------------
        // Polar + true-frequency analysis
        // ---------------------------------------------------------------
        std::array<float, kNumFFTBins> srcMag, srcPhase, trueFreqBin;

        // Find per-hop peak for magnitude-gating the phase estimate.
        // Bins below 1% of peak have unreliable dPhi → use nominal freq instead,
        // giving smooth coherent tone rather than noise-modulated ring artefacts.
        float hopPeak = 1e-10f;
        for (int k = 0; k < kNumFFTBins; ++k)
        {
            float re = workBuf[k * 2], im = workBuf[k * 2 + 1];
            hopPeak = std::max(hopPeak, std::sqrt(re * re + im * im));
        }
        const float magThresh = hopPeak * 0.01f;

        for (int k = 0; k < kNumFFTBins; ++k)
        {
            float re = workBuf[k * 2], im = workBuf[k * 2 + 1];
            srcMag[k] = std::sqrt(re * re + im * im);
            srcPhase[k] = std::atan2(im, re);

            // Phase difference from the previous hop, minus the expected advance
            float dPhi = srcPhase[k] - m_prevInputPhase[ch][k] - (float)k * hopPhaseIncr;
            // Wrap to [-pi, pi]
            dPhi -= twoPi * std::round(dPhi / twoPi);
            // Gate: only trust the phase derivative for bins with real energy.
            // Weak bins (noise floor) use nominal bin frequency so they cohere
            // smoothly rather than producing random phase jumps.
            trueFreqBin[k] = (srcMag[k] > magThresh)
                ? (float)k + dPhi / hopPhaseIncr
                : (float)k;

            m_prevInputPhase[ch][k] = srcPhase[k];
        }

        // ---------------------------------------------------------------
        // Bin mapping — accumulate into output bins
        // dominant-magnitude source drives phase propagation per output bin
        // ---------------------------------------------------------------
        std::array<float, kNumFFTBins> outMag{}, outTrueFreq{};
        std::array<float, kNumFFTBins> outBestMag{}, residMag{};
        // Circular mean accumulators: magnitude-weighted sin/cos components.
        // Plain scalar phase averaging is wrong for angles (breaks at the ±π
        // boundary and intermodulates when multiple bins fold into one target).
        std::array<float, kNumFFTBins> outSinSum{}, outCosSum{};
        std::array<int, kNumFFTBins> outCount{};
        outMag.fill(0); outTrueFreq.fill(0);
        outBestMag.fill(0); residMag.fill(0);
        outSinSum.fill(0); outCosSum.fill(0); outCount.fill(0);

        for (int k = 0; k < kNumFFTBins; ++k)
        {
            const BinMapping& bm = m_binMap[k];
            const float ew = bm.blendWeight * shiftStrength;
            const float contrib = srcMag[k] * ew;

            outMag[bm.targetBin] += contrib;
            // Magnitude-weighted circular accumulation — correct for angles
            outSinSum[bm.targetBin] += contrib * std::sin(srcPhase[k]);
            outCosSum[bm.targetBin] += contrib * std::cos(srcPhase[k]);
            outCount[bm.targetBin] += 1;

            // Winner-takes-all true frequency: dominant source bin drives phase propagation
            if (contrib > outBestMag[bm.targetBin])
            {
                outBestMag[bm.targetBin] = contrib;
                float freqRatio = (k > 0) ? (float)bm.targetBin / (float)k : 1.0f;
                outTrueFreq[bm.targetBin] = trueFreqBin[k] * freqRatio;
            }

            float residFrac = 1.0f - ew;
            if (dampenUnprotected && !bm.isProtected)
                residFrac *= (1.0f - shiftStrength);
            residMag[k] += srcMag[k] * residFrac;
        }

        // Resolve circular mean to a single phase angle per output bin.
        // When only one source contributed, atan2(contrib*sin, contrib*cos) = srcPhase exactly.
        std::array<float, kNumFFTBins> outPhase;
        for (int k = 0; k < kNumFFTBins; ++k)
            outPhase[k] = std::atan2(outSinSum[k], outCosSum[k]);

        // ---------------------------------------------------------------
        // Build coherent output phases (phase vocoder propagation)
        // ---------------------------------------------------------------
        // coherentPhase[k] = prevOutputPhase[k] + trueFreq[k] * hopPhaseIncr
        // This keeps spectral components in phase across hops → "resolved" sound
        std::array<float, kNumFFTBins> coherentPhase;
        for (int k = 0; k < kNumFFTBins; ++k)
        {
            float tf = (outCount[k] > 0 && outBestMag[k] > 0.0f)
                ? outTrueFreq[k]
                : (float)k;                 // unshifted bin: propagate naturally
            coherentPhase[k] = m_prevOutputPhase[ch][k] + tf * hopPhaseIncr;
        }

        if (boostDB > 0.01f)
            for (int k = 0; k < kNumFFTBins; ++k)
                if (m_binMap[k].isProtected) outMag[k] *= boostLin;

        // ---------------------------------------------------------------
        // Synthesise output, blending coherent vs. electric phase
        // ---------------------------------------------------------------
        workBuf.fill(0.0f);
        for (int k = 0; k < kNumFFTBins; ++k)
        {
            float wetMag, wetPhase;
            if (outCount[k] > 0)
            {
                wetMag = residMag[k] + outMag[k];
                float sw = outMag[k] / (outMag[k] + residMag[k] + 1e-12f);

                // Electric (original): raw phase blend
                float pElectric = srcPhase[k] * (1.0f - sw) + outPhase[k] * sw;
                // Coherent (new): phase-vocoder propagated
                float pClean = coherentPhase[k];

                wetPhase = pClean * (1.0f - electrify) + pElectric * electrify;
            }
            else
            {
                wetMag = residMag[k];
                // Unshifted bins: blend coherent propagation vs original srcPhase,
                // exactly like the if-branch — at electrify=1.0 this is identical
                // to the original code (srcPhase[k]), at 0.0 it's fully coherent.
                float coherentUnshifted = m_prevOutputPhase[ch][k] + (float)k * hopPhaseIncr;
                wetPhase = coherentUnshifted * (1.0f - electrify) + srcPhase[k] * electrify;
            }

            float finalMag = srcMag[k] * (1.0f - fftMix) + wetMag * fftMix;
            float finalPhase = srcPhase[k] * (1.0f - fftMix) + wetPhase * fftMix;

            // Store wetPhase (synthesis output phase), NOT finalPhase.
            // The dry/wet blend is an audio-domain mix; the coherent propagation
            // chain must advance from the pure synthesis phase or it drifts.
            m_prevOutputPhase[ch][k] = wetPhase;

            workBuf[k * 2] = finalMag * std::cos(finalPhase);
            workBuf[k * 2 + 1] = finalMag * std::sin(finalPhase);
        }

        for (int k = kNumFFTBins; k < kFFTSize; ++k)
        {
            workBuf[k * 2] = workBuf[(kFFTSize - k) * 2];
            workBuf[k * 2 + 1] = -workBuf[(kFFTSize - k) * 2 + 1];
        }

        m_fft->performRealOnlyInverseTransform(workBuf.data());

        auto& outBuf = m_outputAccum[ch];
        const int rp = m_outputReadPos[ch];
        for (int i = 0; i < kFFTSize; ++i)
        {
            int outIdx = (rp + i) % (kFFTSize * 2);
            outBuf[outIdx] += workBuf[i] * m_window[i];
        }
    }

    ++m_outputHopsReady;
}

//==============================================================================
void PitchControlAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int N = buffer.getNumSamples();

    for (int i = numIn; i < numOut; ++i) buffer.clear(i, 0, N);

    if (m_dirty.load(std::memory_order_relaxed))       rebuildFilters();
    if (m_binMapDirty.load(std::memory_order_relaxed)) rebuildBinMap();

    const bool fftMode = apvts.getRawParameterValue(fftModeParamID())->load() > 0.5f;
    const int  numCh = std::min(numIn, kMaxChannels);

    //==========================================================================
    if (!fftMode)
    {
        if (!m_anyActive) { if (m_wetOnly) buffer.clear(); return; }

        const float outputGainDB = apvts.getRawParameterValue(outputGainParamID())->load();
        const float outputGainLin = std::pow(10.0f, outputGainDB / 20.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int s = 0; s < N; ++s)
            {
                const float dry = data[s];
                double proc = (double)dry;
                for (int n = 0; n < kTotalNotes; ++n) if (m_filterActive[n]) proc = m_filters[n].process(proc, m_states[ch][n]);
                for (int n = 0; n < kTotalNotes; ++n) if (m_boostActive[n])  proc = m_boostFilters[n].process(proc, m_boostStates[ch][n]);
                data[s] = (m_wetOnly ? dry - (float)proc : (float)proc) * outputGainLin;
            }
        }
        // Feed wet output into spectrum analyser
        for (int s = 0; s < N; ++s)
            pushSampleToAnalyser(buffer.getSample(0, s));
        runAnalyserIfReady();
        return;
    }

    //==========================================================================
    for (int s = 0; s < N; ++s)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            m_inputFifo[ch][m_fifoWritePos[ch]] = buffer.getSample(ch, s);
            m_fifoWritePos[ch] = (m_fifoWritePos[ch] + 1) % (kFFTSize * 2);
        }

        if (++m_samplesInHop >= kHopSize)
        {
            m_samplesInHop = 0;
            processFFTHop(numCh);
        }

        if (m_outputHopsReady >= 3)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                int rp = m_outputReadPos[ch];
                buffer.setSample(ch, s, m_outputAccum[ch][rp]);
                m_outputAccum[ch][rp] = 0.0f;
            }
            for (int ch = 0; ch < numCh; ++ch)
                m_outputReadPos[ch] = (m_outputReadPos[0] + 1) % (kFFTSize * 2);
        }
        else
        {
            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample(ch, s, 0.0f);
        }
    }
    // Feed wet output into spectrum analyser (after all FFT processing)
    for (int s = 0; s < N; ++s)
        pushSampleToAnalyser(buffer.getSample(0, s));
    runAnalyserIfReady();
}

//==============================================================================
void PitchControlAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PitchControlAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchControlAudioProcessor();
}