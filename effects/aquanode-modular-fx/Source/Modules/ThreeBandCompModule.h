#pragma once

#include "ModuleCore.h"

// 3-Band Compressor (typeId "fx.threebandcomp"; part of the
// violet Colorbass family) - three-band upward + downward compressor, the "squash
// everything flat and loud" stage that sits at the end of nearly every
// colour-bass patch. The signal is split at Low X / High X (Linkwitz-Riley,
// 4th order); each band is pulled down above its upper threshold and pushed
// UP below its lower threshold, so quiet detail (unison tails, reverb, the
// gaps in a chop) comes forward and peaks are flattened. Depth scales the
// whole effect, Time scales attack and release together.
// Global effect. Input 0 = Audio In, Output 0 = Audio Out.
class ThreeBandCompModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pDepth = 0, pTime, pUpward, pDownward, pLowGain, pMidGain, pHighGain,
                      pInGain, pOutGain, pLowX, pHighX };

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    juce::dsp::LinkwitzRileyFilter<float> splitLow, splitHigh;
    float env[3] { 0.0f, 0.0f, 0.0f };
    float lastLowX { -1.0f }, lastHighX { -1.0f };
    int coeffCounter { 0 };
};
