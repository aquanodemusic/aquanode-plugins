#pragma once

#include "ModuleCore.h"

// Precision - surgical channel utility (adapted from the PrecisionUtility
// VST): independent sub-sample delay per channel, per-channel volume, and a
// blendable polarity invert. Useful for phase alignment and Haas tricks.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class PrecisionUtilityModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolL = 0, pVolR, pDelayL, pDelayR, pPhase };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const int size = juce::jmax (64, (int) (sr * 0.25));   // 250 ms max
        for (int c = 0; c < 2; ++c)
            line[c].assign ((size_t) size, 0.0f);
        writePos = 0;
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
            std::fill (line[c].begin(), line[c].end(), 0.0f);
        writePos = 0;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const int size = (int) line[0].size();
        if (size < 8)
        {
            outputs[0] = inputs[0];
            return;
        }

        line[0][(size_t) writePos] = inputs[0][0];
        line[1][(size_t) writePos] = inputs[0][1];

        // blendable polarity: 0% = untouched, 100% = fully inverted
        const float phaseGain = 1.0f - 2.0f * param (pPhase) * 0.01f;

        for (int c = 0; c < 2; ++c)
        {
            const double delayMs = param (c == 0 ? pDelayL : pDelayR);
            const double dt = juce::jlimit (0.0, (double) size - 2.0, delayMs * 0.001 * sampleRate);

            double readPos = (double) writePos - dt;
            while (readPos < 0.0) readPos += size;
            const int i0 = (int) readPos % size;
            const int i1 = (i0 + 1) % size;
            const float frac = (float) (readPos - std::floor (readPos));
            const float delayed = line[c][(size_t) i0] * (1.0f - frac) + line[c][(size_t) i1] * frac;

            outputs[0][(size_t) c] = delayed * param (c == 0 ? pVolL : pVolR) * phaseGain;
        }

        writePos = (writePos + 1) % size;
    }

private:
    std::vector<float> line[2];
    int writePos { 0 };
};
