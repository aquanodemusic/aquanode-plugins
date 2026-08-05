#pragma once

#include "ModuleCore.h"

// Ladder Filter - Moog-style 4-pole ladder with drive and self-oscillating
// resonance. Modes: 12dB LP / 24dB LP / 12dB HP / 24dB HP.
// Inputs: 0 = Audio In, 1 = Mod In (cutoff). Output: 0 = Audio Out.
class LadderFilterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCutoff = 0, pResonance, pDrive, pModDepth, pOutputVolume, pMode };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        for (int c = 0; c < 2; ++c)
            s1[c] = s2[c] = s3[c] = s4[c] = 0.0f;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    float s1[2] {}, s2[2] {}, s3[2] {}, s4[2] {};
};
