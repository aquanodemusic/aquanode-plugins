#pragma once

#include "ModuleCore.h"

// Phaser - 2..12 first-order allpass stages per channel swept by an LFO
// around a centre frequency, with feedback.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class PhaserModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pDepth, pCentre, pFeedback, pDryWet, pStages };
    static constexpr int maxStages = 12;

    void prepare (double sr) override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    float apState[2][maxStages] {};
    float lastOut[2] {};
    double lfoPhase { 0.0 };
};
