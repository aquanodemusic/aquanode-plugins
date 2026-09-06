#pragma once

#include "ModuleCore.h"

// Slew Limiter - rate-limits how fast a signal may rise and fall. Patch
// after KeyTrack for portamento, after Gate for custom AR envelopes, after a
// square LFO for trapezoid shapes. Flexible: per-voice when fed per-voice.
// Input: 0 = Mod In. Output: 0 = Mod Out.
class SlewModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRise = 0, pFall };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        state[v][0] = state[v][1] = 0.0f;
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        // full-scale (1.0) traversal takes Rise/Fall milliseconds
        const float riseStep = (float) (1.0 / (juce::jmax (0.05f, param (pRise)) * 0.001 * sampleRate));
        const float fallStep = (float) (1.0 / (juce::jmax (0.05f, param (pFall)) * 0.001 * sampleRate));

        for (int c = 0; c < 2; ++c)
        {
            const float diff = inputs[0][(size_t) c] - state[v][c];
            state[v][c] += juce::jlimit (-fallStep, riseStep, diff);
            outputs[0][(size_t) c] = state[v][c];
        }
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    float state [aquanode::kMaxVoices][2] { };
};
