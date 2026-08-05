#pragma once

#include "ModuleCore.h"

// Panning - stereo balance with an additive bipolar modulation input
// (reads as 0.0 when unpatched).
// Inputs: 0 = Audio In, 1 = Mod In. Output: 0 = Audio Out.
class PanningModule : public aquanode::SynthModule
{
public:
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float mod = isInputConnected (1) ? inputs[1][0] : 0.0f;
        const float pan = juce::jlimit (-1.0f, 1.0f, param (0) * 0.01f + mod);
        outputs[0][0] = inputs[0][0] * (pan > 0.0f ? 1.0f - pan : 1.0f);
        outputs[0][1] = inputs[0][1] * (pan < 0.0f ? 1.0f + pan : 1.0f);
    }
};
