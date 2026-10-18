#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Cross Delay - a stereo / cross delay: two delay lines with
// their own time and (bipolar) feedback; in Cross mode each repeat feeds the
// opposite line, so echoes ping-pong. High and Low Damp filter the repeats,
// Spread narrows the echo image toward the centre. Times glide when changed.
// Global. Input: 0 = Audio In. Output: 0 = Audio Out.
class CrossDelayModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTimeL = 0, pTimeR, pFeedbackL, pFeedbackR, pMode, pHighDamp, pLowDamp, pSpread, pDryWet };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fx.prepare (sr);
    }

    void reset() override { fx.reset(); }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const auto o = fx.process (inputs[0][0], inputs[0][1], param (pTimeL), param (pTimeR),
                                   param (pFeedbackL) * 0.01f, param (pFeedbackR) * 0.01f,
                                   (int) param (pMode) == 1, param (pHighDamp), param (pLowDamp),
                                   param (pSpread) * 0.01f, 1.0f, param (pDryWet) * 0.01f);
        outputs[0] = { o[0], o[1] };
    }

private:
    vintagedsp::CrossDelay fx;
};
