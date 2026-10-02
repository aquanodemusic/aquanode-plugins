#pragma once

#include "ModuleCore.h"

// Audio Out - the patch's output back to the host, with a level knob.
class AudioOutModule : public aquanode::SynthModule
{
public:
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame*) override
    {
        const float level = param (0);
        outL = inputs[0][0] * level;
        outR = inputs[0][1] * level;
    }

    bool providesHostOutput() const override { return true; }
    void getHostOutput (float& l, float& r) override { l = outL; r = outR; }

private:
    float outL { 0.0f }, outR { 0.0f };
};
