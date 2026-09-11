#pragma once

#include "ModuleCore.h"

// Mixer - four audio inputs with individual level knobs plus a master.
// Flexible: per-voice when fed per-voice, so it can sit inside a voice
// chain (mixing operators) or in the global effect chain.
// Inputs: 0-3 = In 1..4 (Audio). Output: 0 = Audio Out.
class MixerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pLevel1 = 0, pLevel2, pLevel3, pLevel4, pMaster };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float master = param (pMaster);
        for (int c = 0; c < 2; ++c)
        {
            float sum = 0.0f;
            for (int i = 0; i < 4; ++i)
                sum += inputs[i][(size_t) c] * param (i);
            outputs[0][(size_t) c] = sum * master;
        }
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
