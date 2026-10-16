#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Smooth Hall - an 8-line feedback delay network with slowly modulated line
// lengths (no metallic flutter on sustained pads), an input diffuser, Pre
// Delay, a High Damp that keeps the decay time honest per line, and the
// a pre-EQ: low and high shelf gains on what goes in.
// Reverb Time is the RT60 in seconds. Global.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SmoothHallModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTime = 0, pPreDelay, pHighDamp, pLowGain, pHighGain, pDryWet };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fx.prepare (sr);
    }

    void reset() override { fx.reset(); }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const auto o = fx.process (inputs[0][0], inputs[0][1], param (pTime), param (pHighDamp),
                                   param (pPreDelay), param (pLowGain), param (pHighGain),
                                   param (pDryWet) * 0.01f);
        outputs[0] = { o[0], o[1] };
    }

private:
    vintagedsp::SmoothHall fx;
};
