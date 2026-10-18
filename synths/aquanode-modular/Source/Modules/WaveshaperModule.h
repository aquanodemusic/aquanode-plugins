#pragma once

#include "ModuleCore.h"

// Waveshaper - drive into a choice of shaping curves: Tanh (smooth
// saturation), Sine Fold (wavefolder), Hard Clip, Rectify. Flexible:
// per-voice when fed per-voice, so each note distorts independently.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class WaveshaperModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pDrive = 0, pOut, pDryWet, pShape };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float drive = param (pDrive);
        const float outLevel = param (pOut);
        const float wet = param (pDryWet) * 0.01f;
        const int shape = (int) param (pShape);

        for (int c = 0; c < 2; ++c)
        {
            const float in = inputs[0][(size_t) c];
            const float x = in * drive;
            float y;
            switch (shape)
            {
                case 1:  y = std::sin (x * juce::MathConstants<float>::halfPi); break;  // Sine Fold
                case 2:  y = juce::jlimit (-1.0f, 1.0f, x); break;                      // Hard Clip
                case 3:  y = std::abs (std::tanh (x)) * 2.0f - 1.0f; break;             // Rectify
                default: y = std::tanh (x); break;                                      // Tanh
            }
            outputs[0][(size_t) c] = (in * (1.0f - wet) + y * wet) * outLevel;
        }
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
