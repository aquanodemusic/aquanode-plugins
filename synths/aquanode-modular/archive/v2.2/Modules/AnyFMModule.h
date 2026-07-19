#pragma once

#include "ModuleCore.h"

// FM - adapted from the AnyFM VST: audio-rate cross-modulation between two
// arbitrary signals. Phase Mod scrubs a delayed copy of the carrier by the
// modulator (true through-zero PM, ~23 ms latency in the wet path from the
// delay centre); Ring Mod multiplies; Feedback feeds the output back as its
// own modulator for chaotic self-FM. The original's routing matrix and
// fade-in machinery are dropped - patch cables ARE the routing here.
// Inputs: 0 = Carrier In, 1 = Mod In. Output: 0 = Audio Out.
class AnyFMModule : public aquanode::SynthModule
{
public:
    static constexpr int kBufferSize = 4096;
    static constexpr int kCentreDelay = 1024;
    enum ParamIndex { pDepth = 0, pMode, pModGain, pDryWet };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (buffer[c].begin(), buffer[c].end(), 0.0f);
            feedback[c] = 0.0f;
        }
        writePos = 0;
        smoothedDepth = -1.0f;   // snap to target on first sample
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    std::vector<float> buffer [2] { std::vector<float> (kBufferSize, 0.0f),
                                    std::vector<float> (kBufferSize, 0.0f) };
    int writePos { 0 };
    float feedback [2] {};
    float smoothedDepth { -1.0f };
};
