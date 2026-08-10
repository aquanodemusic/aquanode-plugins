#pragma once

#include "ModuleCore.h"

// L/R Splitter - splits a stereo signal into its channels. Each output stays
// a full stereo pair, but only its named channel carries signal.
// Input: 0 = Audio In. Outputs: 0 = Left Audio Out, 1 = Right Audio Out.
class SplitterModule : public aquanode::SynthModule
{
public:
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        outputs[0][0] = inputs[0][0];   // Left out:  (L, silence)
        outputs[0][1] = 0.0f;
        outputs[1][0] = 0.0f;           // Right out: (silence, R)
        outputs[1][1] = inputs[0][1];
    }
};
