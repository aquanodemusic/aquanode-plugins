#pragma once

#include "ModuleCore.h"

// L/R Splitter - splits a stereo signal into its channels. Each output stays
// a full stereo pair, but only its named channel carries signal. Flexible
// lane, stateless.
// Input: 0 = Audio In. Outputs: 0 = Left Audio Out, 1 = Right Audio Out.
class SplitterModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        outputs[0][0] = inputs[0][0];   // Left out:  (L, silence)
        outputs[0][1] = 0.0f;
        outputs[1][0] = 0.0f;           // Right out: (silence, R)
        outputs[1][1] = inputs[0][1];
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
