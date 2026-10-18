#pragma once

#include "ModuleCore.h"

// AquaReverb - lush FDN reverb (reduced adaptation of the Aquaton Reverb
// VST: 8 modulated feedback-delay-network lines with input diffusion
// allpasses, per-line lowpass damping and a Householder feedback matrix;
// the original's tank-allpass / HF-wash extras are folded into the core).
// Freeze holds the tank indefinitely.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class AquatonReverbModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pSize = 0, pFeedback, pDamping, pModRate, pModDepth, pDryWet, pFreeze };
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    struct Allpass
    {
        std::vector<float> buf;
        int pos { 0 };
        void assign (int n) { buf.assign ((size_t) juce::jmax (4, n), 0.0f); pos = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        float process (float in, float coeff)
        {
            const float delayed = buf[(size_t) pos];
            const float y = -coeff * in + delayed;
            buf[(size_t) pos] = in + coeff * y;
            if (++pos >= (int) buf.size()) pos = 0;
            return y;
        }
    };

    float readLine (int l, double delaySamples) const;

    std::vector<float> lines[numLines];
    int writePos[numLines] {};
    float lpState[numLines] {};
    double lfoPhase[numLines] {};
    Allpass diffusers[2][numDiffusers];
    double baseDelay[numLines] {};
    int maxLine { 0 };
};
