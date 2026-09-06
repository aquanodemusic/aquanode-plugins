#pragma once

#include "ModuleCore.h"

// DC Offset - shifts and scales a signal: Out = In * Gain + Offset.
// The classic use is turning a bipolar source into a unipolar one (an LFO at
// Gain 50% / Offset 0.5 sweeps 0..1 instead of -1..+1), or inverting a
// modulation signal with a negative Gain. With nothing patched in it emits a
// constant Offset, which makes it a handy manual DC source for driving other
// modules' mod inputs.
// Input: 0 = Mod In. Output: 0 = Mod Out.
class DCOffsetModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pOffset = 0, pGain };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        const float gain = param (pGain);
        const float offset = param (pOffset);
        outputs[0][0] = inputs[0][0] * gain + offset;
        outputs[0][1] = inputs[0][1] * gain + offset;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
