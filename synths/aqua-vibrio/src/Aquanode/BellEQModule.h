#pragma once

#include "ModuleCore.h"

// 3-Bell EQ - three peaking bells with a graphic display. Drag a dot to move
// its bell: sideways for frequency, up and down for gain. The mouse wheel
// over a dot (or a right-drag) sets Q - narrow for surgical notches, wide for
// gentle tone shaping. The curve behind the dots is the real summed response
// of the three filters, computed from the coefficients themselves.
//
// Unlike the Pitch Lock Filter's bank of 128, three biquads are cheap enough
// to re-derive their coefficients every block, so every knob here is fully
// modulatable: an LFO into a bell frequency is a wah, an envelope into gain
// is a dynamic EQ. The coefficients then glide to those targets per sample
// (~5 ms), which is what keeps a swept bell from stepping and crackling.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class BellEQModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumBells = 3;

    // freq / gain / Q per bell, laid out bell-major: f0 g0 q0 f1 g1 q1 ...
    enum ParamIndex { pFreq0 = 0, pGain0, pQ0, pFreq1, pGain1, pQ1, pFreq2, pGain2, pQ2 };

    static constexpr float kMinFreq = 20.0f, kMaxFreq = 20000.0f;
    static constexpr float kMinGain = -24.0f, kMaxGain = 24.0f;
    static constexpr float kMinQ = 0.2f, kMaxQ = 8.0f;

    static juce::String freqParamId (int bell) { return "freq" + juce::String (bell); }
    static juce::String gainParamId (int bell) { return "gain" + juce::String (bell); }
    static juce::String qParamId    (int bell) { return "q" + juce::String (bell); }

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;

    void processSample (const aquanode::StereoFrame* inputs,
                        aquanode::StereoFrame* outputs) override;

    // magnitude response in dB at a frequency, summed over the three bells,
    // from the CURRENT knob positions - the display reads this
    float responseDbAt (float frequencyHz) const;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 104; }

private:
    struct Biquad
    {
        // transposed direct form II: coefficient changes glide through it far
        // more gracefully than DF1, which matters when a bell is swept
        float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f }, a1 { 0.0f }, a2 { 0.0f };
        float z1[2] {}, z2[2] {};

        float process (int ch, float x)
        {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }

        void clearState() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }
    };

    struct Coeffs { float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f }, a1 { 0.0f }, a2 { 0.0f }; };

    Coeffs computeCoeffs (float freqHz, float gainDb, float q) const;

    Biquad bells [kNumBells];
    Coeffs targets [kNumBells];
    float smoothCoeff { 0.005f };
};
