#include "KickModule.h"

using namespace aquanode;

void KickModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
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

void KickModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // rising edge at Trig In fires the drum (in either lane)
    const float trigIn = inputs[0][0];
    if (trigIn > 0.5f && lastTrig[v] <= 0.5f)
        trigger (v);
    lastTrig[v] = trigIn;

    if (ampEnv[v] < 1.0e-5f && clickSamples[v] <= 0)
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    // envelopes (exponential decays)
    const float ampCoeff   = std::exp ((float) (-1.0 / (juce::jmax (5.0f, param (pDecay))      * 0.001 * sampleRate)));
    const float pitchCoeff = std::exp ((float) (-1.0 / (juce::jmax (2.0f, param (pPitchDecay)) * 0.001 * sampleRate)));
    ampEnv[v]   *= ampCoeff;
    pitchEnv[v] *= pitchCoeff;

    // pitch: tune plus a drop of up to pDrop semitones that decays away
    const double freq = param (pTune) * std::pow (2.0, (double) (param (pDrop) * pitchEnv[v]) / 12.0);
    phase[v] += freq / sampleRate;
    phase[v] -= std::floor (phase[v]);

    float body = (float) std::sin (phase[v] * juce::MathConstants<double>::twoPi) * ampEnv[v];

    // click transient
    if (clickSamples[v] > 0)
    {
        --clickSamples[v];
        body += (random.nextFloat() * 2.0f - 1.0f) * param (pClick) * 0.01f * 0.8f;
    }

    // drive (gain-compensated tanh)
    const float driveAmt = 1.0f + param (pDrive) * 0.01f * 6.0f;
    const float out = std::tanh (body * driveAmt) / std::tanh (driveAmt);

    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor kickDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.kick";
    d.displayName = "Kick";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 4;
    d.sockets = {
        modIn    ("trigIn",   "Trig In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("tune",       "Tune",     25.0f, 200.0f, 50.0f, 0, "Hz", true),
        makeRotary ("drop",       "Drop",     0.0f, 48.0f, 24.0f, 0, "st"),
        makeRotary ("pitchDecay", "P Decay",  2.0f, 500.0f, 40.0f, 0, "ms", true),
        makeRotary ("decay",      "Decay",    20.0f, 2000.0f, 400.0f, 0, "ms", true),
        makeRotary ("click",      "Click",    0.0f, 100.0f, 30.0f, 1, "%"),
        makeRotary ("drive",      "Drive",    0.0f, 100.0f, 20.0f, 1, "%")
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 1, {}, false, 1.0f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (KickModule, kickDescriptor)
