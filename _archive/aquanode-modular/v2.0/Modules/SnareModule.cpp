#include "SnareModule.h"

using namespace aquanode;

void SnareModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
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

void SnareModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float trigIn = inputs[0][0];
    if (trigIn > 0.5f && lastTrig[v] <= 0.5f)
        trigger (v);
    lastTrig[v] = trigIn;

    if (shellEnv[v] < 1.0e-5f && noiseEnv[v] < 1.0e-5f)
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    shellEnv[v] *= std::exp ((float) (-1.0 / (juce::jmax (5.0f, param (pShellDecay)) * 0.001 * sampleRate)));
    noiseEnv[v] *= std::exp ((float) (-1.0 / (juce::jmax (5.0f, param (pNoiseDecay)) * 0.001 * sampleRate)));

    // two shells a musical interval apart give the classic snare "body"
    const double f0 = param (pTune);
    phaseA[v] += f0 / sampleRate;
    phaseB[v] += f0 * 1.4983 / sampleRate;          // ~a fifth above
    phaseA[v] -= std::floor (phaseA[v]);
    phaseB[v] -= std::floor (phaseB[v]);

    const float shell = 0.5f * (float) (std::sin (phaseA[v] * juce::MathConstants<double>::twoPi)
                                      + std::sin (phaseB[v] * juce::MathConstants<double>::twoPi))
                        * shellEnv[v];

    // snare wires: noise through a Chamberlin band-pass set by Tone
    const float noise = random.nextFloat() * 2.0f - 1.0f;
    const float bpFreq = juce::jlimit (300.0f, 9000.0f, 800.0f + param (pTone) * 80.0f);
    const float f = 2.0f * std::sin (juce::MathConstants<float>::pi
                                     * juce::jmin (bpFreq, (float) (sampleRate * 0.22)) / (float) sampleRate);
    bpLow[v] += f * bpBand[v];
    const float hp = noise - bpLow[v] - bpBand[v] * 0.7f;
    bpBand[v] += f * hp;
    const float wires = bpBand[v] * noiseEnv[v];

    const float snap = param (pSnap) * 0.01f;
    float mix = shell * (1.0f - snap) + wires * snap * 1.6f;

    const float driveAmt = 1.0f + param (pDrive) * 0.01f * 5.0f;
    mix = std::tanh (mix * driveAmt) / std::tanh (driveAmt);

    outputs[0][0] = mix;
    outputs[0][1] = mix;
}

static ModuleDescriptor snareDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.snare";
    d.displayName = "Snare";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 8;
    d.sockets = {
        modIn    ("trigIn",   "Trig In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("tune",       "Tune",       80.0f, 400.0f, 180.0f, 0, "Hz", true),
        makeRotary ("shellDecay", "Shell Dec",  10.0f, 800.0f, 120.0f, 0, "ms", true),
        makeRotary ("noiseDecay", "Wire Dec",   10.0f, 1500.0f, 200.0f, 0, "ms", true),
        makeRotary ("snap",       "Snap",       0.0f, 100.0f, 60.0f, 1, "%"),
        makeRotary ("tone",       "Tone",       0.0f, 100.0f, 45.0f, 1, "%"),
        makeRotary ("drive",      "Drive",      0.0f, 100.0f, 15.0f, 1, "%"),
        makeRotary ("voices",     "Voices",     1.0f, (float) kMaxVoices, (float) kMaxVoices, 1, {}, false, 1.0f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SnareModule, snareDescriptor)
