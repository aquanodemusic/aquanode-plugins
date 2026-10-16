#include "EchoModule.h"

using namespace aquanode;

namespace
{
    const juce::StringArray& echoDivisions()
    {
        static const juce::StringArray c { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.",
                                           "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1/1" };
        return c;
    }

    double echoBeats (int i)
    {
        static const double b[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                    2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0 };
        return b[juce::jlimit (0, 13, i)];
    }
}

void EchoModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    const auto len = (size_t) (sr * kMaxSeconds) + 8;
    lineL.assign (len, 0.0f);
    lineR.assign (len, 0.0f);
    smCoef = 1.0f - std::exp (-1.0f / (0.12f * (float) sr));   // tape-like glide on time changes
    reset();
}

void EchoModule::reset()
{
    std::fill (lineL.begin(), lineL.end(), 0.0f);
    std::fill (lineR.begin(), lineR.end(), 0.0f);
    writePos = 0;
    smoothL = smoothR = 0.0;
    hpL = hpR = lpL = lpR = SVF();
    wobblePhase = 0.0;
}

float EchoModule::readLine (const std::vector<float>& line, double delaySamples) const
{
    const int size = (int) line.size();
    double rp = (double) writePos - delaySamples;
    while (rp < 0.0)
        rp += size;
    const int i0 = (int) rp % size;
    const int i1 = (i0 + 1) % size;
    const float f = (float) (rp - std::floor (rp));
    return line[(size_t) i0] + f * (line[(size_t) i1] - line[(size_t) i0]);
}

void EchoModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (lineL.empty())
    {
        outputs[0] = inputs[0];
        return;
    }

    const double sr = sampleRate;
    double seconds = param (pTime) * 0.001;
    if (param (pSync) > 0.5f)
        seconds = echoBeats ((int) std::lround (param (pDivision))) * 60.0 / juce::jmax (20.0, tempoBpm);
    seconds = juce::jlimit (0.001, kMaxSeconds - 0.05, seconds);

    const int style = (int) std::lround (param (pStyle));   // 0 stereo, 1 ping-pong, 2 mono
    const double offset = style == 0 ? param (pOffset) * 0.01 * 0.5 : 0.0;

    // slow drift on the time (two detuned sines), in samples
    wobblePhase += 0.31 / sr;
    if (wobblePhase >= 1.0) wobblePhase -= 1.0;
    const double wob = param (pWobble) * 0.01 * 0.0025 * sr
                     * (std::sin (juce::MathConstants<double>::twoPi * wobblePhase)
                        + 0.5 * std::sin (juce::MathConstants<double>::twoPi * wobblePhase * 3.7));

    const double targetL = seconds * (1.0 - offset) * sr;
    const double targetR = seconds * (1.0 + offset) * sr;
    if (smoothL <= 0.0) { smoothL = targetL; smoothR = targetR; }
    smoothL += smCoef * (targetL - smoothL);
    smoothR += smCoef * (targetR - smoothR);

    const float yL = readLine (lineL, juce::jmax (2.0, smoothL + wob));
    const float yR = readLine (lineR, juce::jmax (2.0, smoothR - wob));

    // band-pass in the loop: Tone is the centre, Width the octaves around it
    const float tone = juce::jlimit (40.0f, 16000.0f, param (pTone));
    const float halfOct = juce::jlimit (0.25f, 5.0f, param (pWidth) * 0.5f);
    const float fLo = juce::jlimit (20.0f, (float) sr * 0.45f, tone / std::exp2 (halfOct));
    const float fHi = juce::jlimit (30.0f, (float) sr * 0.45f, tone * std::exp2 (halfOct));
    const float gLo = std::tan (juce::MathConstants<float>::pi * fLo / (float) sr);
    const float gHi = std::tan (juce::MathConstants<float>::pi * fHi / (float) sr);
    const float k = 1.41421356f;

    const float fbL = lpL.low (hpL.high (yL, gLo, k), gHi, k);
    const float fbR = lpR.low (hpR.high (yR, gLo, k), gHi, k);
    const float fb = juce::jlimit (0.0f, 1.1f, param (pFeedback) * 0.01f);

    const float inL = inputs[0][0], inR = inputs[0][1];
    float wL, wR;
    switch (style)
    {
        case 1:    // ping-pong: the input enters left, the repeats cross over
            wL = 0.5f * (inL + inR) + fb * fbR;
            wR = fb * fbL;
            break;
        case 2:    // mono
            wL = wR = 0.5f * (inL + inR) + fb * 0.5f * (fbL + fbR);
            break;
        default:
            wL = inL + fb * fbL;
            wR = inR + fb * fbR;
            break;
    }
    // a ceiling above full scale only, so high feedback saturates like tape
    // instead of running away - normal levels pass untouched
    auto ceiling = [] (float x) { return std::abs (x) <= 1.0f ? x : std::copysign (1.0f + std::tanh (std::abs (x) - 1.0f), x); };
    lineL[(size_t) writePos] = ceiling (wL);
    lineR[(size_t) writePos] = ceiling (wR);
    writePos = (writePos + 1) % (int) lineL.size();

    const float mix = juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f);
    outputs[0][0] = inL * (1.0f - mix) + yL * mix;
    outputs[0][1] = inR * (1.0f - mix) + yR * mix;
}

//==============================================================================
static ModuleDescriptor echoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.echo";
    d.displayName = "Echo";
    d.description =
        "A tape-style echo: time in ms or locked to the tempo (straight, dotted, triplet), Stereo / Ping-Pong / "
        "Mono, and a band-pass in the feedback loop - Tone is its centre, Width how many octaves pass - so "
        "every repeat gets darker and thinner. Time changes glide the repeats' pitch like tape; Wobble adds "
        "drift; Offset spreads left and right times in Stereo.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 1;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("time", "Time", 1.0f, 3900.0f, 375.0f, 0, "ms", true).visibleWhen ("sync", 0.0f),
        makeSteppedList ("division", "Division", echoDivisions(), 6, 0).visibleWhen ("sync", 1.0f),
        makeRotary ("feedback", "Feedback", 0.0f, 110.0f, 40.0f, 0, "%"),
        makeRotary ("tone",     "Tone",     40.0f, 16000.0f, 1800.0f, 0, "Hz", true),
        makeRotary ("width",    "Width",    0.5f, 10.0f, 4.0f, 0, "oct"),
        makeCombo ("sync",  "Sync",  { "Free (ms)", "Tempo" }, 1, 1, 2),
        makeCombo ("style", "Style", { "Stereo", "PingPong", "Mono" }, 1, 1, 2),
        makeRotary ("mix",    "Mix",    0.0f, 100.0f, 30.0f, 1, "%", false, 1.0f),
        makeRotary ("offset", "Offset", 0.0f, 100.0f, 0.0f, 2, "%", false, 1.0f).visibleWhen ("style", 0.0f),
        makeRotary ("wobble", "Wobble", 0.0f, 100.0f, 0.0f, 2, "%", false, 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (EchoModule, echoDescriptor)
