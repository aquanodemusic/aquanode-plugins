#include "ReverbModule.h"

using namespace aquanode;

void ReverbModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reverb.setSampleRate (sr);
    reverb.reset();
    const int size = juce::jmax (1, (int) (sr * 0.25));   // 250 ms > max 200 ms pre-delay
    for (int c = 0; c < 2; ++c)
        preLine[c].assign ((size_t) size, 0.0f);
    writePos = 0;
}

void ReverbModule::blockStart()
{
    // decay 0.1..20 s mapped logarithmically to 0..1, blended into roomSize
    const float decayNorm = juce::jlimit (0.0f, 1.0f,
        std::log (param (pDecay) / 0.1f) / std::log (200.0f));

    juce::Reverb::Parameters p;
    p.roomSize   = juce::jlimit (0.0f, 1.0f, 0.35f * param (pRoomSize) * 0.01f + 0.63f * decayNorm);
    p.damping    = param (pDamping) * 0.01f;
    const float wet = param (pDryWet) * 0.01f;
    p.wetLevel   = wet;
    p.dryLevel   = 0.0f;    // dry mixed manually so pre-delay only affects the wet path
    p.width      = 1.0f;
    p.freezeMode = 0.0f;
    reverb.setParameters (p);
}

void ReverbModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int size = (int) preLine[0].size();
    const int delaySamples = juce::jlimit (0, size - 1, (int) (param (pPreDelay) * 0.001f * sampleRate));
    const float wet = param (pDryWet) * 0.01f;

    int readPos = writePos - delaySamples;
    if (readPos < 0) readPos += size;

    float l = preLine[0][(size_t) readPos];
    float r = preLine[1][(size_t) readPos];

    preLine[0][(size_t) writePos] = inputs[0][0];
    preLine[1][(size_t) writePos] = inputs[0][1];
    writePos = (writePos + 1) % size;

    reverb.processStereo (&l, &r, 1);   // wet-only (dryLevel = 0)

    outputs[0][0] = inputs[0][0] * (1.0f - wet) + l;
    outputs[0][1] = inputs[0][1] * (1.0f - wet) + r;
}

static ModuleDescriptor reverbDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.reverb";
    d.displayName = "Reverb";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 1;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("roomSize", "Room Size", 0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("decay",    "Decay",     0.1f, 20.0f, 2.0f, 0, "s", true),
        makeRotary ("damping",  "Damping",   0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("preDelay", "Pre-Delay", 0.0f, 200.0f, 20.0f, 0, "ms"),
        makeRotary ("dryWet",   "Dry/Wet",   0.0f, 100.0f, 30.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ReverbModule, reverbDescriptor)
