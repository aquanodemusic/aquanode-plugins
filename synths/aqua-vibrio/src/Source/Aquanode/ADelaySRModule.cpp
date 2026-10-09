#include "ADelaySRModule.h"

using namespace aquanode;

void ADelaySRModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    const int size = juce::jmax (16, (int) (sr * 5.2));   // 5 s max delay
    for (int c = 0; c < 2; ++c)
        line[c].assign ((size_t) size, 0.0f);
    reset();
}

float ADelaySRModule::tapEnvelope (float phase, float atkFrac, float relFrac)
{
    // fade-in over the first atkFrac of the delay window, fade-out over the
    // last relFrac - straight from the original's tap shaping
    float env = 1.0f;
    if (atkFrac > 0.0001f && phase < atkFrac)
        env *= phase / atkFrac;
    if (relFrac > 0.0001f && phase > 1.0f - relFrac)
        env *= (1.0f - phase) / relFrac;
    return juce::jlimit (0.0f, 1.0f, env);
}

void ADelaySRModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int size = (int) line[0].size();
    if (size < 16)
    {
        outputs[0] = inputs[0];
        return;
    }

    // smoothed fractional delay time; snaps on huge jumps like the original
    // A slow LFO on the delay time. A delay whose time never moves sounds
    // digital in a way tape never did; this is what the Virus's Delay Rate
    // and Depth are for.
    modPhase += (double) param (pModRate) / sampleRate;
    modPhase -= std::floor (modPhase);

    const double modDepth = (double) param (pModDepth) * 0.01;
    const double wobble = 1.0 + 0.03 * modDepth
                                  * std::sin (modPhase * juce::MathConstants<double>::twoPi);

    // Ratio offsets the right channel against the left, which is where a
    // delay stops being one echo in the middle and becomes two in the image.
    const double ratioR = 1.0 + (double) param (pRatio);

    const auto smooth = [size] (double& smoothed, double targetSamples)
    {
        const double target = juce::jlimit (1.0, (double) size - 2.0, targetSamples);
        const double cur = smoothed;
        const double jump = cur > 0.0 ? (target > cur ? target / cur : cur / target) : 1.0;

        if (jump > 10.0)
            smoothed = target;          // snap on huge jumps, as before
        else
            smoothed += 0.0008 * (target - smoothed);

        return juce::jmax (1.0, smoothed);
    };

    const double baseSamples = param (pTime) * 0.001 * sampleRate * wobble;
    const double dt = smooth (smoothedDelay, baseSamples);
    const double dtR = smooth (smoothedDelayR, baseSamples * ratioR);

    const float atkFrac = param (pAttack) * 0.01f;
    const float relFrac = param (pRelease) * 0.01f;
    const float tapVol = param (pTapVolume) * 0.01f;
    const float wet = param (pDryWet) * 0.01f;
    const int mode = (int) param (pMode);   // 0 Mono, 1 Stereo, 2 Ping Pong, 3 Hard PP

    const float inL = inputs[0][0];
    const float inR = inputs[0][1];

    // fractional read, linear interp
    double readF = (double) writePos - dt;
    if (readF < 0.0) readF += size;
    const int rp0 = (int) readF % size;
    const int rp1 = (rp0 + 1) % size;
    const float frac = (float) (readF - std::floor (readF));

    const float delL = line[0][(size_t) rp0] * (1.0f - frac) + line[0][(size_t) rp1] * frac;

    // the right channel reads at its own time
    double readFR = (double) writePos - dtR;
    if (readFR < 0.0) readFR += size;
    const int rp0R = (int) readFR % size;
    const int rp1R = (rp0R + 1) % size;
    const float fracR = (float) (readFR - std::floor (readFR));

    const float delR = line[1][(size_t) rp0R] * (1.0f - fracR) + line[1][(size_t) rp1R] * fracR;

    const float env = tapEnvelope ((float) cyclePhase, atkFrac, relFrac);

    float wetL, wetR, fbL, fbR;
    switch (mode)
    {
        case 0:   // Mono
        {
            const float inM = (inL + inR) * 0.5f;
            const float delM = (delL + delR) * 0.5f;
            wetL = wetR = delM * env;
            fbL = fbR = inM + delM * tapVol;
            break;
        }
        case 2:   // Ping Pong (cross-feedback)
            wetL = delL * env;  wetR = delR * env;
            fbL = inL + delR * tapVol;
            fbR = inR + delL * tapVol;
            break;
        case 3:   // Hard Ping Pong - each tap 100% L or 100% R, alternating
        {
            const bool tapLeft = (hardPPTap & 1) == 0;
            const float delMix = (delL + delR) * 0.5f;
            const float inMix = (inL + inR) * 0.5f;
            wetL = tapLeft  ? delMix * env : 0.0f;
            wetR = !tapLeft ? delMix * env : 0.0f;
            const float fb = inMix + delMix * tapVol;   // write both channels full-level
            fbL = fbR = fb;
            break;
        }
        default:  // Stereo
            wetL = delL * env;  wetR = delR * env;
            fbL = inL + delL * tapVol;
            fbR = inR + delR * tapVol;
            break;
    }

    outputs[0][0] = inL * (1.0f - wet) + wetL * wet;
    outputs[0][1] = inR * (1.0f - wet) + wetR * wet;

    line[0][(size_t) writePos] = juce::jlimit (-4.0f, 4.0f, fbL);
    line[1][(size_t) writePos] = juce::jlimit (-4.0f, 4.0f, fbR);

    writePos = (writePos + 1) % size;
    cyclePhase += 1.0 / dt;
    if (cyclePhase >= 1.0)
    {
        cyclePhase -= 1.0;
        ++hardPPTap;
    }
}

static ModuleDescriptor adelaysrDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.adelaysr";
    d.displayName = "ADSR Delay";
    d.description =
        "A creative delay/resonator: one free-running tap with a shapeable per-cycle envelope and "
        "four routing modes including hard ping-pong. The time knob reaches into the "
        "sub-millisecond range, where it stops being a delay and starts resonating.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 5;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        // ORDER MATTERS: must match the ParamIndex enum in the header.
        makeRotary ("time",      "Time",     0.02f, 5000.0f, 500.0f, 0, "ms", true),
        makeRotary ("tapVolume", "Tap Vol",  0.0f, 120.0f, 50.0f, 0, "%"),
        makeRotary ("attack",    "Attack",   0.0f, 50.0f, 0.0f, 0, "%"),
        makeRotary ("release",   "Release",  0.0f, 50.0f, 0.0f, 0, "%"),
        makeRotary ("dryWet",    "Dry/Wet",  0.0f, 100.0f, 50.0f, 0, "%"),
        makeCombo  ("mode", "Mode", { "Mono", "Stereo", "Ping Pong", "Hard PP" }, 1, 1, 2),
        makeRotary ("ratio",    "Ratio",     -0.5f, 0.5f, 0.0f, 1),
        makeRotary ("modRate",  "Mod Rate",  0.0f, 8.0f, 0.0f, 2, "Hz"),
        makeRotary ("modDepth", "Mod Depth", 0.0f, 100.0f, 0.0f, 2, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ADelaySRModule, adelaysrDescriptor)
