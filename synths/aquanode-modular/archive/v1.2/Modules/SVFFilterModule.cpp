#include "SVFFilterModule.h"

using namespace aquanode;

void SVFFilterModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float modIn = inputs[1][0];
    const float depth = param (pModDepth) * 0.01f;   // -1..+1

    // exponential (octave-based) cutoff modulation: +-5 octaves at full depth+signal
    const float cutoff = juce::jlimit (20.0f, 20000.0f,
        param (pCutoff) * std::pow (2.0f, depth * modIn * 5.0f));

    const float f = juce::jmin (1.4f, 2.0f * std::sin (juce::MathConstants<float>::pi
                    * juce::jmin (cutoff, (float) (sampleRate * 0.45)) / (float) sampleRate));
    const float q = 1.0f - param (pResonance) * 0.98f;
    const int mode = (int) param (pMode);   // 0 LP, 1 BP, 2 HP

    for (int c = 0; c < 2; ++c)
    {
        const float in = inputs[0][(size_t) c];
        low[v][c]  += f * band[v][c];
        const float high = in - low[v][c] - q * band[v][c];
        band[v][c] += f * high;

        // very light stabilisation
        low[v][c]  = juce::jlimit (-4.0f, 4.0f, low[v][c]);
        band[v][c] = juce::jlimit (-4.0f, 4.0f, band[v][c]);

        outputs[0][(size_t) c] = mode == 0 ? low[v][c] : (mode == 1 ? band[v][c] : high);
    }
}

static ModuleDescriptor svfDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.svf";
    d.displayName = "SVF Filter";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 0;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modIn ("cutoffMod", "Cutoff"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.2f, 0),
        makeCombo  ("mode",      "Filter Mode", { "Lowpass", "Bandpass", "Highpass" }, 0, 0, 2),
        makeRotary ("modDepth",  "Mod Depth", -100.0f, 100.0f, 0.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SVFFilterModule, svfDescriptor)
