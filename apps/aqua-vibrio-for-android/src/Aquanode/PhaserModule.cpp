#include "PhaserModule.h"

using namespace aquanode;

void PhaserModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    for (int c = 0; c < 2; ++c)
    {
        lastOut[c] = 0.0f;
        for (auto& s : apState[c]) s = 0.0f;
    }
    lfoPhase = 0.0;
}

void PhaserModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float rate = param (pRate);
    const float depth = param (pDepth) * 0.01f;
    const float centre = param (pCentre);
    const float feedback = param (pFeedback) * 0.01f * 0.9f;
    const float wet = param (pDryWet) * 0.01f;

    static const int stageChoices[] = { 2, 4, 6, 8, 10, 12 };
    const int stages = stageChoices[juce::jlimit (0, 5, (int) param (pStages))];

    lfoPhase += rate / sampleRate;
    lfoPhase -= std::floor (lfoPhase);

    // Spread walks the right channel's LFO around the cycle away from the
    // left's. At the top the two sides sweep in opposition, which is what
    // makes a phaser move across the image rather than in front of it.
    const float spread = juce::jlimit (0.0f, 1.0f, param (pSpread) * 0.01f);

    for (int c = 0; c < 2; ++c)
    {
        double ph = lfoPhase + (c == 1 ? spread * 0.5 : 0.0);
        ph -= std::floor (ph);

        const float lfo = (float) std::sin (ph * juce::MathConstants<double>::twoPi);

        // sweep +-2 octaves around centre at full depth
        const float sweepHz = juce::jlimit (30.0f, (float) (sampleRate * 0.45),
                                            centre * std::pow (2.0f, lfo * depth * 2.0f));

        const float wt = std::tan (juce::MathConstants<float>::pi * sweepHz / (float) sampleRate);
        const float a = (wt - 1.0f) / (wt + 1.0f);

        const float in = inputs[0][(size_t) c];
        float x = in + lastOut[c] * feedback;

        for (int s = 0; s < stages; ++s)
        {
            const float y = a * x + apState[c][s];
            apState[c][s] = x - a * y;
            x = y;
        }

        lastOut[c] = x;
        outputs[0][(size_t) c] = in * (1.0f - wet) + (in + x) * 0.5f * wet;
    }
}

static ModuleDescriptor phaserDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.phaser";
    d.displayName = "Phaser";
    d.description =
        "Two to twelve allpass stages per channel swept by an LFO around a centre frequency, with "
        "feedback.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 3;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        // ORDER MATTERS: must match the ParamIndex enum in the header.
        makeRotary ("rate",     "Rate",       0.05f, 10.0f, 0.5f, 0, "Hz", true),
        makeRotary ("depth",    "Depth",      0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("centre",   "Center",     100.0f, 10000.0f, 800.0f, 0, "Hz", true),
        makeRotary ("feedback", "Feedback",   0.0f, 100.0f, 20.0f, 0, "%"),
        makeRotary ("dryWet",   "Dry/Wet",    0.0f, 100.0f, 50.0f, 0, "%"),
        makeSteppedList ("stages", "Stages", { "2", "4", "6", "8", "10", "12" }, 1, 1),
        makeRotary ("spread", "Spread", 0.0f, 100.0f, 0.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (PhaserModule, phaserDescriptor)
