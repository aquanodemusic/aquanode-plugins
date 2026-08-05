#include "LadderFilterModule.h"

using namespace aquanode;

void LadderFilterModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float modIn = inputs[1][0];
    const float depth = param (pModDepth) * 0.01f;

    const float baseCutoff = param (pCutoff);
    const float cutoff = juce::jlimit (20.0f, 20000.0f,
        baseCutoff * std::pow (2.0f, depth * modIn * 5.0f));

    const float g = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                    * juce::jmin (cutoff, (float) (sampleRate * 0.45)) / (float) sampleRate);
    const float k = param (pResonance) * 4.4f;   // > ~4 self-oscillates (res near/above ~0.9)
    const float drive = param (pDrive);
    const float outVol = param (pOutputVolume);
    const int mode = (int) param (pMode);        // 0=12LP 1=24LP 2=12HP 3=24HP

    for (int c = 0; c < 2; ++c)
    {
        const float in = inputs[0][(size_t) c];
        const float x = std::tanh (drive * in - k * s4[c]);

        s1[c] += g * (x     - s1[c]);
        s2[c] += g * (s1[c] - s2[c]);
        s3[c] += g * (s2[c] - s3[c]);
        s4[c] += g * (s3[c] - s4[c]);

        float y = 0.0f;
        switch (mode)
        {
            case 0: y = s2[c];      break;   // 12dB LP
            case 1: y = s4[c];      break;   // 24dB LP
            case 2: y = x - s2[c];  break;   // 12dB HP
            case 3: y = x - s4[c];  break;   // 24dB HP
        }
        outputs[0][(size_t) c] = y * outVol;
    }
}

static ModuleDescriptor ladderDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.ladder";
    d.displayName = "Ladder Filter";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 1;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modIn ("cutoffMod", "Cutoff"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.2f, 0),
        makeRotary ("drive",     "Drive",     1.0f, 5.0f, 1.0f, 0),
        makeRotary ("modDepth",  "Mod Depth", -100.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("outputVolume", "Out Vol", 0.0f, 1.2f, 1.0f, 0),
        makeCombo  ("mode", "Filter Mode", { "12dB LP", "24dB LP", "12dB HP", "24dB HP" }, 1, 1, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (LadderFilterModule, ladderDescriptor)
