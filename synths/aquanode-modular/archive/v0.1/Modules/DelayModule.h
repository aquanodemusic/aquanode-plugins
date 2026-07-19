#pragma once

#include "ModuleCore.h"

// Delay - stereo, tempo-synced times per channel, feedback with a high-pass
// filter in the loop. Input: 0 = Audio In. Output: 0 = Audio Out.
class DelayModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTimeL = 0, pTimeR, pFeedback, pHighPass, pDryWet };

    void prepare (double sr) override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    std::vector<float> line[2];
    int writePos { 0 };
    double smoothedDelay[2] { 0.0, 0.0 };
    float hpState[2] {};
};
