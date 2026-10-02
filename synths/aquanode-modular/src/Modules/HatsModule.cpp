#include "HatsModule.h"

using namespace aquanode;

void HatsModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // voice-steal de-click: a muted voice ramps out over a few ms instead of
    // being cut dead, and a re-used voice ramps back in
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void HatsModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
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

    // band-pass around Tone. A TPT state-variable filter: the Chamberlin form
    // used before went unstable at this damping once Tone passed ~70% (its
    // coefficient outgrew the damping), which with a long Decay ended in NaN.
    const float bpFreq = juce::jlimit (1000.0f, 12000.0f, 3000.0f + param (pTone) * 90.0f);
    const float g = std::tan (juce::MathConstants<float>::pi
                              * juce::jmin (bpFreq, (float) (sampleRate * 0.45)) / (float) sampleRate);
    constexpr float k = 1.2f;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1, a3 = g * a2;
    const float v3 = sum - bpLow[v];
    const float bandOut = a1 * bpBand[v] + a2 * v3;
    const float lowOut = bpLow[v] + a2 * bpBand[v] + a3 * v3;
    bpBand[v] = 2.0f * bandOut - bpBand[v];
    bpLow[v] = 2.0f * lowOut - bpLow[v];

    // one-pole high-pass at ~5 kHz to strip the body
    const float hpCoeff = std::exp ((float) (-2.0 * juce::MathConstants<double>::pi * 5000.0 / sampleRate));
    hpState[v] = hpCoeff * (hpState[v] + bandOut - hpPrevIn[v]);
    hpPrevIn[v] = bandOut;

    const float out = std::tanh (hpState[v] * 3.0f * env[v]);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor hatsDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.hats";
    d.displayName = "Hats";
    d.description =
        "606/808-style metallic percussion: six detuned squares summed, band-passed and shaped by "
        "a snappy decay - short Decay is a closed hat, long an open one. Trig In takes a Clock, "
        "Euclid or Step Seq gate; it also fires on MIDI note-on. Midi In plays it from notes instead (Keyboard Midi, Piano Roll, Arp...): every note is a hit, alongside Trig In.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 7;
    d.sockets = {
        modIn    ("trigIn",   "Trig In"),
        audioOut ("audioOut", "Audio Out"),
        midiIn   ("midiIn",   "Midi In")
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
