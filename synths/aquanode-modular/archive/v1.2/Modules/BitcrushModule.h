#pragma once

#include "ModuleCore.h"

// Bitcrush - bit-depth reduction: quantizes the signal to 2^bits levels for
// classic digital grit, from subtle 12-bit warmth down to 1-bit destruction.
// Bits is continuous, so fractional settings crossfade between step sizes.
// Flexible: crushes each voice separately when patched per-voice.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class BitcrushModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pBits = 0, pDryWet };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        const float bits = param (pBits);
        const float levels = std::pow (2.0f, bits - 1.0f);   // per polarity
        const float wet01 = param (pDryWet) * 0.01f;

        for (int c = 0; c < 2; ++c)
        {
            const float x = inputs[0][(size_t) c];
            const float crushed = std::round (x * levels) / levels;
            outputs[0][(size_t) c] = x + wet01 * (crushed - x);
        }
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
