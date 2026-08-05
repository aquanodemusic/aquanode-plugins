#include "TapeWobbleModule.h"

using namespace aquanode;

//==============================================================================
void TapeWobbleModule::prepare (double sr)
{
    SynthModule::prepare (sr);

    lineLength = (int) (kMaxDelaySeconds * sr) + 4;
    delayLine.assign ((size_t) lineLength * 2, 0.0f);

    // the random walks glide over ~120 ms: slower than flutter, so it reads as
    // drift rather than as another modulation source
    walkSmooth = 1.0f - std::exp ((float) (-1.0 / (0.12 * sr)));

    reset();
}

void TapeWobbleModule::reset()
{
    std::fill (delayLine.begin(), delayLine.end(), 0.0f);
    writePos = 0;
    wowPhase = flutterPhase = 0.0;
    wowWalk = wowTarget = flutWalk = flutTarget = 0.0f;
    wowCount = flutCount = 0.0;
    dropoutTimer = 0.0;
    dropoutDepth = 0.0f;
    dropoutEnv = 0.0f;
    dropoutLp[0] = dropoutLp[1] = 0.0f;
    hissEnv = 0.0f;
}

float TapeWobbleModule::readDelay (int ch, float delaySamples) const
{
    const float d = juce::jlimit (1.0f, (float) (lineLength - 2), delaySamples);
    const int di = (int) d;
    const float frac = d - (float) di;

    const int i0 = (writePos - di + lineLength) % lineLength;
    const int i1 = (i0 - 1 + lineLength) % lineLength;

    const float s0 = delayLine[(size_t) (ch * lineLength + i0)];
    const float s1 = delayLine[(size_t) (ch * lineLength + i1)];
    return s0 + frac * (s1 - s0);
}

void TapeWobbleModule::advanceWalk (float& value, float& target, double& countdown, double rateHz)
{
    countdown -= 1.0;
    if (countdown <= 0.0)
    {
        target = rng.nextFloat() * 2.0f - 1.0f;
        countdown = (sampleRate / juce::jmax (0.05, rateHz)) * (0.5 + rng.nextDouble());
    }
    value += walkSmooth * (target - value);
}

void TapeWobbleModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (delayLine.empty())
    {
        outputs[0] = inputs[0];
        return;
    }

    const float wow = param (pWow) * 0.01f;
    const float flutter = param (pFlutter) * 0.01f;
    const float drive = param (pDrive) * 0.01f;
    const float dropouts = param (pDropouts) * 0.01f;
    const float hiss = param (pHiss) * 0.01f;
    const float wet = juce::jlimit (0.0f, 1.0f, param (pDryWet) * 0.01f);

    // ---- the wobble: two sines, each pulled about by its own random walk
    const double wowRate = 0.5, flutterRate = 7.0;
    wowPhase += wowRate / sampleRate;
    wowPhase -= std::floor (wowPhase);
    flutterPhase += flutterRate / sampleRate;
    flutterPhase -= std::floor (flutterPhase);

    advanceWalk (wowWalk, wowTarget, wowCount, wowRate * 0.7);
    advanceWalk (flutWalk, flutTarget, flutCount, flutterRate * 0.5);

    const float wowMod = (float) std::sin (wowPhase * juce::MathConstants<double>::twoPi)
                            * 0.7f + wowWalk * 0.3f;
    const float flutMod = (float) std::sin (flutterPhase * juce::MathConstants<double>::twoPi)
                            * 0.6f + flutWalk * 0.4f;

    // park at the middle of the line so the wobble can push either way
    const float centreDelay = (float) (lineLength * 0.5);
    const float swing = (float) (0.004 * sampleRate);   // +-4 ms at full wow
    const float delaySamples = centreDelay
                                 + wowMod * wow * swing
                                 + flutMod * flutter * swing * 0.12f;

    // ---- dropouts: worn oxide, at random intervals
    dropoutTimer -= 1.0;
    if (dropoutTimer <= 0.0)
    {
        // more Dropouts = they come round more often, not that they get deeper
        const double meanSeconds = juce::jmap ((double) dropouts, 0.0, 1.0, 30.0, 0.7);
        dropoutTimer = sampleRate * meanSeconds * (0.4 + rng.nextDouble() * 1.2);
        dropoutDepth = dropouts > 0.0f ? 0.35f + rng.nextFloat() * 0.5f : 0.0f;
        dropoutEnv = 1.0f;
    }

    // each dropout fades away over ~80 ms rather than switching off
    dropoutEnv *= (float) std::exp (-1.0 / (0.08 * sampleRate));
    const float dip = 1.0f - dropoutEnv * dropoutDepth * dropouts;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = inputs[0][ch];

        delayLine[(size_t) (ch * lineLength + writePos)] = x;
        float y = readDelay (ch, delaySamples);

        // ---- tape saturation, gain compensated so Drive thickens rather
        // than just turns up
        if (drive > 0.0f)
        {
            const float pre = 1.0f + drive * 6.0f;
            y = std::tanh (y * pre) / std::tanh (pre * 0.7f) * 0.7f;
        }

        // ---- a dropout takes the top end with it, which is what makes it
        // sound like tape rather than like a volume dip
        const float dropoutHz = juce::jmap (juce::jlimit (0.0f, 1.0f, dropoutEnv * dropouts),
                                            0.0f, 1.0f, 18000.0f, 1800.0f);
        const float lpCoeff = 1.0f - std::exp ((float) (-juce::MathConstants<double>::twoPi
                                                        * dropoutHz / sampleRate));
        dropoutLp[ch] += lpCoeff * (y - dropoutLp[ch]);
        y = dropoutLp[ch] * dip;

        // ---- hiss, ducked under loud material like real tape noise
        if (hiss > 0.0f)
        {
            const float level = std::abs (x);
            hissEnv += 0.0005f * (level - hissEnv);
            const float duck = 1.0f / (1.0f + hissEnv * 8.0f);
            y += (rng.nextFloat() * 2.0f - 1.0f) * hiss * 0.02f * duck;
        }

        outputs[0][ch] = x * (1.0f - wet) + y * wet;
    }

    writePos = (writePos + 1) % lineLength;
}

//==============================================================================
static ModuleDescriptor tapeWobbleDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.tapewobble";
    d.displayName = "Tape Wobble";
    d.description =
        "What a worn tape machine does to a signal: Wow is the slow sag of an off-centre reel, "
        "Flutter the fast jitter, and both ride a random walk so the wobble never sounds "
        "like an LFO. Drive is gain-compensated tape saturation, Dropouts are worn oxide, and "
        "Hiss ducks under loud material like real tape noise. The global-lane sibling of Analog "
        "Drift: that wobbles an oscillator from the inside, this wobbles a finished signal from "
        "the outside.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 24;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("wow",      "Wow",      0.0f, 100.0f, 25.0f, 0, "%"),
        makeRotary ("flutter",  "Flutter",  0.0f, 100.0f, 30.0f, 0, "%"),
        makeRotary ("drive",    "Drive",    0.0f, 100.0f, 20.0f, 0, "%"),
        makeRotary ("dropouts", "Dropouts", 0.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("hiss",     "Hiss",     0.0f, 100.0f, 8.0f, 1, "%"),
        makeRotary ("dryWet",   "Dry/Wet",  0.0f, 100.0f, 100.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (TapeWobbleModule, tapeWobbleDescriptor)
