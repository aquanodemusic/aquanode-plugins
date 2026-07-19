#pragma once

#include "ModuleCore.h"

// Env Follow - turns any audio signal into a modulation signal: rectifies the
// input and smooths it with separate attack / release times. Sidechain-style
// patching: kick -> Env Follow -> (inverted via Precision Utility) -> Volume.
// Flexible: follows each voice separately when fed per-voice audio.
// Input: 0 = Audio In. Output: 0 = Mod Out (unipolar).
class EnvFollowModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pAttack = 0, pRelease, pGain };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            env[v] = 0.0f;
    }

    void voiceReset (int v) override { env[v] = 0.0f; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        const float rectified = 0.5f * (std::abs (inputs[0][0]) + std::abs (inputs[0][1]));

        const float ms = rectified > env[v] ? param (pAttack) : param (pRelease);
        const float coeff = 1.0f - std::exp ((float) (-1.0 / (juce::jmax (0.1f, ms) * 0.001 * sampleRate)));
        env[v] += coeff * (rectified - env[v]);

        const float out = juce::jlimit (0.0f, 1.0f, env[v] * param (pGain));
        outputs[0][0] = out;
        outputs[0][1] = out;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    float env [aquanode::kMaxVoices] {};
};
