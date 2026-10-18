#pragma once

#include "ModuleCore.h"

// Echo - a tape-style delay with the repeats filtered: free time in
// milliseconds or locked to the tempo (straight, dotted, triplet), Stereo /
// Ping-Pong / Mono, and a band-pass in the feedback loop (Tone sets its
// centre, Width how many octaves it lets through), so each repeat gets
// darker and thinner like a real echo. Changing the time glides the pitch of
// the repeats, as tape does; Wobble adds a slow drift to the time.
// Offset spreads the left and right times apart in Stereo mode.
// Global effect. Input: 0 = Audio In. Output: 0 = Audio Out.
class EchoModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTime = 0, pDivision, pFeedback, pTone, pWidth, pSync, pStyle, pMix, pOffset, pWobble };

    static constexpr double kMaxSeconds = 4.0;

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    struct SVF
    {
        float ic1 { 0 }, ic2 { 0 };
        float low (float x, float g, float k)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }
        float high (float x, float g, float k)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return x - k * v1 - v2;
        }
    };

    float readLine (const std::vector<float>& line, double delaySamples) const;

    std::vector<float> lineL, lineR;
    int writePos { 0 };
    double smoothL { 0.0 }, smoothR { 0.0 };
    SVF hpL, hpR, lpL, lpR;
    double wobblePhase { 0.0 };
    float smCoef { 0.001f };
};
