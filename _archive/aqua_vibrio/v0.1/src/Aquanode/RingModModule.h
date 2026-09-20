#pragma once

#include "ModuleCore.h"

// Ring Mod - multiplies input A by input B. Depth blends from clean A
// (0%) to full ring modulation (100%), i.e. classic AM in between.
// Flexible: per-voice when fed per-voice, so two per-voice oscillators
// ring-modulate per note. B reads as silence when unpatched.
// Inputs: 0 = In A (Audio), 1 = In B (Audio). Output: 0 = Audio Out.
class RingModModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pDepth = 0, pLevel };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float d = param (pDepth) * 0.01f;
        const float level = param (pLevel);
        const bool bConnected = isInputConnected (1);   // unpatched B = unity (pass-through)
        for (int c = 0; c < 2; ++c)
        {
            const float a = inputs[0][(size_t) c];
            const float b = bConnected ? inputs[1][(size_t) c] : 1.0f;
            outputs[0][(size_t) c] = a * ((1.0f - d) + d * b) * level;
        }
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
