#pragma once

#include "ModuleCore.h"

// Flanger - short modulated delay with positive/negative feedback.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class FlangerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pDepth, pManual, pFeedback, pDryWet };

    void prepare (double sr) override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    std::vector<float> line[2];
    int writePos { 0 };
    double lfoPhase { 0.0 };
};
