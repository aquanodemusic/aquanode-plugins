#pragma once

#include "ModuleCore.h"

// Resonate - tuned comb resonator (adapted from the Resonate VST, reduced to
// a single stereo resonator). Mode A = classic positive-feedback comb with
// LPF damping and tanh limiting; Mode B = negative-feedback comb with the
// original's square-wave character. Pitch is set in MIDI note numbers.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class ResonateModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pNote = 0, pDecay, pColor, pLfoDepth, pDryWet, pMode, pLfoRate };
    static constexpr int maxDelaySamples = 8192;

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (line[c].begin(), line[c].end(), 0.0f);
            writeIndex[c] = 0;
            lpfState[c] = 0.0f;
            lfoPhase[c] = c * 0.25;   // slight stereo offset
        }
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    float readDelayLinear (int channel, double delaySamples) const;

    std::vector<float> line[2] { std::vector<float> (maxDelaySamples, 0.0f),
                                 std::vector<float> (maxDelaySamples, 0.0f) };
    int writeIndex[2] {};
    float lpfState[2] {};
    double lfoPhase[2] {};
};
