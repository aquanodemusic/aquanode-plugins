#include "CombFilterModule.h"

using namespace aquanode;

//==============================================================================
void CombFilterModule::prepare (double sr)
{
    SynthModule::prepare (sr);

    // the longest delay we can ever need is one cycle of the lowest frequency
    lineLength = (int) std::ceil (sr / kMinFreq) + 4;

    const size_t total = (size_t) kNumLanes * 2 * (size_t) lineLength;
    fbLine.assign (total, 0.0f);
    ffLine.assign (total, 0.0f);

    reset();
}

void CombFilterModule::reset()
{
    std::fill (fbLine.begin(), fbLine.end(), 0.0f);
    std::fill (ffLine.begin(), ffLine.end(), 0.0f);
    for (int i = 0; i < kNumLanes; ++i)
    {
        lanes[i].writePos = 0;
        lanes[i].loopLp[0] = lanes[i].loopLp[1] = 0.0f;
    }
}

void CombFilterModule::clearLane (int lane)
{
    if (lane < 0 || lane >= kNumLanes || fbLine.empty())
        return;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < lineLength; ++i)
        {
            fbLine[(size_t) lineIndex (lane, ch, i)] = 0.0f;
            ffLine[(size_t) lineIndex (lane, ch, i)] = 0.0f;
        }

    lanes[lane].writePos = 0;
    lanes[lane].loopLp[0] = lanes[lane].loopLp[1] = 0.0f;
}

float CombFilterModule::readLine (const std::vector<float>& line, int lane, int ch,
                                float delaySamples) const
{
    // fractional read: without it the comb would only be able to tune to
    // frequencies that divide the sample rate exactly, which is audible as
    // stepping when the pitch is swept
    const float d = juce::jlimit (1.0f, (float) (lineLength - 2), delaySamples);
    const int di = (int) d;
    const float frac = d - (float) di;

    const int i0 = (lanes[lane].writePos - di + lineLength) % lineLength;
    const int i1 = (i0 - 1 + lineLength) % lineLength;

    const float s0 = line[(size_t) lineIndex (lane, ch, i0)];
    const float s1 = line[(size_t) lineIndex (lane, ch, i1)];
    return s0 + frac * (s1 - s0);
}

void CombFilterModule::renderLane (int lane, const StereoFrame* inputs, StereoFrame* outputs)
{
    if (fbLine.empty() || lane < 0 || lane >= kNumLanes)
    {
        outputs[0] = inputs[0];
        return;
    }

    auto& L = lanes[lane];

    // Pitch In is the KeyTrack convention: semitones / 60. Patch KeyTrack
    // straight in and the comb follows the keyboard exactly.
    float freq = param (pFreq);
    if (isInputConnected (1))
    {
        const float semis = inputs[1][0] * 60.0f * param (pPitchDepth) * 0.01f;
        freq *= std::pow (2.0f, semis / 12.0f);
    }
    freq = juce::jlimit (kMinFreq, juce::jmin (kMaxFreq, (float) (sampleRate * 0.45)), freq);

    const float delaySamples = (float) sampleRate / freq;
    const float feedback = juce::jlimit (-0.995f, 0.995f, param (pFeedback) * 0.01f);
    const float blend = juce::jlimit (0.0f, 1.0f, param (pBlend) * 0.01f);
    const float wet = juce::jlimit (0.0f, 1.0f, param (pDryWet) * 0.01f);

    // Damping: 100 % = wide open, 0 % = only the lowest harmonics survive a
    // trip round the loop
    const float dampHz = juce::jlimit (200.0f, (float) (sampleRate * 0.45),
                                       param (pDamping) * 200.0f);
    const float dampCoeff = 1.0f - std::exp ((float) (-juce::MathConstants<double>::twoPi
                                                      * dampHz / sampleRate));

    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = inputs[0][ch];

        // ---- feedback comb: rings, and is what self-oscillates
        const float delayed = readLine (fbLine, lane, ch, delaySamples);
        L.loopLp[ch] += dampCoeff * (delayed - L.loopLp[ch]);
        const float yFb = x + feedback * L.loopLp[ch];

        // ---- feedforward comb: same delay, no loop, so notches without ring
        const float delayedX = readLine (ffLine, lane, ch, delaySamples);
        const float yFf = x + feedback * delayedX;

        fbLine[(size_t) lineIndex (lane, ch, L.writePos)] = juce::jlimit (-8.0f, 8.0f, yFb);
        ffLine[(size_t) lineIndex (lane, ch, L.writePos)] = x;

        const float combed = yFf * (1.0f - blend) + yFb * blend;

        // a feedback comb near self-oscillation gets loud fast; the soft clip
        // keeps a runaway loop musical instead of destructive
        outputs[0][ch] = x * (1.0f - wet) + std::tanh (combed * 0.7f) * 1.2f * wet;
    }

    L.writePos = (L.writePos + 1) % lineLength;
}

//==============================================================================
static ModuleDescriptor combFilterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.combfilter";
    d.displayName = "Comb Filter";
    d.description =
        "An actual delay-line comb unlike Center Comb: A delay one wavelength long fed back on "
        "itself, so it rings at a pitch. Positive Feedback reinforces every harmonic, negative "
        "cancels the even ones for a hollow clarinet tone, and the extremes self-oscillate. Pitch "
        "In takes KeyTrack or a Step Seq's Pitch Out (semitones/60) - KeyTrack straight in turns "
        "a Noise module into a Karplus-Strong string.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 5;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        modIn    ("pitchIn",  "Pitch In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("freq",       "Freq",       CombFilterModule::kMinFreq, CombFilterModule::kMaxFreq,
                                                220.0f, 0, "Hz", true),
        makeRotary ("feedback",   "Feedback",   -99.0f, 99.0f, 80.0f, 0, "%"),
        makeRotary ("damping",    "Damping",    0.0f, 100.0f, 45.0f, 0, "%"),
        makeRotary ("blend",      "Blend",      0.0f, 100.0f, 100.0f, 1, "%"),
        makeRotary ("pitchDepth", "Pitch Depth", 0.0f, 100.0f, 100.0f, 1, "%"),
        makeRotary ("dryWet",     "Dry/Wet",    0.0f, 100.0f, 100.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CombFilterModule, combFilterDescriptor)
