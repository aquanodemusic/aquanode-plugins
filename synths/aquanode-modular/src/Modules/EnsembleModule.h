#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Ensemble - a vintage polysynth effect section: Chorus, Phase or Ensemble, the
// three-phase BBD ensemble with a slow sweep plus a faster shimmer that makes
// its pads so wide. Global (it works on the summed voices, as on a polysynth).
// Input: 0 = Audio In. Output: 0 = Audio Out (stereo).
class EnsembleModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pMode = 0, pSpeed, pDepth, pSpread, pMix };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fx.prepare (sr);
    }

    void reset() override { fx.reset(); }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const auto o = fx.process (inputs[0][0], inputs[0][1], param (pSpeed) * 0.1f, param (pDepth) * 0.01f,
                                   param (pSpread) * 0.01f, param (pMix) * 0.01f, (int) param (pMode));
        outputs[0] = { o[0], o[1] };
    }

private:
    vintagedsp::EnsembleCore fx;
};
