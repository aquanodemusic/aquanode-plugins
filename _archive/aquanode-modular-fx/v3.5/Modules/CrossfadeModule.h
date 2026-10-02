#pragma once

#include "ModuleCore.h"

// Crossfade - equal-power A/B crossfader with a modulation input. Fade sets
// the base position (0% = all A, 100% = all B); Mod Depth adds the Fade In
// signal on top (bipolar depth allows inverted control). LFO -> Fade In gives
// auto-morphing between two sound sources; a Gate can hard-switch them.
// Inputs: 0 = A In, 1 = B In, 2 = Fade In. Output: 0 = Audio Out.
class CrossfadeModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pFade = 0, pModDepth };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        const float pos = juce::jlimit (0.0f, 1.0f,
            param (pFade) * 0.01f + param (pModDepth) * 0.01f * inputs[2][0]);

        const float gainB = std::sin (pos * juce::MathConstants<float>::halfPi);
        const float gainA = std::cos (pos * juce::MathConstants<float>::halfPi);

        outputs[0][0] = inputs[0][0] * gainA + inputs[1][0] * gainB;
        outputs[0][1] = inputs[0][1] * gainA + inputs[1][1] * gainB;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
