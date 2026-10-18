#include "VocoderModule.h"

using namespace aquanode;

void VocoderModule::rebuildBank()
{
    static const int bandCounts[3] = { 8, 16, 32 };
    numBands = bandCounts[juce::jlimit (0, 2, (int) param (pBands))];

    // Two banks, laid out independently. Sliding the carrier's centre away
    // from the modulator's is what makes a vocoder sound like a robot rather
    // than like speech: the formants land in the wrong place on purpose.
    const auto layOut = [this] (float centreHz, float spread, float qScale,
                                Biquad (*banks)[kOrder], Biquad (*banksR)[kOrder])
    {
        // spread is how far the bank reaches either side of its centre
        const float halfSpan = juce::jlimit (1.2f, 12.0f, 1.5f + spread * 0.06f);
        const float loHz = juce::jlimit (40.0f, 4000.0f, centreHz / halfSpan);
        const float hiHz = juce::jlimit (200.0f, (float) (sampleRate * 0.45), centreHz * halfSpan);

        const float ratio = std::pow (hiHz / loHz, 1.0f / (float) (numBands - 1));

        // Q proportional to band spacing so neighbours meet cleanly, then
        // scaled: narrow bands are articulate, wide ones are smeary and big
        const float q = qScale / (std::sqrt (ratio) - 1.0f / std::sqrt (ratio));

        float fc = loHz;

        for (int b = 0; b < numBands; ++b)
        {
            for (int o = 0; o < kOrder; ++o)
            {
                banks[b][o].setBandpass (fc, q, (float) sampleRate);

                if (banksR != nullptr)
                    banksR[b][o].setBandpass (fc, q, (float) sampleRate);
            }

            if (banksR != nullptr)
                bandGain[b] = fc;   // stash the carrier centre for the tilt

            fc *= ratio;
        }
    };

    layOut (param (pModCentre), param (pModSpread), juce::jmax (0.2f, param (pModQ)),
            modFilters, nullptr);

    layOut (param (pCarrierCentre), param (pCarrierSpread), juce::jmax (0.2f, param (pCarrierQ)),
            carFiltersL, carFiltersR);

    // New coefficients take over smoothly; only a change in the NUMBER of
    // bands (which reshuffles every band) starts the filters from silence.
    if (numBands != builtBands)
    {
        builtBands = numBands;
        reset();
    }
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
    d.description =
        "The Mod In signal's per-band envelope shapes the same bands of the Carrier In signal, in "
        "8, 16 or 32 bands. Carrier In wants something bright and harmonically rich (a saw "
        "Oscillator); Mod In wants speech from Audio In, or drums for rhythmic pads. Bright tilts "
        "the bands toward the highs for intelligibility.";
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
        makeRotary ("level",   "Level",   0.0f, 4.0f, 1.0f, 1, {}, true),
        makeRotary ("carrierCentre", "Carrier Freq", 100.0f, 5000.0f, 850.0f, 2, "Hz", true),
        makeRotary ("modCentre",     "Mod Freq",     100.0f, 5000.0f, 850.0f, 2, "Hz", true),
        makeRotary ("carrierSpread", "Carrier Spread", 0.0f, 100.0f, 60.0f, 2, "%"),
        makeRotary ("modSpread",     "Mod Spread",     0.0f, 100.0f, 60.0f, 2, "%"),
        makeRotary ("carrierQ",      "Carrier Q",      0.2f, 6.0f, 1.0f, 3),
        makeRotary ("modQ",          "Mod Q",          0.2f, 6.0f, 1.0f, 3)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (VocoderModule, vocoderDescriptor)
