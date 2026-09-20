#pragma once

#include "ModuleCore.h"

// Reverb - pre-delay line + juce::Reverb core. Decay time and room size are
// mapped onto the core's roomSize/damping controls.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class ReverbModule : public aquanode::SynthModule
{
public:
    // FORKED for Aqua Vibrio: added Feedback and Colour. Feedback drives the
    // tail past the decay setting; Colour is a tilt filter on the wet path,
    // which the Virus has and juce::Reverb does not. Upstream has the first
    // five parameters.
    enum ParamIndex { pRoomSize = 0, pDecay, pDamping, pPreDelay, pDryWet, pFeedback, pColour };

    void prepare (double sr) override;
    void reset() override
    {
        reverb.reset();
        for (int c = 0; c < 2; ++c)
            std::fill (preLine[c].begin(), preLine[c].end(), 0.0f);
        writePos = 0;
    }
    void blockStart() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    juce::Reverb reverb;
    std::vector<float> preLine[2];
    int writePos { 0 };

    // one-pole state for the colour tilt, per channel
    float tilt[2] { 0.0f, 0.0f };
};
