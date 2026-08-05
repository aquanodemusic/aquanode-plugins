#include "CenterCombModule.h"

using namespace aquanode;

void CenterCombModule::blockStart()
{
    const float freq = param (pFreq);
    const float gainDb = param (pGain);
    const int count = juce::jlimit (0, maxPairs, (int) param (pCount));
    const float spread = param (pSpread);
    const float q = param (pQ);
    const float damp = param (pDamp) * 0.01f;

    if (freq == lastFreq && gainDb == lastGain && count == lastCount
        && spread == lastSpread && q == lastQ && damp == lastDamp)
        return;

    lastFreq = freq; lastGain = gainDb; lastCount = count;
    lastSpread = spread; lastQ = q; lastDamp = damp;

    const float lowLimit = 20.0f;
    const float highLimit = (float) (sampleRate * 0.475);

    activeFilters = count * 2 + 1;

    for (int i = 0; i < maxFilters; ++i)
    {
        auto& cf = coeffs[i];
        cf.active = false;

        if (i >= activeFilters)
            continue;

        const int offsetIdx = i - count;
        const float f = freq + (float) offsetIdx * spread;   // Regular (linear) spread
        if (f < lowLimit || f > highLimit)
            continue;

        // dampening: peaks further from the centre get less gain
        float weight = 1.0f - damp * (std::abs ((float) offsetIdx) / (float) (count + 1));
        weight = juce::jmax (0.0f, weight);
        if (weight <= 0.001f)
            continue;

        // RBJ peaking EQ
        const float A = std::pow (10.0f, gainDb * weight / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * f / (float) sampleRate;
        const float alpha = std::sin (w0) / (2.0f * juce::jmax (0.1f, q));
        const float cosw = std::cos (w0);

        const float a0 = 1.0f + alpha / A;
        cf.b0 = (1.0f + alpha * A) / a0;
        cf.b1 = (-2.0f * cosw) / a0;
        cf.b2 = (1.0f - alpha * A) / a0;
        cf.a1 = (-2.0f * cosw) / a0;
        cf.a2 = (1.0f - alpha / A) / a0;
        cf.active = true;
    }
}

void CenterCombModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    for (int c = 0; c < 2; ++c)
    {
        float x = inputs[0][(size_t) c];

        // filters in series, like the original
        for (int i = 0; i < activeFilters; ++i)
        {
            const auto& cf = coeffs[i];
            if (! cf.active)
                continue;

            const float y = cf.b0 * x + cf.b1 * x1[c][i] + cf.b2 * x2[c][i]
                            - cf.a1 * y1[c][i] - cf.a2 * y2[c][i];
            x2[c][i] = x1[c][i];  x1[c][i] = x;
            y2[c][i] = y1[c][i];  y1[c][i] = juce::jlimit (-8.0f, 8.0f, y);
            x = y1[c][i];
        }

        outputs[0][(size_t) c] = x;
    }
}

static ModuleDescriptor centerCombDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.centercomb";
    d.displayName = "Comb Filter";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 2;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("freq",   "Center",  20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("gain",   "Gain",    -24.0f, 24.0f, 6.0f, 0, "dB"),
        makeRotary ("count",  "Count",   0.0f, 16.0f, 2.0f, 0, {}, false, 1.0f),
        makeRotary ("spread", "Spread",  10.0f, 2000.0f, 100.0f, 0, "Hz", true),
        makeRotary ("q",      "Q",       0.5f, 100.0f, 10.0f, 0, {}, true),
        makeRotary ("damp",   "Damp",    0.0f, 100.0f, 50.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CenterCombModule, centerCombDescriptor)
