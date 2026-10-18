#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Stereo Phaser - allpass chains per channel swept by
// an LFO whose right side can run up to 180 degrees behind the left (the
// notches then move in opposite directions and the image swims), Manual (the
// sweep centre), Depth, bipolar Resonance with a High Damp low-pass inside the
// feedback loop. Global.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class StereoPhaserModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pLrPhase, pManual, pDepth, pResonance, pHighDamp, pDryWet, pStages, pShape };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fx.prepare (sr);
    }

    void reset() override { fx.reset(); }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        static const int stageChoices[] = { 4, 6, 8, 12 };
        const auto o = fx.process (inputs[0][0], inputs[0][1], param (pRate), param (pLrPhase),
                                   param (pManual), param (pDepth) * 0.01f, param (pResonance) * 0.01f,
                                   param (pHighDamp), param (pDryWet) * 0.01f,
                                   stageChoices[juce::jlimit (0, 3, (int) param (pStages))], (int) param (pShape));
        outputs[0] = { o[0], o[1] };
    }

private:
    vintagedsp::StereoPhaser fx;
};
