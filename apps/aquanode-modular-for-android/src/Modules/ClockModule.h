#pragma once

#include "ModuleCore.h"

// Clock - tempo-synced pulse generator. Runs continuously at the host tempo,
// emitting gate pulses at the chosen division, with swing (every 2nd pulse
// delayed) and adjustable gate width. Drives Step Seq / Euclid / S&H clock
// inputs, or anything with a Trig In.
// Input: 0 = Reset In (rising edge restarts the cycle).
// Output: 0 = Gate Out (unipolar 0/1 with anti-click ramp).
class ClockModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pDivision = 0, pSwing, pWidth };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        pairPhase = 0.0;
        level = 0.0f;
        lastReset = 0.0f;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    double pairPhase { 0.0 };   // phase across a PAIR of pulses (0..1), for swing
    float level { 0.0f };
    float lastReset { 0.0f };
};
