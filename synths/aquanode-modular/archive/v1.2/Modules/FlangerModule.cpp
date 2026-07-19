#include "FlangerModule.h"

using namespace aquanode;

void FlangerModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    const int size = juce::jmax (1, (int) (sr * 0.025));   // 25 ms > max 10 ms manual, doubled by depth
    for (int c = 0; c < 2; ++c)
        line[c].assign ((size_t) size, 0.0f);
    writePos = 0;
    lfoPhase = 0.0;
}

void FlangerModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int size = (int) line[0].size();
    if (size < 4)
    {
        outputs[0] = inputs[0];
        return;
    }

    const float rate = param (pRate);
    const float depth = param (pDepth) * 0.01f;
    const float manual = param (pManual);
    const float feedback = param (pFeedback) * 0.01f * 0.95f;
    const float wet = param (pDryWet) * 0.01f;

    lfoPhase += rate / sampleRate;
    lfoPhase -= std::floor (lfoPhase);
    const float lfo = (float) std::sin (lfoPhase * juce::MathConstants<double>::twoPi);

    const float delayMs = juce::jmax (0.05f, manual * (1.0f + depth * lfo));
    const double delaySamples = juce::jlimit (1.0, (double) size - 2.0, delayMs * 0.001 * sampleRate);

    for (int c = 0; c < 2; ++c)
    {
        double readPos = (double) writePos - delaySamples;
        while (readPos < 0.0) readPos += size;
        const int i0 = (int) readPos;
        const int i1 = (i0 + 1) % size;
        const float frac = (float) (readPos - i0);
        const float delayed = line[c][(size_t) i0] + (line[c][(size_t) i1] - line[c][(size_t) i0]) * frac;

        const float in = inputs[0][(size_t) c];
        line[c][(size_t) writePos] = juce::jlimit (-4.0f, 4.0f, in + delayed * feedback);
        outputs[0][(size_t) c] = in * (1.0f - wet) + delayed * wet;
    }

    writePos = (writePos + 1) % size;
}

static ModuleDescriptor flangerDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.flanger";
    d.displayName = "Flanger";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 4;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("rate",     "Rate",     0.1f, 10.0f, 0.25f, 0, "Hz", true),
        makeRotary ("depth",    "Depth",    0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("manual",   "Manual",   0.1f, 10.0f, 2.0f, 0, "ms"),
        makeRotary ("feedback", "Feedback", -100.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("dryWet",   "Dry/Wet",  0.0f, 100.0f, 50.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (FlangerModule, flangerDescriptor)
