#pragma once

#include "ModuleCore.h"

// Compressor - feedforward stereo-linked compressor. Peak detector in the log
// domain with one-pole attack / release smoothing on the gain reduction,
// plus makeup gain. The patch's first dynamics processor.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class CompressorModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pThreshold = 0, pRatio, pAttack, pRelease, pMakeup };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override { gainReductionDb = 0.0f; }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float peak = juce::jmax (std::abs (inputs[0][0]), std::abs (inputs[0][1]));
        const float levelDb = 20.0f * std::log10 (juce::jmax (peak, 1.0e-6f));

        const float over = juce::jmax (0.0f, levelDb - param (pThreshold));
        const float targetGrDb = over * (1.0f - 1.0f / juce::jmax (1.0f, param (pRatio)));

        const float ms = targetGrDb > gainReductionDb ? param (pAttack) : param (pRelease);
        const float coeff = 1.0f - std::exp ((float) (-1.0 / (juce::jmax (0.05f, ms) * 0.001 * sampleRate)));
        gainReductionDb += coeff * (targetGrDb - gainReductionDb);

        const float gain = std::pow (10.0f, (param (pMakeup) - gainReductionDb) / 20.0f);
        outputs[0][0] = inputs[0][0] * gain;
        outputs[0][1] = inputs[0][1] * gain;
    }

private:
    float gainReductionDb { 0.0f };
};
