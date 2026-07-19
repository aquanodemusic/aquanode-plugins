#pragma once

#include "ModuleCore.h"

// LFO - free-running low-frequency oscillator with offset and level.
// No inputs. Output: 0 = Modulation Out.
class LFOModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pWaveform, pOffset, pLevel };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        shValue = 0.0f;
    }

    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override;

private:
    double phase { 0.0 };
    float shValue { 0.0f };
    juce::Random random;
};
