/*
  ==============================================================================
    CrispEngine.h  --  the "Crisp" engine: your beta resonator DSP, kept as-is
    (comb + one-pole damping + tanh, LFO chorus, membrane wobble, DC "Center",
    exponential decay shaping, input/output Smooth, optional 2x oversampling).
    Wrapped in its own namespace/class so it can run side by side with the
    Modelled and Native engines.
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace CrispEngine
{
enum FilterType { Lowpass = 0, Highpass, Bandpass, Notch };
enum ProcessingMode { ModeA = 0, ModeB = 1 };
static constexpr int kMaxRes = 7;

struct SVF
{
    float low[2] = { 0.0f, 0.0f };
    float band[2] = { 0.0f, 0.0f };
    float freq = 0.5f;
    float q = 0.7f;

    void  setFrequency(float normalizedFreq, float resonance);
    float process(float input, int channel, FilterType type);
    void  reset();
};

struct Resonate
{
    static constexpr int MAX_DELAY_SAMPLES = 8192;
    std::vector<float> delayBuffer[2];
    int writeIndex[2] = { 0, 0 };

    double targetFrequency = 440.0;
    double delayInSamples = 100.0;
    double feedback = 0.9;
    int    pitchSemitones = 0;
    double fineDetune = 0.0;
    double gain = 0.0;
    bool   enabled = true;

    float  lpfState[2] = { 0.0f, 0.0f };
    float  lpfCoeff = 1.0f;

    double lfoPhase[2] = { 0.0, 0.0 };
    double lfoRate = 0.0;
    double lfoDepthCents = 0.0;
    double currentSampleRate = 44100.0;

    uint32_t noiseSeed[2] = { 12345u, 67890u };
    float    membraneFilter[2] = { 0.0f, 0.0f };
    double   membraneSamples = 0.0;

    float  dcEnv[2] = { 0.0f, 0.0f };
    bool   centerMode = true;

    float  expEnv[2] = { 0.0f, 0.0f };
    float  expCoeff = 0.9f;
    bool   expDecay = false;

    double localDecay = 85.0;
    double localColor = 50.0;
    bool   localConst = false;
    bool   useLocalParams = false;

    void  prepare(double sampleRate);
    void  updateParameters(double sampleRate,
        double globalDecay, double globalNote,
        double color,
        ProcessingMode mode, bool constMode,
        double chorusAmount, int resonatorIndex,
        double userLfoRate, double userLfoDepth,
        bool   center,
        bool   useLocal,
        double lDecay, double lColor, bool lConst,
        double midiNote = -1.0);
    float processA(float input, int channel);
    float processB(float input, int channel);
    void  reset();

private:
    float readDelayLinear(int channel, double delaySamples);
    float fastNoise(int channel)
    {
        noiseSeed[channel] = noiseSeed[channel] * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(noiseSeed[channel]))
            / static_cast<float>(0x80000000);
    }
};

//==============================================================================
// All parameters the Crisp engine reads (filled by the processor each block).
struct Params
{
    double note = 60, decay = 80, color = 52, smooth = 0, chorus = 0, lfoRate = 0.5, lfoDepth = 5;
    bool   filterOn = false; double filterFreq = 1000; int filterType = 0;
    bool   constMode = false, center = true, expDecay = false, perRes = false, os2x = false;
    int    mode = 0;
    float  width = 1.0f;
    double pan[kMaxRes] = { 0.0, -100.0, 100.0, -100.0, 100.0, -100.0, 100.0 };
    struct Res { bool enabled = false; int pitch = 0; double fine = 0, gain = 0, decay = 95, color = 74.2; bool cnst = false; double midiNote = -1; };
    Res res[kMaxRes];
};

//==============================================================================
class Engine
{
public:
    void prepare(double sampleRate, int samplesPerBlock)
    {
        currentSampleRate = sampleRate;
        for (auto& r : resonators) { r.prepare(sampleRate); r.reset(); }
        inputFilter.reset();
        inputSmoothing[0] = inputSmoothing[1] = 0.0f;
        outputSmoothing[0] = outputSmoothing[1] = 0.0f;

        const int blockSize = juce::jmax(1, samplesPerBlock);
        const int maxOs = blockSize * 2;
        res1Buffer.setSize(2, maxOs, false, true, true);
        res2to5Buffer.setSize(2, maxOs, false, true, true);
        wetBuffer.setSize(2, maxOs, false, true, true);
        osInputBuffer.setSize(2, blockSize, false, true, true);
        outBuffer.setSize(2, blockSize, false, true, true);

        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampler->initProcessing(static_cast<size_t>(blockSize));
        oversampler->reset();
    }

    void reset()
    {
        for (auto& r : resonators) r.reset();
        inputFilter.reset();
        inputSmoothing[0] = inputSmoothing[1] = 0.0f;
        outputSmoothing[0] = outputSmoothing[1] = 0.0f;
        if (oversampler) oversampler->reset();
    }

    // Renders the Crisp wet signal (before master gain / dry-wet) for
    // numSamples of input into outBuffer (read it with getOutput()).
    void process(const float* const inPtr[2], int numSamples, int nIn, int nOut, const Params& p)
    {
        const bool osEnabled = p.os2x && oversampler != nullptr;
        update(p, osEnabled ? currentSampleRate * 2.0 : currentSampleRate);

        const float smoothNorm = 1.0f - static_cast<float>(p.smooth / 100.0);
        const float smoothCoeff = 0.01f + smoothNorm * 0.4f;

        if (outBuffer.getNumSamples() < numSamples)   // host sent a bigger block than announced
        {
            outBuffer.setSize(2, numSamples, false, true, true);
            osInputBuffer.setSize(2, numSamples, false, true, true);
        }
        outBuffer.clear();

        if (!osEnabled)
        {
            if (res1Buffer.getNumSamples() < numSamples)
            {
                res1Buffer.setSize(2, numSamples, false, true, true);
                res2to5Buffer.setSize(2, numSamples, false, true, true);
                wetBuffer.setSize(2, numSamples, false, true, true);
            }
            res1Buffer.clear(); res2to5Buffer.clear(); wetBuffer.clear();
            renderWet(inPtr, numSamples, nIn, nOut, smoothCoeff);
            for (int ch = 0; ch < nOut; ++ch)
                outBuffer.copyFrom(ch, 0, wetBuffer, ch, 0, numSamples);
            return;
        }

        // 2x oversampled path (as in the beta)
        osInputBuffer.clear();
        for (int ch = 0; ch < nIn; ++ch)
            osInputBuffer.copyFrom(ch, 0, inPtr[ch], numSamples);

        juce::dsp::AudioBlock<float> inBlock(osInputBuffer);
        inBlock = inBlock.getSubBlock(0, static_cast<size_t>(numSamples));
        auto upBlock = oversampler->processSamplesUp(inBlock);
        const int osNumSamples = static_cast<int>(upBlock.getNumSamples());

        if (res1Buffer.getNumSamples() < osNumSamples)
        {
            res1Buffer.setSize(2, osNumSamples, false, true, true);
            res2to5Buffer.setSize(2, osNumSamples, false, true, true);
            wetBuffer.setSize(2, osNumSamples, false, true, true);
        }
        res1Buffer.clear(); res2to5Buffer.clear(); wetBuffer.clear();

        const float* osInPtr[2] = { upBlock.getChannelPointer(0),
                                    upBlock.getNumChannels() > 1 ? upBlock.getChannelPointer(1) : nullptr };
        renderWet(osInPtr, osNumSamples, nIn, nOut, smoothCoeff);

        for (int ch = 0; ch < nOut; ++ch)
            std::memcpy(upBlock.getChannelPointer(static_cast<size_t>(ch)), wetBuffer.getReadPointer(ch),
                        static_cast<size_t>(osNumSamples) * sizeof(float));

        juce::dsp::AudioBlock<float> downBlock(outBuffer);
        downBlock = downBlock.getSubBlock(0, static_cast<size_t>(numSamples));
        oversampler->processSamplesDown(downBlock);
    }

    const float* getOutput(int ch) const { return outBuffer.getReadPointer(juce::jmin(ch, 1)); }

private:
    Resonate resonators[kMaxRes];
    SVF      inputFilter;
    double   currentSampleRate = 44100.0;

    bool           filterEnabled = false;
    FilterType     filterType = Lowpass;
    ProcessingMode processingMode = ModeA;
    float          currentWidth = 1.0f;
    double         resPan[kMaxRes] = { 0.0, -100.0, 100.0, -100.0, 100.0, -100.0, 100.0 };
    float inputSmoothing[2] = { 0.0f, 0.0f };
    float outputSmoothing[2] = { 0.0f, 0.0f };

    juce::AudioBuffer<float> res1Buffer, res2to5Buffer, wetBuffer, osInputBuffer, outBuffer;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    void update(const Params& p, double processSampleRate)
    {
        filterEnabled = p.filterOn;
        filterType = static_cast<FilterType>(p.filterType);
        processingMode = (p.mode == 0) ? ModeA : ModeB;
        currentWidth = p.width;
        for (int i = 0; i < kMaxRes; ++i) resPan[i] = p.pan[i];

        if (filterEnabled && processSampleRate > 0.0)
        {
            float normFreq = juce::jlimit(0.0f, 0.99f,
                static_cast<float>(p.filterFreq / (processSampleRate * 0.5)));
            inputFilter.setFrequency(normFreq, 0.7f);
        }

        for (int i = 0; i < kMaxRes; ++i)
        {
            auto& r = resonators[i];
            const auto& q = p.res[i];
            r.enabled = q.enabled;
            r.pitchSemitones = (i == 0) ? 0 : q.pitch;
            r.fineDetune = q.fine;
            r.gain = q.gain;
            r.expDecay = p.expDecay;
            r.updateParameters(processSampleRate, p.decay, p.note, p.color, processingMode, p.constMode,
                p.chorus, i, p.lfoRate, p.lfoDepth, p.center,
                p.perRes, q.decay, q.color, q.cnst, q.midiNote);
        }
    }

    void renderWet(const float* const inPtr[2], int numSamples, int nIn, int nOut, float smoothCoeff);
};

//==============================================================================
//==============================================================================
// State Variable Filter

inline void SVF::setFrequency(float normalizedFreq, float resonance)
{
    freq = juce::jlimit(0.0f, 0.99f, normalizedFreq);
    q = juce::jlimit(0.1f, 10.0f, resonance);
}

inline float SVF::process(float input, int channel, FilterType type)
{
    float fb = q + q / (1.0f - freq);
    low[channel] += freq * band[channel];
    float high = input - low[channel] - fb * band[channel];
    band[channel] += freq * high;

    switch (type)
    {
    case Lowpass:  return low[channel];
    case Highpass: return high;
    case Bandpass: return band[channel];
    case Notch:    return low[channel] + high;
    default:       return low[channel];
    }
}

inline void SVF::reset()
{
    low[0] = low[1] = 0.0f;
    band[0] = band[1] = 0.0f;
}

//==============================================================================
// Resonator helpers

inline void Resonate::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
    for (int ch = 0; ch < 2; ++ch)
    {
        delayBuffer[ch].resize(MAX_DELAY_SAMPLES, 0.0f);
        writeIndex[ch] = 0;
        lpfState[ch] = 0.0f;
        lfoPhase[ch] = 0.0;
        membraneFilter[ch] = 0.0f;
        dcEnv[ch] = 0.0f;
        expEnv[ch] = 0.0f;
    }
}

inline void Resonate::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill(delayBuffer[ch].begin(), delayBuffer[ch].end(), 0.0f);
        writeIndex[ch] = 0;
        lpfState[ch] = 0.0f;
        membraneFilter[ch] = 0.0f;
        dcEnv[ch] = 0.0f;
        expEnv[ch] = 0.0f;
    }
}

inline void Resonate::updateParameters(
    double sampleRate,
    double globalDecay, double globalNote,
    double color,
    ProcessingMode mode, bool constMode,
    double chorusAmount, int resonatorIndex,
    double userLfoRate, double userLfoDepth,
    bool   center,
    bool   useLocal, double lDecay, double lColor, bool lConst,
    double midiNote)
{
    if (!enabled || sampleRate <= 0.0)
    {
        feedback = 0.0;
        return;
    }

    currentSampleRate = sampleRate;
    centerMode = center;
    // expDecay is set directly from the processor before updateParameters is called
    useLocalParams = useLocal;
    localDecay = lDecay;
    localColor = lColor;
    localConst = lConst;

    // Effective decay, color, const (local overrides global when per-res mode on)
    double effDecay = useLocalParams ? localDecay : globalDecay;
    double effColor = useLocalParams ? localColor : color;
    bool   effConst = useLocalParams ? localConst : constMode;

    // ── LFO for chorus/beating ────────────────────────────────────────────
    const double rateOffsets[7] = { 0.0, 0.13, 0.27, 0.41, 0.19, 0.05, 0.35 };
    double baseRate = userLfoRate;
    lfoRate = (baseRate + rateOffsets[resonatorIndex % 7] * 0.3)
        * (chorusAmount / 100.0);
    lfoDepthCents = userLfoDepth * (chorusAmount / 100.0);

    // ── Membrane depth (sample-domain, scales with frequency naturally) ───
    membraneSamples = (chorusAmount / 100.0) * 1.5;

    // ── Frequency from MIDI note (if held) or knob note + pitch, then fine ─
    // midiNote >= 0 replaces the (globalNote + pitchSemitones) pair entirely;
    // fine detune still applies on top so you can still beat notes against
    // each other while playing.
    double totalNote = (midiNote >= 0.0)
        ? midiNote
        : (globalNote + static_cast<double>(pitchSemitones));

    double cents = (totalNote - 69.0) * 100.0 + fineDetune;
    targetFrequency = 440.0 * std::pow(2.0, cents / 1200.0);
    targetFrequency = juce::jlimit(20.0, 20000.0, targetFrequency);

    delayInSamples = sampleRate / targetFrequency;
    delayInSamples = juce::jlimit(2.0,
        static_cast<double>(MAX_DELAY_SAMPLES - 2),
        delayInSamples);

    // ── Feedback from decay ───────────────────────────────────────────────
    // UNCHANGED mapping from decay VALUE to feedback. Only the knob-travel-to-
    // value curve moved (see createParameterLayout), so a given displayed
    // decay number still produces exactly the same feedback as before.
    double decayNorm = effDecay / 100.0;
    feedback = std::pow(decayNorm, 0.25);
    feedback = juce::jlimit(0.0, 0.9999, feedback);

    // ── Color → damping LPF coefficient ──────────────────────────────────
    double colorNorm = effColor / 100.0;
    lpfCoeff = static_cast<float>(0.3 + colorNorm * 0.69);

    if (effConst)
    {
        double referenceFreq = 261.63;
        double freqRatio = referenceFreq / targetFrequency;
        double constCompensation = std::pow(freqRatio, 0.3);
        feedback *= constCompensation;
        feedback = juce::jlimit(0.0, 0.9999, feedback);
    }

    // ── Exp decay coefficient (freq-scaled, used when expDecay is on) ────────
    {
        double referenceFreq = 261.63;
        double exponent = targetFrequency / referenceFreq;
        expCoeff = static_cast<float>(
            juce::jlimit(0.0, 0.9999, std::pow(feedback, exponent)));
    }
}

inline float Resonate::readDelayLinear(int channel, double delaySamples)
{
    double readPos = static_cast<double>(writeIndex[channel]) - delaySamples;
    while (readPos < 0.0)
        readPos += static_cast<double>(MAX_DELAY_SAMPLES);

    int   index1 = static_cast<int>(readPos) % MAX_DELAY_SAMPLES;
    int   index2 = (index1 + 1) % MAX_DELAY_SAMPLES;
    float frac = static_cast<float>(readPos - std::floor(readPos));

    return delayBuffer[channel][index1] + frac * (delayBuffer[channel][index2]
        - delayBuffer[channel][index1]);
}

inline float Resonate::processA(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    // ── 1. Compute effective delay (LFO + membrane wobble) ───────────────────
    double actualDelay = delayInSamples;

    if (lfoDepthCents > 0.001 && lfoRate > 0.0)
    {
        lfoPhase[channel] += lfoRate / currentSampleRate;
        if (lfoPhase[channel] >= 1.0) lfoPhase[channel] -= 1.0;
        double lfo = std::sin(lfoPhase[channel] * juce::MathConstants<double>::twoPi);
        double freqRatio = std::pow(2.0, lfo * lfoDepthCents / 1200.0);
        actualDelay = delayInSamples / freqRatio;
    }

    if (membraneSamples > 0.001)
    {
        float noise = fastNoise(channel);
        membraneFilter[channel] += 0.0003f * (noise - membraneFilter[channel]);
        actualDelay += static_cast<double>(membraneFilter[channel]) * membraneSamples;
    }

    actualDelay = juce::jlimit(2.0,
        static_cast<double>(MAX_DELAY_SAMPLES - 2),
        actualDelay);

    // ── 3. Classic comb filter with LPF damping ───────────────────────────────
    float delayedSample = readDelayLinear(channel, actualDelay);

    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float dampedFeedback = lpfState[channel];

    float output = input + static_cast<float>(feedback) * dampedFeedback;
    output = std::tanh(output);

    delayBuffer[channel][writeIndex[channel]] = output;
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;

    // ── 4. DC bias applied to OUTPUT only (not the feedback path) ────────────
    if (!centerMode)
    {
        float absOut = std::abs(output);
        float attack = 0.05f;
        float release = 0.0002f;
        dcEnv[channel] += (absOut > dcEnv[channel])
            ? attack * (absOut - dcEnv[channel])
            : release * (absOut - dcEnv[channel]);
        output += dcEnv[channel] * 0.65f;
    }

    // ── 5. Exponential decay envelope (freq-scaled) ──────────────────────────
    if (expDecay)
    {
        float absOut = std::abs(output);
        if (absOut > expEnv[channel])
            expEnv[channel] = absOut;
        else
            expEnv[channel] *= expCoeff;
        if (expEnv[channel] < 1e-6f) expEnv[channel] = 0.0f;
        output *= (expEnv[channel] > 0.0f) ? expEnv[channel] / (expEnv[channel] + 0.001f) : 0.0f;
    }

    float gainLinear = std::pow(10.0f, static_cast<float>(gain) / 20.0f);
    return output * gainLinear;
}

inline float Resonate::processB(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    double baseModeDelay = delayInSamples * 0.5;
    double actualDelay = baseModeDelay;

    if (lfoDepthCents > 0.001 && lfoRate > 0.0)
    {
        lfoPhase[channel] += lfoRate / currentSampleRate;
        if (lfoPhase[channel] >= 1.0) lfoPhase[channel] -= 1.0;
        double lfo = std::sin(lfoPhase[channel] * juce::MathConstants<double>::twoPi);
        double freqRatio = std::pow(2.0, lfo * lfoDepthCents / 1200.0);
        actualDelay = baseModeDelay / freqRatio;
    }

    if (membraneSamples > 0.001)
    {
        float noise = fastNoise(channel);
        membraneFilter[channel] += 0.0003f * (noise - membraneFilter[channel]);
        actualDelay += static_cast<double>(membraneFilter[channel]) * membraneSamples;
    }

    actualDelay = std::max(1.0, actualDelay);

    float delayedSample = readDelayLinear(channel, actualDelay);
    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float damped = lpfState[channel];

    float output = std::tanh(input - static_cast<float>(feedback) * damped);

    delayBuffer[channel][writeIndex[channel]] = output;
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;

    if (!centerMode)
    {
        float absOut = std::abs(output);
        float attack = 0.05f;
        float release = 0.0002f;
        dcEnv[channel] += (absOut > dcEnv[channel])
            ? attack * (absOut - dcEnv[channel])
            : release * (absOut - dcEnv[channel]);
        output += dcEnv[channel] * 0.65f;
    }

    if (expDecay)
    {
        float absOut = std::abs(output);
        if (absOut > expEnv[channel])
            expEnv[channel] = absOut;
        else
            expEnv[channel] *= expCoeff;
        if (expEnv[channel] < 1e-6f) expEnv[channel] = 0.0f;
        output *= (expEnv[channel] > 0.0f) ? expEnv[channel] / (expEnv[channel] + 0.001f) : 0.0f;
    }

    float gainLinear = std::pow(10.0f, static_cast<float>(gain) / 20.0f);
    return output * gainLinear;
}

//==============================================================================
inline void Engine::renderWet(const float* const inPtr[2], int numSamples, int nIn, int nOut,
    float smoothCoeff)
{
    // ── Pan gains ────────────────────────────────────────────────────────────
    // Resonator I is stereo: BALANCE law, unity on both sides at centre.
    // Resonators II-VII are mono sources: constant-power law that reaches
    // exactly 1.0 at the extremes, so the default hard pans are bit-identical
    // to the previous fixed routing.
    const int n = kMaxRes;
    float panL[kMaxRes], panR[kMaxRes];
    for (int i = 0; i < n; ++i)
    {
        const double p = juce::jlimit(-1.0, 1.0, resPan[i] / 100.0);

        if (i == 0)
        {
            panL[0] = static_cast<float>(p <= 0.0 ? 1.0 : 1.0 - p);
            panR[0] = static_cast<float>(p >= 0.0 ? 1.0 : 1.0 + p);
        }
        else if (p <= -0.9995)   // snap the extremes: cos(pi/2) is ~6e-17, not 0,
        {                        // which would leak the old hard pans at -147 dB
            panL[i] = 1.0f; panR[i] = 0.0f;
        }
        else if (p >= 0.9995)
        {
            panL[i] = 0.0f; panR[i] = 1.0f;
        }
        else
        {
            const double theta = (p + 1.0) * juce::MathConstants<double>::pi * 0.25;
            panL[i] = static_cast<float>(std::cos(theta));
            panR[i] = static_cast<float>(std::sin(theta));
        }
    }

    // Which input channel each resonator reads. UNCHANGED from the original
    // for I-V: keeping this fixed is what makes each resonator's internal
    // state evolve exactly as before. VI/VII continue the same L/R alternation.
    static const int sourceChannel[kMaxRes] = { -1, 0, 1, 0, 1, 0, 1 };

    float* res1Ptr[2] = { res1Buffer.getWritePointer(0),
                            res1Buffer.getNumChannels() > 1 ? res1Buffer.getWritePointer(1) : nullptr };
    float* res25Ptr[2] = { res2to5Buffer.getWritePointer(0),
                            res2to5Buffer.getNumChannels() > 1 ? res2to5Buffer.getWritePointer(1) : nullptr };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // ── Input conditioning, per channel (state is per-channel, so the
        //    loop order swap versus the original is bit-exact) ──────────────
        float sIn[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < nIn; ++ch)
        {
            float input = inPtr[ch][sample];
            inputSmoothing[ch] += smoothCoeff * (input - inputSmoothing[ch]);
            float s = inputSmoothing[ch];
            if (filterEnabled)
                s = inputFilter.process(s, ch, filterType);
            sIn[ch] = s;
        }

        // ── Resonator I (stereo) ─────────────────────────────────────────────
        float r1L = 0.0f, r1R = 0.0f;
        for (int ch = 0; ch < nIn; ++ch)
        {
            const float o = (processingMode == ModeA)
                ? resonators[0].processA(sIn[ch], ch)
                : resonators[0].processB(sIn[ch], ch);

            if (ch == 0) r1L = o * panL[0];
            else         r1R = o * panR[0];
        }

        // ── Resonators II-VII (mono sources, panned) ─────────────────────────
        float busL = 0.0f, busR = 0.0f;
        for (int idx = 1; idx < n; ++idx)
        {
            const int ch = sourceChannel[idx];
            if (ch >= nIn) continue;   // mono input: odd-numbered resonators stay silent, as before

            const float o = (processingMode == ModeA)
                ? resonators[idx].processA(sIn[ch], ch)
                : resonators[idx].processB(sIn[ch], ch);

            busL += o * panL[idx];
            busR += o * panR[idx];
        }

        res1Ptr[0][sample] = r1L;
        res25Ptr[0][sample] = busL;
        if (nOut > 1 && res1Ptr[1] != nullptr && res25Ptr[1] != nullptr)
        {
            res1Ptr[1][sample] = r1R;
            res25Ptr[1][sample] = busR;
        }
    }

    // Stereo width on II-VII
    if (nIn >= 2 && nOut >= 2 && res25Ptr[1] != nullptr)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float left = res25Ptr[0][sample];
            float right = res25Ptr[1][sample];
            float mid = (left + right) * 0.5f;
            float side = (left - right) * 0.5f * currentWidth;
            res25Ptr[0][sample] = mid + side;
            res25Ptr[1][sample] = mid - side;
        }
    }

    // Combine and output-smooth
    for (int channel = 0; channel < nOut; ++channel)
    {
        auto* wet = wetBuffer.getWritePointer(channel);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float combined = res1Buffer.getSample(channel, sample)
                + res2to5Buffer.getSample(channel, sample);
            outputSmoothing[channel] += smoothCoeff * (combined - outputSmoothing[channel]);
            wet[sample] = outputSmoothing[channel];
        }
    }
}

} // namespace CrispEngine
