#include "VirusDistortionModule.h"

namespace aquavibrio
{

using namespace aquanode;

//==============================================================================
// The curves, in the hardware's own order:
//
//   0 Off            1 Light           2 Soft            3 Medium
//   4 Hard           5 Digital         6 Wave Shaper     7 Rectifier
//   8 Bit Reducer Old 9 Rate Reducer Old
//  10 Low Pass      11 High Pass      12 Wide
//  13 Soft Bounce   14 Hard Bounce    15 Sine Fold      16 Triangle Fold
//  17 Sawtooth Fold 18 Rate Reducer   19 Bit Reducer
//  20 Mint          21 Curry          22 Saffron        23 Onion
//  24 Pepper        25 Chili
//
// The five spice names are Access's own for a family of overdrives that get
// progressively more aggressive; here they are the same asymmetric soft clip
// with the asymmetry and the pre-emphasis climbing through the list, which is
// what makes them sound like variations rather than five separate effects.
//==============================================================================
float VirusDistortionModule::shape (int curve, float x, int ch, LaneState& s, float tone)
{
    const auto softClip = [] (float v, float amount)
    {
        return std::tanh (v * amount) / std::tanh (amount);
    };

    const auto fold = [] (float v, float limit)
    {
        // reflect back and forth between the rails instead of clipping
        while (v > limit || v < -limit)
            v = v > limit ? 2.0f * limit - v : -2.0f * limit - v;
        return v;
    };

    switch (curve)
    {
        case 0:  return x;                                   // Off
        case 1:  return softClip (x, 1.5f);                  // Light
        case 2:  return softClip (x, 3.0f);                  // Soft
        case 3:  return softClip (x, 6.0f);                  // Medium
        case 4:  return softClip (x, 14.0f);                 // Hard

        case 5:  return juce::jlimit (-1.0f, 1.0f, x * 2.0f); // Digital: straight clip

        case 6:  // Wave Shaper: a cubic, which leaves odd harmonics only
            return juce::jlimit (-1.0f, 1.0f, 1.5f * x - 0.5f * x * x * x);

        case 7:  return std::abs (x) * 2.0f - 1.0f;          // Rectifier

        case 8:  // Bit Reducer Old: coarse, no dither, deliberately ugly
        {
            const float steps = 8.0f;
            return std::floor (x * steps) / steps;
        }

        case 9:  // Rate Reducer Old: hold every Nth sample
        {
            if (++s.holdCount[ch] >= 8)
            {
                s.holdCount[ch] = 0;
                s.hold[ch] = x;
            }
            return s.hold[ch];
        }

        case 10: // Low Pass: a one-pole, since that is what the hardware means
        {
            const float a = 0.05f + tone * 0.9f;
            s.lowpass[ch] += (x - s.lowpass[ch]) * a;
            return s.lowpass[ch];
        }

        case 11: // High Pass
        {
            const float a = 0.05f + tone * 0.9f;
            s.highpass[ch] += (x - s.highpass[ch]) * a;
            return x - s.highpass[ch];
        }

        case 12: // Wide: asymmetric, so it makes even harmonics and sits wide
            return softClip (x + 0.25f, 5.0f) - softClip (0.25f, 5.0f);

        case 13: return fold (x, 1.0f) * 0.8f;               // Soft Bounce
        case 14: return fold (x * 1.8f, 1.0f);               // Hard Bounce

        case 15: // Sine Fold: the classic wavefolder
            return std::sin (x * juce::MathConstants<float>::pi * 1.5f);

        case 16: // Triangle Fold
            return fold (x * 2.0f, 1.0f);

        case 17: // Sawtooth Fold: wraps rather than reflecting, so it tears
            return std::fmod (x * 2.0f + 1.0f, 2.0f) - 1.0f;

        case 18: // Rate Reducer: hold length follows Tone, so it can sweep
        {
            const int period = 2 + (int) ((1.0f - tone) * 30.0f);

            if (++s.holdCount[ch] >= period)
            {
                s.holdCount[ch] = 0;
                s.hold[ch] = x;
            }
            return s.hold[ch];
        }

        case 19: // Bit Reducer: depth follows Tone
        {
            const float bits = 1.0f + tone * 11.0f;
            const float steps = std::pow (2.0f, bits);
            return std::round (x * steps) / steps;
        }

        default: // 20..25, the spice family
        {
            const int spice = juce::jlimit (0, 5, curve - 20);

            // asymmetry and pre-emphasis both climb through the list
            const float bias = 0.05f + 0.09f * (float) spice;
            const float amount = 2.0f + 3.2f * (float) spice;

            const float pre = x + bias;
            const float driven = softClip (pre, amount) - softClip (bias, amount);

            // the hotter ones lose a little bottom, which is what keeps them
            // from turning to mud as they get more aggressive
            const float a = 0.02f + 0.02f * (float) spice;
            s.highpass[ch] += (driven - s.highpass[ch]) * a;

            return driven - s.highpass[ch] * (float) spice * 0.15f;
        }
    }
}

//==============================================================================
void VirusDistortionModule::render (int lane, const StereoFrame* inputs, StereoFrame* outputs)
{
    if (lane < 0 || lane >= (int) state.size())
    {
        outputs[0] = inputs[0];
        return;
    }

    auto& s = state[(size_t) lane];

    const int curve = juce::jlimit (0, 25, (int) param (pCurve));
    const float drive = juce::jmax (0.01f, param (pDrive));
    const float tone = juce::jlimit (0.0f, 1.0f, param (pTone));
    const float highCut = juce::jlimit (0.0f, 1.0f, param (pHighCut));
    const float wet = juce::jlimit (0.0f, 1.0f, param (pDryWet) * 0.01f);
    const float level = param (pLevel);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float dry = inputs[0][ch];
        float v = shape (curve, dry * drive, ch, s, tone);

        // High Cut after the shaper: distortion without it is all fizz, which
        // is why the hardware puts one there.
        if (highCut < 1.0f)
        {
            const float a = 0.01f + highCut * 0.95f;
            s.lowpass[ch] += (v - s.lowpass[ch]) * a;
            v = s.lowpass[ch];
        }

        outputs[0][ch] = dry * (1.0f - wet) + v * level * wet;
    }
}

