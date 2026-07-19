#pragma once

#include "ModuleCore.h"

// LiquidChor - liquid chorus/flanger (adapted from the LiquidChor VST).
// Two delay lines sweep between Time 1 and Time 2 under an LFO, with an
// adjustable L/R LFO phase offset for width and feedback for flanging.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class LiquidChorModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTime1 = 0, pTime2, pSpeed, pFeedback, pDryWet, pLrPhase };

    void prepare (double sr) override;
    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (line[c].begin(), line[c].end(), 0.0f);
            fb[c] = 0.0f;
        }
        writePos = 0;
        lfoPhase = 0.0;
    }
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    float readInterp (int channel, double delaySamples) const;

    std::vector<float> line[2];
    int writePos { 0 };
    double lfoPhase { 0.0 };
    float fb[2] {};
};
