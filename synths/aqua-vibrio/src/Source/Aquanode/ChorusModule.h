#pragma once

#include "ModuleCore.h"

// Chorus - modulated delay line per channel, LFO phase offset between the
// channels controls stereo spread. Input: 0 = Audio In. Output: 0 = Audio Out.
class ChorusModule : public aquanode::SynthModule
{
public:
    // FORKED for Aqua Vibrio: added Voices and LFO Shape, so the Virus's
    // chorus types (which differ by voice count) and its LFO shape control
    // have something real to drive. Upstream has the first five parameters.
    enum ParamIndex { pRate = 0, pDepth, pBaseDelay, pSpread, pDryWet, pVoices, pLfoShape };

    void prepare (double sr) override;
    void reset() override
    {
        for (int c = 0; c < 2; ++c)
            std::fill (line[c].begin(), line[c].end(), 0.0f);
        writePos = 0;
        lfoPhase = 0.0;
    }
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    std::vector<float> line[2];
    int writePos { 0 };
    double lfoPhase { 0.0 };
};
