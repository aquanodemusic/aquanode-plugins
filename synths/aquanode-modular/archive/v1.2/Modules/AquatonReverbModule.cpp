#include "AquatonReverbModule.h"

using namespace aquanode;

// mutually-prime base delays (ms) for the 8 FDN lines, scaled by Size
static const double kLineMs[AquatonReverbModule::numLines] =
    { 29.7, 37.1, 41.1, 43.7, 53.3, 61.9, 71.3, 79.7 };

static const double kDiffMs[AquatonReverbModule::numDiffusers] = { 4.7, 3.6, 12.7, 9.3 };

void AquatonReverbModule::prepare (double sr)
{
    SynthModule::prepare (sr);

    // largest delay at max size (8x) + modulation headroom
    maxLine = (int) (sr * 0.0797 * 8.0) + 512;
    for (int l = 0; l < numLines; ++l)
        lines[l].assign ((size_t) maxLine, 0.0f);

    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < numDiffusers; ++i)
            diffusers[c][i].assign ((int) (kDiffMs[i] * 0.001 * sr) + 1);

    reset();
}

void AquatonReverbModule::reset()
{
    for (int l = 0; l < numLines; ++l)
    {
        std::fill (lines[l].begin(), lines[l].end(), 0.0f);
        writePos[l] = 0;
        lpState[l] = 0.0f;
        lfoPhase[l] = (double) l / numLines;   // spread LFO phases across lines
    }
    for (int c = 0; c < 2; ++c)
        for (auto& ap : diffusers[c])
            ap.clear();
}

float AquatonReverbModule::readLine (int l, double delaySamples) const
{
    double readPos = (double) writePos[l] - delaySamples;
    while (readPos < 0.0) readPos += maxLine;
    const int i0 = (int) readPos % maxLine;
    const int i1 = (i0 + 1) % maxLine;
    const float frac = (float) (readPos - std::floor (readPos));
    return lines[l][(size_t) i0] * (1.0f - frac) + lines[l][(size_t) i1] * frac;
}

void AquatonReverbModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (maxLine < 512)
    {
        outputs[0] = inputs[0];
        return;
    }

    const bool frozen = (int) param (pFreeze) == 1;
    const float size = param (pSize);
    const float damping = param (pDamping) * 0.01f;
    const float modRate = param (pModRate);
    const float modDepth = param (pModDepth);
    const float wet = param (pDryWet) * 0.01f;

    // freeze: unity feedback, no new input, no damping loss
    const float fb = frozen ? 1.0f
                            : juce::jlimit (0.0f, 0.98f, param (pFeedback) * 0.8f);
    const float lpCoeff = frozen ? 1.0f
                                 : juce::jlimit (0.05f, 1.0f, 1.0f - damping * 0.9f);

    // ---- input diffusion (allpass chain per channel) ------------------------
    float difL = inputs[0][0], difR = inputs[0][1];
    if (! frozen)
    {
        for (int i = 0; i < numDiffusers; ++i)
        {
            difL = diffusers[0][i].process (difL, 0.62f);
            difR = diffusers[1][i].process (difR, 0.62f);
        }
    }
    else
    {
        difL = difR = 0.0f;
    }

    // ---- read all modulated lines -------------------------------------------
    float lineOut[numLines];
    float sum = 0.0f;
    for (int l = 0; l < numLines; ++l)
    {
        lfoPhase[l] += modRate / sampleRate;
        lfoPhase[l] -= std::floor (lfoPhase[l]);
        const double lfo = std::sin (lfoPhase[l] * juce::MathConstants<double>::twoPi);

        const double dly = juce::jlimit (16.0, (double) maxLine - 4.0,
            kLineMs[l] * 0.001 * sampleRate * size + lfo * modDepth);

        lineOut[l] = readLine (l, dly);
        sum += lineOut[l];
    }

    // ---- Householder feedback matrix: y_i = x_i - (2/N) * sum ---------------
    const float h = 2.0f / (float) numLines;
    for (int l = 0; l < numLines; ++l)
    {
        // per-line lowpass damping in the loop
        lpState[l] += lpCoeff * ((lineOut[l] - h * sum) - lpState[l]);
        const float mixed = lpState[l] * fb;

        // inject the diffused input into alternating lines
        const float inject = (l & 1) ? difR : difL;
        lines[l][(size_t) writePos[l]] = juce::jlimit (-4.0f, 4.0f, mixed + inject * 0.5f);
        writePos[l] = (writePos[l] + 1) % maxLine;
    }

    // ---- stereo tap: odd lines left, even lines right ------------------------
    float wetL = 0.0f, wetR = 0.0f;
    for (int l = 0; l < numLines; ++l)
    {
        if (l & 1) wetL += lineOut[l];
        else       wetR += lineOut[l];
    }
    wetL *= 0.4f;
    wetR *= 0.4f;

    outputs[0][0] = inputs[0][0] * (1.0f - wet) + wetL * wet;
    outputs[0][1] = inputs[0][1] * (1.0f - wet) + wetR * wet;
}

static ModuleDescriptor aquatonDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.aquaton";
    d.displayName = "AquaReverb";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 6;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("size",     "Size",     0.1f, 8.0f, 1.0f, 0, {}, true),
        makeRotary ("feedback", "Feedback", 0.0f, 1.2f, 0.7f, 0),
        makeRotary ("damping",  "Damping",  0.0f, 100.0f, 40.0f, 0, "%"),
        makeRotary ("modRate",  "Mod Rate", 0.01f, 8.0f, 0.25f, 0, "Hz", true),
        makeRotary ("modDepth", "Mod Depth", 0.0f, 50.0f, 4.0f, 0),
        makeRotary ("dryWet",   "Dry/Wet",  0.0f, 100.0f, 35.0f, 1, "%"),
        makeCombo  ("freeze", "Freeze", { "Off", "On" }, 0, 1, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (AquatonReverbModule, aquatonDescriptor)
