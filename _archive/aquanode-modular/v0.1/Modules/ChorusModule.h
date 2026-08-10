#pragma once

#include "ModuleCore.h"

// Chorus - modulated delay line per channel, LFO phase offset between the
// channels controls stereo spread. Input: 0 = Audio In. Output: 0 = Audio Out.
class ChorusModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pDepth, pBaseDelay, pSpread, pDryWet };

    void prepare (double sr) override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    std::vector<float> line[2];
    int writePos { 0 };
    double lfoPhase { 0.0 };
};
