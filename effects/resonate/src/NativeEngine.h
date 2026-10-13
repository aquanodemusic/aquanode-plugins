/*
  ==============================================================================
    NativeEngine.h  --  the ORIGINAL (v2) Resonate sound engine, kept verbatim
    as the "Native" engine option. Only renamed into its own namespace so it
    can run side by side with the Modelled engine (ResonatorCore.h).
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <cstdint>

namespace NativeEngine
{
enum FilterType { Lowpass = 0, Highpass, Bandpass, Notch };
enum NativeMode { ModeA = 0, ModeB = 1 };

struct NativeSVF
{
    float low[2] = { 0.0f, 0.0f };
    float band[2] = { 0.0f, 0.0f };
    float freq = 0.5f;
    float q = 0.7f;

    void  setFrequency(float normalizedFreq, float resonance);
    float process(float input, int channel, FilterType type);
    void  reset();
};

struct NativeResonate
{
    static constexpr int MAX_DELAY_SAMPLES = 8192;
    std::vector<float> delayBuffer[2];
    int writeIndex[2] = { 0, 0 };

    // Core comb-filter params
    double targetFrequency = 440.0;
    double delayInSamples = 100.0;
    double feedback = 0.9;
    int    pitchSemitones = 0;
    double fineDetune = 0.0;
    double gain = 0.0;
    bool   enabled = true;

    // Color/damping LPF
    float  lpfState[2] = { 0.0f, 0.0f };
    float  lpfCoeff = 1.0f;

    double currentSampleRate = 44100.0;

    // Membrane modulator (fixed-amount wobble)
    uint32_t noiseSeed[2] = { 12345u, 67890u };
    float    membraneFilter[2] = { 0.0f, 0.0f };
    double   membraneSamples = 0.4;

    // DC-offset accumulator (always-on bipolar leaky integrator)
    float  dcEnv[2] = { 0.0f, 0.0f };

    // Per-resonator decay/color/const
    double localDecay = 85.0;
    double localColor = 50.0;
    bool   localConst = false;
    bool   useLocalParams = false;

    void  prepare(double sampleRate);
    void  updateParameters(double sampleRate,
        double globalDecay, double globalNote,
        double color,
        NativeMode mode, bool constMode,
        int resonatorIndex,
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
// State Variable Filter (original v2 code)

inline void NativeSVF::setFrequency(float normalizedFreq, float resonance)
{
    freq = juce::jlimit(0.0f, 0.99f, normalizedFreq);
    q = juce::jlimit(0.1f, 10.0f, resonance);
}

inline float NativeSVF::process(float input, int channel, FilterType type)
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

inline void NativeSVF::reset()
{
    low[0] = low[1] = 0.0f;
    band[0] = band[1] = 0.0f;
}

//==============================================================================
// Resonator helpers

inline void NativeResonate::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
    for (int ch = 0; ch < 2; ++ch)
    {
        delayBuffer[ch].resize(MAX_DELAY_SAMPLES, 0.0f);
        writeIndex[ch] = 0;
        lpfState[ch] = 0.0f;
        membraneFilter[ch] = 0.0f;
        dcEnv[ch] = 0.0f;
    }
}

inline void NativeResonate::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill(delayBuffer[ch].begin(), delayBuffer[ch].end(), 0.0f);
        writeIndex[ch] = 0;
        lpfState[ch] = 0.0f;
        membraneFilter[ch] = 0.0f;
        dcEnv[ch] = 0.0f;
    }
}

inline void NativeResonate::updateParameters(
    double sampleRate,
    double globalDecay, double globalNote,
    double color,
    NativeMode mode, bool constMode,
    int resonatorIndex,
    bool   useLocal, double lDecay, double lColor, bool lConst,
    double midiNote)
{
    juce::ignoreUnused(resonatorIndex, mode);

    if (!enabled || sampleRate <= 0.0)
    {
        feedback = 0.0;
        return;
    }

    currentSampleRate = sampleRate;
    useLocalParams = useLocal;
    localDecay = lDecay;
    localColor = lColor;
    localConst = lConst;

    // Effective decay, color, const (local overrides global when per-res mode on)
    double effDecay = useLocalParams ? localDecay : globalDecay;
    double effColor = useLocalParams ? localColor : color;
    bool   effConst = useLocalParams ? localConst : constMode;

    // membraneSamples is a fixed constant now (always-on wobble, no chorus gate)

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
}

inline float NativeResonate::readDelayLinear(int channel, double delaySamples)
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

inline float NativeResonate::processA(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    double actualDelay = delayInSamples;

    if (membraneSamples > 0.001)
    {
        float noise = fastNoise(channel);
        membraneFilter[channel] += 0.0003f * (noise - membraneFilter[channel]);
        actualDelay += static_cast<double>(membraneFilter[channel]) * membraneSamples;
    }

    actualDelay = juce::jlimit(2.0, static_cast<double>(MAX_DELAY_SAMPLES - 2), actualDelay);

    float delayedSample = readDelayLinear(channel, actualDelay);
    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float dampedFeedback = lpfState[channel];

    float output = std::tanh(input + static_cast<float>(feedback) * dampedFeedback);

    // Clean DC offset BEFORE storing in the delay buffer
    dcEnv[channel] += 0.0005f * (output - dcEnv[channel]);
    output -= dcEnv[channel];

    delayBuffer[channel][writeIndex[channel]] = output;
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;

    float gainLinear = std::pow(10.0f, static_cast<float>(gain) / 20.0f);
    return output * gainLinear;
}

inline float NativeResonate::processB(float input, int channel)
{
    if (!enabled || channel < 0 || channel > 1)
        return 0.0f;

    double baseModeDelay = delayInSamples * 0.5;
    double actualDelay = std::max(1.0, baseModeDelay);

    if (membraneSamples > 0.001)
    {
        float noise = fastNoise(channel);
        membraneFilter[channel] += 0.0003f * (noise - membraneFilter[channel]);
        actualDelay += static_cast<double>(membraneFilter[channel]) * membraneSamples;
    }

    float delayedSample = readDelayLinear(channel, actualDelay);
    lpfState[channel] += lpfCoeff * (delayedSample - lpfState[channel]);
    float damped = lpfState[channel];

    float output = std::tanh(input - static_cast<float>(feedback) * damped);

    // Clean DC offset BEFORE storing in the delay buffer
    dcEnv[channel] += 0.0005f * (output - dcEnv[channel]);
    output -= dcEnv[channel];

    delayBuffer[channel][writeIndex[channel]] = output;
    if (++writeIndex[channel] >= MAX_DELAY_SAMPLES)
        writeIndex[channel] = 0;

    float gainLinear = std::pow(10.0f, static_cast<float>(gain) / 20.0f);
    return output * gainLinear;
}

} // namespace NativeEngine