//==============================================================================
static ModuleDescriptor virusDistortionDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.virusdistortion";
    d.displayName = "Virus Distortion";
    d.description =
        "Twenty-six distortion curves in the Access Virus's own order: the saturation family, "
        "rectifier and bit and rate reducers, three wavefolders, and the five spice-named "
        "overdrives whose asymmetry climbs through the list. Tone drives whichever part of a "
        "curve is sweepable - the reducers' depth, the filters' corner - and High Cut tames the "
        "fizz afterwards.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 3;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo ("curve", "Curve",
            { "Off", "Light", "Soft", "Medium", "Hard", "Digital", "Wave Shaper", "Rectifier",
              "Bit Reducer Old", "Rate Reducer Old", "Low Pass", "High Pass", "Wide",
              "Soft Bounce", "Hard Bounce", "Sine Fold", "Triangle Fold", "Sawtooth Fold",
              "Rate Reducer", "Bit Reducer", "Mint", "Curry", "Saffron", "Onion", "Pepper",
              "Chili" }, 0, 0, 3),
        makeRotary ("drive",   "Drive",    0.01f, 40.0f, 1.0f, 0, {}, true),
        makeRotary ("tone",    "Tone",     0.0f, 1.0f, 0.5f, 1),
        makeRotary ("highCut", "High Cut", 0.0f, 1.0f, 1.0f, 1),
        makeRotary ("dryWet",  "Dry/Wet",  0.0f, 100.0f, 100.0f, 1, "%"),
        makeRotary ("level",   "Level",    0.0f, 2.0f, 0.9f, 1)
    };
    return d;
}

} // namespace aquavibrio

// The registration macro expands to an `extern "C"` variable, which has to
// sit at global scope - inside a namespace MSVC rejects it, and the error it
// reports points at <xlocale> rather than here. The vendored modules all do
// the same thing; they just have no namespace to be inside.
using VirusDistortionModule = aquavibrio::VirusDistortionModule;
AQUANODE_REGISTER_MODULE (VirusDistortionModule, aquavibrio::virusDistortionDescriptor)
