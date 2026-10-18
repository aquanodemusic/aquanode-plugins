#pragma once

#include "ModuleCore.h"

// Volume - level control with a multiplicative modulation input (reads as 1.0
// when unpatched, so an ADSR into Mod In acts as a per-voice amplitude
// envelope). Flexible lane, stateless: the same math runs per voice or
// globally as the patch requires.
// Inputs: 0 = Audio In, 1 = Mod In. Output: 0 = Audio Out.
class VolumeModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float mod = isInputConnected (1) ? inputs[1][0] : 1.0f;
        const float gain = param (0) * mod;
        outputs[0][0] = inputs[0][0] * gain;
        outputs[0][1] = inputs[0][1] * gain;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
