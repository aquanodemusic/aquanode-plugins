#pragma once

#include "ModuleCore.h"

// Shelf EQ - a three-band tone shaper: a low band (shelf or low cut), a mid
// band (peak or notch) and a high band (shelf or high cut), each with its own
// frequency, gain and Q, plus an output trim. The display draws the summed
// response. Where 3-Bell EQ is three surgical bells, this is the broad
// "warm it up, thin it out, take the fizz off" kind.
// Global effect. Input: 0 = Audio In. Output: 0 = Audio Out.
class ShelfEQModule : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pLowMode = 0, pLowFreq, pLowGain, pLowQ,
        pMidMode, pMidFreq, pMidGain, pMidQ,
        pHighMode, pHighFreq, pHighGain, pHighQ,
        pOutput
    };

    struct Biquad
    {
        double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 };
        std::complex<double> response (double w) const;
    };

    // the three bands for a set of parameter values (also used by the display)
    static void design (const float* values, double sampleRate, Biquad out[3]);

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 64; }

private:
    Biquad band[3];
    double z1[3][2] {}, z2[3][2] {};
    float lastValues[13] {};
    int ctrl { 0 };
    float outGain { 1.0f };
};
