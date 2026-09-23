#pragma once

#include "ModuleCore.h"

// Stereo Width - mid/side stereo widener. Width scales the side signal
// (100 % = untouched, 0 % = mono, 200 % = doubled side energy). Haas adds a
// short micro-delay to the right channel, which the ear reads as extra width
// even on a mono source - the classic precedence-effect trick. Bass Mono
// removes the side content below a crossover, keeping the low end centred
// and punchy (and vinyl/club safe) while everything above still spreads.
// A Width modulation input lets an LFO breathe the stereo image.
// Inputs: 0 = Audio In, 1 = Width Mod. Output: 0 = Audio Out.
class StereoWidthModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pWidth = 0, pHaas, pBassMono };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        delayBuffer.assign ((size_t) ((int) (kMaxHaasSeconds * sr) + 4), 0.0f);
        // ~10 ms glide on the Haas time so turning the knob never clicks
        haasSmoothCoeff = 1.0f - std::exp ((float) (-1.0 / (0.010 * sr)));
        reset();
    }

    void reset() override
    {
        std::fill (delayBuffer.begin(), delayBuffer.end(), 0.0f);
        writePos = 0;
        sideLowpass = 0.0f;
        haasMsSmoothed = 0.0f;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float mid  = 0.5f * (inputs[0][0] + inputs[0][1]);
        float side       = 0.5f * (inputs[0][0] - inputs[0][1]);

        // bass mono: one-pole lowpass on the side signal, subtracted away -
        // side content below the crossover collapses to the centre
        const float bassHz = param (pBassMono);
        if (bassHz > 1.0f)
        {
            const float c = 1.0f - std::exp ((float) (-juce::MathConstants<double>::twoPi
                                                      * bassHz / sampleRate));
            sideLowpass += c * (side - sideLowpass);
            side -= sideLowpass;
        }
        else
            sideLowpass = 0.0f;

        // width, with an additive modulation input (+-1 on Width Mod sweeps
        // +-100 % on top of the knob)
        const float widthMod = isInputConnected (1) ? inputs[1][0] : 0.0f;
        const float width = juce::jlimit (0.0f, 2.0f, param (pWidth) * 0.01f + widthMod);
        side *= width;

        float outL = mid + side;
        float outR = mid - side;

        // Haas micro-delay on the right channel (smoothed fractional read so
        // moving the knob glides instead of zipping)
        haasMsSmoothed += haasSmoothCoeff * (param (pHaas) - haasMsSmoothed);

        const int bufLen = (int) delayBuffer.size();
        delayBuffer[(size_t) writePos] = outR;

        if (haasMsSmoothed > 0.005f)
        {
            const float delaySamples = juce::jlimit (0.0f, (float) (bufLen - 2),
                (float) (haasMsSmoothed * 0.001 * sampleRate));
            const int di = (int) delaySamples;
            const float frac = delaySamples - (float) di;

            const int i0 = (writePos - di + bufLen) % bufLen;
            const int i1 = (i0 - 1 + bufLen) % bufLen;
            outR = delayBuffer[(size_t) i0]
                 + frac * (delayBuffer[(size_t) i1] - delayBuffer[(size_t) i0]);
        }

        writePos = (writePos + 1) % bufLen;

        outputs[0][0] = outL;
        outputs[0][1] = outR;
    }

private:
    static constexpr double kMaxHaasSeconds = 0.030;

    std::vector<float> delayBuffer;
    int writePos { 0 };
    float sideLowpass { 0.0f };
    float haasMsSmoothed { 0.0f };
    float haasSmoothCoeff { 0.002f };
};
