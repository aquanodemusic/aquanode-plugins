#pragma once

#include "ModuleCore.h"

// Limiter - fast stereo-linked peak limiter. Instant attack in the envelope
// follower (the detector jumps straight up to any new peak, so nothing gets
// through above the ceiling), one-pole release on the way down, and a final
// hard clamp at the ceiling as a safety net for intersample edge cases.
// Zero latency by design: a lookahead stage would introduce delay that the
// modular graph cannot compensate for, so this stays a brickwall-style
// clamp limiter rather than a transparent mastering limiter.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class LimiterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pGain = 0, pCeiling, pRelease };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override { envelope = 0.0f; }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float gain = std::pow (10.0f, param (pGain) / 20.0f);
        const float l = inputs[0][0] * gain;
        const float r = inputs[0][1] * gain;

        // stereo-linked peak detector: instant attack, one-pole release
        const float peak = juce::jmax (std::abs (l), std::abs (r));
        if (peak > envelope)
            envelope = peak;
        else
        {
            const float coeff = 1.0f - std::exp (
                (float) (-1.0 / (juce::jmax (1.0f, param (pRelease)) * 0.001 * sampleRate)));
            envelope += coeff * (peak - envelope);
        }

        const float ceiling = std::pow (10.0f, param (pCeiling) / 20.0f);
        const float reduce = envelope > ceiling ? ceiling / juce::jmax (envelope, 1.0e-6f) : 1.0f;

        outputs[0][0] = juce::jlimit (-ceiling, ceiling, l * reduce);
        outputs[0][1] = juce::jlimit (-ceiling, ceiling, r * reduce);
    }

private:
    float envelope { 0.0f };
};
