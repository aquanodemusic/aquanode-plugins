#pragma once

#include "ModuleCore.h"

// Reverb - pre-delay line + juce::Reverb core. Decay time and room size are
// mapped onto the core's roomSize/damping controls.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class ReverbModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRoomSize = 0, pDecay, pDamping, pPreDelay, pDryWet };

    void prepare (double sr) override;
    void blockStart() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    juce::Reverb reverb;
    std::vector<float> preLine[2];
    int writePos { 0 };
};
