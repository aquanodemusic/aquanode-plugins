#pragma once

#include "ModuleCore.h"

// SR Reduce - sample-rate reduction (decimator): holds each sample for
// several periods, aliasing the highs into inharmonic digital shimmer.
// Rate is the effective sample rate; fractional hold lengths are supported
// so the knob sweeps smoothly. Flexible: per-voice when patched per-voice.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SRReduceModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pDryWet };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        phase[v] = 1.0;   // sample immediately
        heldL[v] = heldR[v] = 0.0f;
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        phase[v] += param (pRate) / sampleRate;
        if (phase[v] >= 1.0)
        {
            phase[v] -= std::floor (phase[v]);
            heldL[v] = inputs[0][0];
            heldR[v] = inputs[0][1];
        }

        const float wet01 = param (pDryWet) * 0.01f;
        outputs[0][0] = inputs[0][0] + wet01 * (heldL[v] - inputs[0][0]);
        outputs[0][1] = inputs[0][1] + wet01 * (heldR[v] - inputs[0][1]);
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    double phase [aquanode::kMaxVoices] {};
    float heldL [aquanode::kMaxVoices] {};
    float heldR [aquanode::kMaxVoices] {};
};
