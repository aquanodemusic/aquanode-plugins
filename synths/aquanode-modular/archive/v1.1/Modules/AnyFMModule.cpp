#include "AnyFMModule.h"

using namespace aquanode;

void AnyFMModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    // ~20 ms one-pole parameter smoothing (replaces the original's SmoothedValue)
    const float target = param (pDepth);
    if (smoothedDepth < 0.0f)
        smoothedDepth = target;
    smoothedDepth += 0.002f * (target - smoothedDepth);

    const int mode = (int) param (pMode);
    const float modGain = param (pModGain);
    const float wetPos = param (pDryWet) * 0.01f;
    const float wetAmount = std::sin (wetPos * juce::MathConstants<float>::halfPi);
    const float dryAmount = std::cos (wetPos * juce::MathConstants<float>::halfPi);

    for (int c = 0; c < 2; ++c)
    {
        const float carrier = inputs[0][(size_t) c];
        float* delayData = buffer[c].data();

        delayData[writePos] = carrier;

        float modSample;
        if (mode == 2)   // Feedback: the output modulates itself
        {
            if (std::abs (carrier) < 0.0005f)
                feedback[c] *= 0.993f;   // starve the loop when the carrier stops
            modSample = feedback[c] * modGain;
        }
        else
        {
            modSample = inputs[1][(size_t) c] * modGain;
        }

        float wet;
        if (mode == 1)   // Ring Mod
        {
            wet = carrier * (modSample * 2.0f);
        }
        else             // Phase Mod / Feedback: modulated read position
        {
            float readPos = (float) writePos - (float) kCentreDelay + modSample * smoothedDepth;
            while (readPos < 0.0f)                 readPos += (float) kBufferSize;
            while (readPos >= (float) kBufferSize) readPos -= (float) kBufferSize;

            const int idxA = (int) readPos;
            const int idxB = (idxA + 1) % kBufferSize;
            const float frac = readPos - (float) idxA;
            wet = delayData[idxA] * (1.0f - frac) + delayData[idxB] * frac;

            if (mode == 2)
                feedback[c] = wet;
        }

        float out = carrier * dryAmount + wet * wetAmount;
        if (mode == 2)
            out = juce::jlimit (-0.75f, 0.75f, out);   // cap runaway self-oscillation

        outputs[0][(size_t) c] = out;
    }

    if (++writePos >= kBufferSize)
        writePos = 0;
}

static ModuleDescriptor anyFMDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.anyfm";
    d.displayName = "AnyFM";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 13;
    d.sockets = {
        audioIn  ("carrierIn", "Carrier In"),
        audioIn  ("modIn",     "Mod In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("depth",   "Depth",    0.0f, 1000.0f, 50.0f, 0, {}, true),
        makeCombo  ("mode",    "Mode",     { "Phase Mod", "Ring Mod", "Feedback" }, 0, 0, 2),
        makeRotary ("modGain", "Mod Gain", 0.0f, 12.0f, 1.0f, 0),
        makeRotary ("dryWet",  "Dry/Wet",  0.0f, 100.0f, 50.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (AnyFMModule, anyFMDescriptor)
