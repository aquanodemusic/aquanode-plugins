#pragma once

#include "ModuleCore.h"

// Volume - level control with a multiplicative modulation input. When
// nothing is patched into Mod In, it reads as 1.0 (full pass-through at knob
// level), so patching an ADSR into Mod In behaves as a conventional
// amplitude envelope.
// Inputs: 0 = Audio In, 1 = Mod In. Output: 0 = Audio Out.
class VolumeModule : public aquanode::SynthModule
{
public:
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float mod = isInputConnected (1) ? inputs[1][0] : 1.0f;
        const float gain = param (0) * mod;
        outputs[0][0] = inputs[0][0] * gain;
        outputs[0][1] = inputs[0][1] * gain;
    }
};
