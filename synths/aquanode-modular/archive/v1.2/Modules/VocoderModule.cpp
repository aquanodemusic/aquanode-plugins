#include "VocoderModule.h"

using namespace aquanode;

void VocoderModule::rebuildBank()
{
    static const int bandCounts[3] = { 8, 16, 32 };
    numBands = bandCounts[juce::jlimit (0, 2, (int) param (pBands))];

    // log-spaced centres, 90 Hz .. 7.5 kHz
    const float loHz = 90.0f, hiHz = 7500.0f;
    const float ratio = std::pow (hiHz / loHz, 1.0f / (float) (numBands - 1));

    // Q proportional to band spacing so neighbours meet cleanly
    const float q = 1.0f / (std::sqrt (ratio) - 1.0f / std::sqrt (ratio));

    float fc = loHz;
    for (int b = 0; b < numBands; ++b)
    {
        for (int o = 0; o < kOrder; ++o)
        {
            modFilters[b][o].setBandpass (fc, q, (float) sampleRate);
            carFiltersL[b][o].setBandpass (fc, q, (float) sampleRate);
            carFiltersR[b][o].setBandpass (fc, q, (float) sampleRate);
        }
        bandGain[b] = fc;   // stash the centre; Bright tilt applied per sample
        envelope[b] = 0.0f;
        fc *= ratio;
    }

    reset();
}

void VocoderModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float modMono = 0.5f * (inputs[1][0] + inputs[1][1]);

    const float attCoeff = 1.0f - std::exp ((float) (-1.0 / (juce::jmax (0.1f, param (pAttack))  * 0.001 * sampleRate)));
    const float relCoeff = 1.0f - std::exp ((float) (-1.0 / (juce::jmax (1.0f, param (pRelease)) * 0.001 * sampleRate)));
    const float bright = param (pBright) * 0.01f;
    const float level = param (pLevel);

    float outL = 0.0f, outR = 0.0f;

    for (int b = 0; b < numBands; ++b)
    {
        // modulator band -> envelope
        float m = modMono;
        for (int o = 0; o < kOrder; ++o)
            m = modFilters[b][o].process (m);

        const float rectified = std::abs (m);
        envelope[b] += (rectified > envelope[b] ? attCoeff : relCoeff) * (rectified - envelope[b]);

        // carrier band, shaped by the envelope
        float cl = inputs[0][0], cr = inputs[0][1];
        for (int o = 0; o < kOrder; ++o)
        {
            cl = carFiltersL[b][o].process (cl);
            cr = carFiltersR[b][o].process (cr);
        }

        // Bright: up to +12 dB tilt toward the top band
        const float tilt = std::pow (10.0f, bright * 12.0f * (float) b / (float) (numBands - 1) / 20.0f);
        const float g = envelope[b] * tilt;

        outL += cl * g;
        outR += cr * g;
    }

    const float makeup = 2.0f * level;
    outputs[0][0] = std::tanh (outL * makeup);
    outputs[0][1] = std::tanh (outR * makeup);
}

static ModuleDescriptor vocoderDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.vocode";
    d.displayName = "Vocoder";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 15;
    d.sockets = {
        audioIn  ("carrierIn", "Carrier In"),
        audioIn  ("modIn",     "Mod In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeCombo  ("bands",   "Bands",   { "8", "16", "32" }, 1, 0, 1),
        makeRotary ("attack",  "Attack",  0.1f, 100.0f, 3.0f, 0, "ms", true),
        makeRotary ("release", "Release", 1.0f, 500.0f, 40.0f, 0, "ms", true),
        makeRotary ("bright",  "Bright",  0.0f, 100.0f, 40.0f, 0, "%"),
        makeRotary ("level",   "Level",   0.0f, 4.0f, 1.0f, 1, {}, true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (VocoderModule, vocoderDescriptor)
