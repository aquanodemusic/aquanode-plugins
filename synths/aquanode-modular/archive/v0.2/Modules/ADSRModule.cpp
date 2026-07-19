#include "ADSRModule.h"

using namespace aquanode;

void ADSRModule::handleMidiEvent (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        bool retrigger = false;
        const int v = voices.noteOn (m.getNoteNumber(), retrigger);
        stage[v] = Stage::Attack;
        if (! retrigger)
            level[v] = 0.0f;
    }
    else if (m.isNoteOff())
    {
        const int v = voices.noteOff (m.getNoteNumber());
        if (v >= 0)
        {
            stage[v] = Stage::Release;
            const float releaseSec = juce::jmax (0.001f, param (pRelease) * 0.001f);
            releaseStep[v] = level[v] / (releaseSec * (float) sampleRate);
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        voices.allGatesOff();
        for (int v = 0; v < VoiceAllocator::maxVoices; ++v)
            if (voices.slots[v].inUse && stage[v] != Stage::Idle && stage[v] != Stage::Release)
            {
                stage[v] = Stage::Release;
                const float releaseSec = juce::jmax (0.001f, param (pRelease) * 0.001f);
                releaseStep[v] = level[v] / (releaseSec * (float) sampleRate);
            }
    }
}

void ADSRModule::processSample (const StereoFrame*, StereoFrame* outputs)
{
    const float sr = (float) sampleRate;
    const float attackStep  = 1.0f / (juce::jmax (0.001f, param (pAttack) * 0.001f) * sr);
    const float sustain     = param (pSustain);
    const float decayStep   = juce::jmax (0.0f, 1.0f - sustain)
                              / (juce::jmax (0.001f, param (pDecay) * 0.001f) * sr);

    float sum = 0.0f;

    for (int v = 0; v < VoiceAllocator::maxVoices; ++v)
    {
        auto& slot = voices.slots[v];
        if (! slot.inUse)
            continue;

        // note released while still in A/D/S? move to release from the current level
        if (! slot.gate && stage[v] != Stage::Release)
        {
            stage[v] = Stage::Release;
            const float releaseSec = juce::jmax (0.001f, param (pRelease) * 0.001f);
            releaseStep[v] = level[v] / (releaseSec * sr);
        }

        switch (stage[v])
        {
            case Stage::Attack:
                level[v] += attackStep;
                if (level[v] >= 1.0f) { level[v] = 1.0f; stage[v] = Stage::Decay; }
                break;

            case Stage::Decay:
                level[v] -= decayStep;
                if (level[v] <= sustain) { level[v] = sustain; stage[v] = Stage::Sustain; }
                break;

            case Stage::Sustain:
                level[v] = sustain;
                break;

            case Stage::Release:
                level[v] -= releaseStep[v];
                if (level[v] <= 0.0f)
                {
                    level[v] = 0.0f;
                    stage[v] = Stage::Idle;
                    voices.freeVoice (v);
                }
                break;

            case Stage::Idle:
                break;
        }

        sum += level[v];
    }

    outputs[0][0] = sum;   // unipolar; multiple voices sum per spec
    outputs[0][1] = sum;
}

static ModuleDescriptor adsrDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.adsr";
    d.displayName = "ADSR";
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
