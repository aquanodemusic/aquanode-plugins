#include "ThreeBandCompModule.h"

using namespace aquanode;

void ThreeBandCompModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    juce::dsp::ProcessSpec spec { sr, 512, 2 };
    splitLow.prepare (spec);
    splitHigh.prepare (spec);
    splitLow.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    splitHigh.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    lastLowX = lastHighX = -1.0f;
    reset();
}

void ThreeBandCompModule::reset()
{
    splitLow.reset();
    splitHigh.reset();
    for (auto& e : env) e = 0.0f;
}

void ThreeBandCompModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    // crossover frequencies: refreshed every 64 samples, only when changed
    if (--coeffCounter <= 0)
    {
        coeffCounter = 64;
        const float lx = juce::jlimit (40.0f, 1000.0f, param (pLowX));
        const float hx = juce::jlimit (lx * 2.0f, 12000.0f, param (pHighX));
        if (lx != lastLowX)  { splitLow.setCutoffFrequency (lx);  lastLowX = lx; }
        if (hx != lastHighX) { splitHigh.setCutoffFrequency (hx); lastHighX = hx; }
    }

    const float inG = juce::Decibels::decibelsToGain (param (pInGain));
    float band[3][2];
    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = inputs[0][ch] * inG;
        float lo = 0.0f, rest = 0.0f, mid = 0.0f, hi = 0.0f;
        splitLow.processSample (ch, x, lo, rest);
        splitHigh.processSample (ch, rest, mid, hi);
        band[0][ch] = lo; band[1][ch] = mid; band[2][ch] = hi;
    }

    const float depth = param (pDepth) * 0.01f;
    const float timeScale = juce::jmax (0.02f, param (pTime) * 0.01f);
    const float att = 1.0f - std::exp ((float) (-1.0 / (0.0135 * timeScale * sampleRate)));
    const float rel = 1.0f - std::exp ((float) (-1.0 / (0.132 * timeScale * sampleRate)));
    const float up = param (pUpward) * 0.01f;
    const float down = param (pDownward) * 0.01f;

    // thresholds in dBFS per band (low, mid, high): the high band sits lower,
    // since there is far less energy up there in a bass patch
    static const float upperT[3] = { -26.0f, -28.0f, -32.0f };
    static const float lowerT[3] = { -40.0f, -42.0f, -46.0f };
    const float downRatio = 1.0f + down * 11.0f;          // up to 12:1
    const float upRatio   = 1.0f + up * 3.0f;             // up to 4:1 upward
    const float bandGain[3] = { param (pLowGain), param (pMidGain), param (pHighGain) };

    float outL = 0.0f, outR = 0.0f;
    for (int b = 0; b < 3; ++b)
    {
        const float pk = juce::jmax (std::abs (band[b][0]), std::abs (band[b][1]));
        env[b] += (pk > env[b] ? att : rel) * (pk - env[b]);
        const float lvl = juce::Decibels::gainToDecibels (env[b], -100.0f);

        float g = 0.0f;
        if (lvl > upperT[b])
            g = (upperT[b] + (lvl - upperT[b]) / downRatio) - lvl;
        else if (lvl < lowerT[b] && lvl > -75.0f)
            g = juce::jmin (24.0f, (lowerT[b] - (lowerT[b] - lvl) / upRatio) - lvl);

        const float gain = juce::Decibels::decibelsToGain (g * depth + bandGain[b] * depth);
        outL += band[b][0] * gain;
        outR += band[b][1] * gain;
    }

    const float outG = juce::Decibels::decibelsToGain (param (pOutGain));
    outputs[0] = { outL * outG, outR * outG };
}

static ModuleDescriptor threeBandCompDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.threebandcomp";
    d.displayName = "3-Band Compressor";
    d.description =
        "Three-band upward and downward compression: each band is pulled down above its upper threshold "
        "and pushed up below its lower one, so quiet unison tails and the gaps in a chop come forward "
        "and the peaks flatten - the loud, glued finish of a colour-bass patch. Depth scales the whole "
        "effect; Time scales attack and release together.";
    d.section = ModuleSection::Effect;
    d.family = ModuleFamily::Colorbass;
    d.sidebarOrder = 3;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("depth",    "Depth",    0.0f, 100.0f, 60.0f, 0, "%"),
        makeRotary ("time",     "Time",     0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("upward",   "Upward",   0.0f, 100.0f, 70.0f, 0, "%"),
        makeRotary ("downward", "Downward", 0.0f, 100.0f, 80.0f, 0, "%"),
        makeRotary ("lowGain",  "Low",      -12.0f, 18.0f, 4.0f, 1, "dB"),
        makeRotary ("midGain",  "Mid",      -12.0f, 18.0f, 0.0f, 1, "dB"),
        makeRotary ("highGain", "High",     -12.0f, 18.0f, 4.0f, 1, "dB"),
        makeRotary ("inGain",   "In",       -24.0f, 24.0f, 0.0f, 1, "dB"),
        makeRotary ("outGain",  "Out",      -24.0f, 24.0f, 0.0f, 1, "dB"),
        makeRotary ("lowX",     "Low X",    40.0f, 1000.0f, 120.0f, 2, "Hz", true),
        makeRotary ("highX",    "High X",   1000.0f, 12000.0f, 2500.0f, 2, "Hz", true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ThreeBandCompModule, threeBandCompDescriptor)
