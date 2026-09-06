#pragma once

#include "ModuleCore.h"

// Logic - combines two gate/mod signals with boolean or comparison ops
// (threshold 0.5 for the boolean ops). Output is 0/1 with the usual 0.5 ms
// anti-click ramp, so it can gate audio directly. Great for generative
// triggers: Clock AND Euclid, XOR of two clocks, etc.
// Inputs: 0 = A In, 1 = B In. Output: 0 = Mod Out (unipolar).
class LogicModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pOp = 0 };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            level[v] = 0.0f;
    }

    void voiceReset (int v) override { level[v] = 0.0f; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        const bool a = inputs[0][0] > 0.5f;
        const bool b = inputs[1][0] > 0.5f;

        bool result = false;
        switch ((int) param (pOp))
        {
            case 0: result = a && b; break;                    // AND
            case 1: result = a || b; break;                    // OR
            case 2: result = a != b; break;                    // XOR
            case 3: result = ! (a && b); break;                // NAND
            case 4: result = ! (a || b); break;                // NOR
            case 5: result = inputs[0][0] > inputs[1][0]; break;   // A > B
            case 6: result = ! a; break;                       // NOT A
        }

        const float step = (float) (1.0 / (0.0005 * sampleRate));
        const float target = result ? 1.0f : 0.0f;
        if (level[v] < target)      level[v] = juce::jmin (target, level[v] + step);
        else if (level[v] > target) level[v] = juce::jmax (target, level[v] - step);

        outputs[0][0] = level[v];
        outputs[0][1] = level[v];
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    float level [aquanode::kMaxVoices] {};
};
