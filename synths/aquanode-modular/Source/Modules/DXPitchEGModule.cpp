#include "DXPitchEGModule.h"
#include "DXDisplays.h"

using namespace aquanode;

void DXPitchEGModule::configure (int v)
{
    uint8_t r[4], l[4];
    for (int i = 0; i < 4; ++i)
    {
        r[i] = (uint8_t) juce::jlimit (0, 99, (int) std::lround (param (pR1 + i)));
        l[i] = (uint8_t) juce::jlimit (0, 99, (int) std::lround (param (pL1 + i)));
    }
    // configure() only re-aims the running envelope (it keeps its position),
    // so a knob move under a held note bends the pitch instead of jumping
    env[v].configure (r, l);
}

void DXPitchEGModule::voiceNoteOn (int v, int note, bool retrigger)
{
    juce::ignoreUnused (note, retrigger);
    configure (v);
    env[v].keyOn();
    current[v] = env[v].tick (0.0f);   // starts at L4
    step[v] = 0.0f;
    ctrlLeft[v] = 0;
}

void DXPitchEGModule::processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs)
{
    // control rate, as in the engine, with the pitch ramped linearly across
    // the block so the steps never zipper
    if (--ctrlLeft[v] < 0)
    {
        configure (v);
        const float target = env[v].tick ((float) kCtrlBlock / (float) sampleRate);
        step[v] = (target - current[v]) / (float) kCtrlBlock;
        ctrlLeft[v] = kCtrlBlock - 1;
    }
    current[v] += step[v];

    const float semis = current[v] + param (pTranspose);
    const float out = semis / 60.0f;   // KeyTrack convention
    outputs[0] = { out, out };
}

std::unique_ptr<juce::Component> DXPitchEGModule::createExtraContentComponent()
{
    return std::make_unique<dxui::PitchEnvView> (*this);
}

//==============================================================================
static ModuleDescriptor dxPitchEGDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.dxpitcheg";
    d.displayName = "DX Pitch EG";
    d.description =
        "The DX7's pitch envelope: four rates and four levels, level 50 is the played pitch. It starts "
        "each note at L4 and returns there on release, with the DX7's own curve (fine around the centre, "
        "about four octaves at the ends). Transpose adds a fixed offset. Patch Pitch Out into every DX "
        "Operator's Pitch In (KeyTrack scaling, so it bends any other Pitch In too).";
    d.section = ModuleSection::Utility;
    d.family = ModuleFamily::DX;
    d.sidebarOrder = 40;
    d.sockets = { modOut ("pitchOut", "Pitch Out") };
    d.params = {
        makeRotary ("r1", "R1", 0.0f, 99.0f, 99.0f, 0, {}, false, 1.0f),
        makeRotary ("r2", "R2", 0.0f, 99.0f, 99.0f, 0, {}, false, 1.0f),
        makeRotary ("r3", "R3", 0.0f, 99.0f, 99.0f, 0, {}, false, 1.0f),
        makeRotary ("r4", "R4", 0.0f, 99.0f, 99.0f, 0, {}, false, 1.0f),
        makeRotary ("transpose", "Transpose", -24.0f, 24.0f, 0.0f, 0, "st", false, 1.0f),
        makeRotary ("l1", "L1", 0.0f, 99.0f, 50.0f, 1, {}, false, 1.0f),
        makeRotary ("l2", "L2", 0.0f, 99.0f, 50.0f, 1, {}, false, 1.0f),
        makeRotary ("l3", "L3", 0.0f, 99.0f, 50.0f, 1, {}, false, 1.0f),
        makeRotary ("l4", "L4", 0.0f, 99.0f, 50.0f, 1, {}, false, 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DXPitchEGModule, dxPitchEGDescriptor)
