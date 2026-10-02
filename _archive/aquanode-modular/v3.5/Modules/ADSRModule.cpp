#include "ADSRModule.h"

using namespace aquanode;

void ADSRModule::voiceNoteOn (int v, int note, bool retrigger)
{
    juce::ignoreUnused (note, retrigger);
    // (re)start the attack; on retrigger the level continues from where it is,
    // so repeated hits swell smoothly instead of clicking to zero
    stage[v] = Stage::Attack;
}

void ADSRModule::voiceNoteOff (int v)
{
    if (stage[v] != Stage::Idle)
    {
        stage[v] = Stage::Release;
        const float releaseSec = juce::jmax (0.001f, param (pRelease) * 0.001f);
        releaseStep[v] = level[v] / (releaseSec * (float) sampleRate);
    }
}

void ADSRModule::processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs)
{
    const float sr = (float) sampleRate;
    const float sustain = param (pSustain);

    switch (stage[v])
    {
        case Stage::Attack:
        {
            const float attackStep = 1.0f / (juce::jmax (0.001f, param (pAttack) * 0.001f) * sr);
            level[v] += attackStep;
            if (level[v] >= 1.0f) { level[v] = 1.0f; stage[v] = Stage::Decay; }
            break;
        }

        case Stage::Decay:
        {
            const float decayStep = juce::jmax (0.0f, 1.0f - sustain)
                                    / (juce::jmax (0.001f, param (pDecay) * 0.001f) * sr);
            level[v] -= decayStep;
            if (level[v] <= sustain) { level[v] = sustain; stage[v] = Stage::Sustain; }
            break;
        }

        case Stage::Sustain:
            level[v] = sustain;
            break;

        case Stage::Release:
            level[v] -= releaseStep[v];
            if (level[v] <= 0.0f) { level[v] = 0.0f; stage[v] = Stage::Idle; }
            break;

        case Stage::Idle:
            break;
    }

    outputs[0][0] = level[v];   // this voice's envelope, unipolar [0,1]
    outputs[0][1] = level[v];
}

static ModuleDescriptor adsrDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.adsr";
    d.displayName = "ADSR";
    d.description =
        "A per-voice envelope generator, unipolar 0..1: its Mod Out is that voice's own envelope, "
        "so patching it into an Oscillator's Env In gives true per-note amplitude shaping. The "
        "other half of nearly every generator patch.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 1;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = {
        makeRotary ("attack",  "Attack",  1.0f, 20000.0f, 10.0f,  0, "ms", true),
        makeRotary ("decay",   "Decay",   1.0f, 20000.0f, 100.0f, 0, "ms", true),
        makeRotary ("sustain", "Sustain", 0.0f, 1.0f, 0.7f, 0),
        makeRotary ("release", "Release", 1.0f, 20000.0f, 300.0f, 0, "ms", true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ADSRModule, adsrDescriptor)
