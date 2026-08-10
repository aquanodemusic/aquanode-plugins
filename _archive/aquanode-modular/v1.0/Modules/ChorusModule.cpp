#include "ChorusModule.h"

using namespace aquanode;

void ChorusModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    const int size = juce::jmax (1, (int) (sr * 0.06));   // 60 ms > max 20 ms base + modulation
    for (int c = 0; c < 2; ++c)
        line[c].assign ((size_t) size, 0.0f);
    writePos = 0;
    lfoPhase = 0.0;
}

void ChorusModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int size = (int) line[0].size();
    if (size < 4)
    {
        outputs[0] = inputs[0];
        return;
    }

    const float rate = param (pRate);
    const float depth = param (pDepth) * 0.01f;
    const float baseMs = param (pBaseDelay);
    const float spread = param (pSpread) * 0.01f;
    const float wet = param (pDryWet) * 0.01f;

    lfoPhase += rate / sampleRate;
    lfoPhase -= std::floor (lfoPhase);

    for (int c = 0; c < 2; ++c)
    {
        const double ph = lfoPhase + (c == 1 ? spread * 0.5 : 0.0);   // up to 180 deg offset
        const float lfo = (float) std::sin (ph * juce::MathConstants<double>::twoPi);

        // sweep around the base delay; never below 0.5 ms
        const float delayMs = juce::jmax (0.5f, baseMs * (1.0f + 0.9f * depth * lfo));
        const double delaySamples = juce::jlimit (1.0, (double) size - 2.0, delayMs * 0.001 * sampleRate);

        double readPos = (double) writePos - delaySamples;
        while (readPos < 0.0) readPos += size;
        const int i0 = (int) readPos;
        const int i1 = (i0 + 1) % size;
        const float frac = (float) (readPos - i0);
        const float delayed = line[c][(size_t) i0] + (line[c][(size_t) i1] - line[c][(size_t) i0]) * frac;

        const float in = inputs[0][(size_t) c];
        line[c][(size_t) writePos] = in;
        outputs[0][(size_t) c] = in * (1.0f - wet) + delayed * wet;
    }

    writePos = (writePos + 1) % size;
}

static ModuleDescriptor chorusDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.chorus";
    d.displayName = "Chorus";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 2;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("rate",      "Rate",       0.1f, 10.0f, 0.5f, 0, "Hz", true),
        makeRotary ("depth",     "Depth",      0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("baseDelay", "Base Delay", 1.0f, 20.0f, 7.0f, 0, "ms"),
        makeRotary ("spread",    "Spread",     0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("dryWet",    "Dry/Wet",    0.0f, 100.0f, 50.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ChorusModule, chorusDescriptor)
