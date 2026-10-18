#pragma once

#include "ModuleCore.h"

// ADSR Delay - creative delay/resonator (adapted from the ADelaySR VST).
// One free-running tap with a shapeable per-cycle envelope (Attack/Release as
// % of the delay window) and four routing modes incl. hard ping-pong. The
// time knob reaches down into the sub-millisecond range, which is the "SR"
// resonator territory of the original.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class ADelaySRModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTime = 0, pTapVolume, pAttack, pRelease, pDryWet, pMode };

    void prepare (double sr) override;
    void reset() override
    {
        for (int c = 0; c < 2; ++c)
            std::fill (line[c].begin(), line[c].end(), 0.0f);
        writePos = 0;
        cyclePhase = 0.0;
        hardPPTap = 0;
        smoothedDelay = 100.0;
    }
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    static float tapEnvelope (float phase, float atkFrac, float relFrac);

    std::vector<float> line[2];
    int writePos { 0 };
    double cyclePhase { 0.0 };
    double smoothedDelay { 100.0 };
    int hardPPTap { 0 };
};
