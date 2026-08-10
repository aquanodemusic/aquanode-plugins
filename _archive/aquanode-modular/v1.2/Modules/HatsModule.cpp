#include "HatsModule.h"

using namespace aquanode;

void HatsModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    if (pool.isMuted (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }
    const float trigIn = inputs[0][0];
    if (trigIn > 0.5f && lastTrig[v] <= 0.5f)
        env[v] = 1.0f;
    lastTrig[v] = trigIn;

    if (env[v] < 1.0e-5f)
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    env[v] *= std::exp ((float) (-1.0 / (juce::jmax (5.0f, param (pDecay)) * 0.001 * sampleRate)));

    // the classic 808 cymbal oscillator bank, spread further by Metal
    static const double baseFreqs[kNumOscs] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    const double tune = param (pTune);
    const double metal = 1.0 + param (pMetal) * 0.01 * 0.5;

    float sum = 0.0f;
    for (int o = 0; o < kNumOscs; ++o)
    {
        const double freq = baseFreqs[o] * std::pow (metal, (double) o) * tune / 205.3;
        phase[v][o] += freq / sampleRate;
        phase[v][o] -= std::floor (phase[v][o]);
        sum += phase[v][o] < 0.5 ? 1.0f : -1.0f;
    }
    sum *= 1.0f / kNumOscs;

    // band-pass (Chamberlin SVF) around Tone
    const float bpFreq = juce::jlimit (1000.0f, 12000.0f, 3000.0f + param (pTone) * 90.0f);
    const float f = 2.0f * std::sin (juce::MathConstants<float>::pi
                                     * juce::jmin (bpFreq, (float) (sampleRate * 0.22)) / (float) sampleRate);
    bpLow[v]  += f * bpBand[v];
    const float hpTmp = sum - bpLow[v] - bpBand[v] * 1.2f;
    bpBand[v] += f * hpTmp;

    // one-pole high-pass at ~5 kHz to strip the body
    const float hpCoeff = std::exp ((float) (-2.0 * juce::MathConstants<double>::pi * 5000.0 / sampleRate));
    hpState[v] = hpCoeff * (hpState[v] + bpBand[v] - hpPrevIn[v]);
    hpPrevIn[v] = bpBand[v];

    const float out = std::tanh (hpState[v] * 3.0f * env[v]);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor hatsDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.hats";
    d.displayName = "Hats";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 7;
    d.sockets = {
        modIn    ("trigIn",   "Trig In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("tune",  "Tune",  100.0f, 500.0f, 205.0f, 0, "Hz", true),
        makeRotary ("decay", "Decay", 10.0f, 2000.0f, 120.0f, 0, "ms", true),
        makeRotary ("tone",  "Tone",  0.0f, 100.0f, 60.0f, 0, "%"),
        makeRotary ("metal", "Metal", 0.0f, 100.0f, 20.0f, 0, "%")
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 1, {}, false, 1.0f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (HatsModule, hatsDescriptor)
