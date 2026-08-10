#pragma once

#include "ModuleCore.h"

// SVF Filter - Chamberlin state-variable filter, LP/BP/HP.
// Inputs: 0 = Audio In, 1 = Mod In (cutoff). Output: 0 = Audio Out.
class SVFFilterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCutoff = 0, pResonance, pMode, pModDepth };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        for (int c = 0; c < 2; ++c)
            low[c] = band[c] = 0.0f;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    float low[2] {}, band[2] {};
};
