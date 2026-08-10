#pragma once

#include "ModuleCore.h"

// Delay - stereo, tempo-synced times per channel, feedback with a high-pass
// filter in the loop. Input: 0 = Audio In. Output: 0 = Audio Out.
class DelayModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTimeL = 0, pTimeR, pFeedback, pHighPass, pDryWet };

    void prepare (double sr) override;
    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (line[c].begin(), line[c].end(), 0.0f);
            hpState[c] = 0.0f;
        }
        writePos = 0;
    }
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    std::vector<float> line[2];
    int writePos { 0 };
    double smoothedDelay[2] { 0.0, 0.0 };
    float hpState[2] {};
};
