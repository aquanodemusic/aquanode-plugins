#pragma once

#include "ModuleCore.h"

// Audio In - passes the host's audio input into the patch, with a level knob.
class AudioInModule : public aquanode::SynthModule
{
public:
    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        const float level = param (0);
        outputs[0][0] = hostL * level;
        outputs[0][1] = hostR * level;
    }

    bool wantsHostInput() const override { return true; }
    void setHostInput (float l, float r) override { hostL = l; hostR = r; }

private:
    float hostL { 0.0f }, hostR { 0.0f };
};
