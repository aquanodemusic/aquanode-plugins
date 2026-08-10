#include "ResonateModule.h"

using namespace aquanode;

float ResonateModule::readDelayLinear (int channel, double delaySamples) const
{
    double readPos = (double) writeIndex[channel] - delaySamples;
    while (readPos < 0.0) readPos += maxDelaySamples;
    const int i0 = (int) readPos % maxDelaySamples;
    const int i1 = (i0 + 1) % maxDelaySamples;
    const float frac = (float) (readPos - std::floor (readPos));
    return line[channel][(size_t) i0] * (1.0f - frac) + line[channel][(size_t) i1] * frac;
}

void ResonateModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const double baseDelay = juce::jlimit (2.0, (double) maxDelaySamples - 2.0,
        sampleRate / midiNoteToHz ((int) param (pNote)));

    // Decay -> feedback with a musical taper (default 95% rings strongly)
    const float decay = param (pDecay) * 0.01f;
    const float feedback = juce::jlimit (0.0f, 0.9995f, std::sqrt (decay) * 0.9995f);

    // Color -> feedback-loop LPF coefficient (bright at 100%)
    const float color = param (pColor) * 0.01f;
    const float lpfCoeff = juce::jlimit (0.02f, 1.0f, 0.05f + 0.95f * color * color);

    const float lfoRate = param (pLfoRate);
    const float lfoDepthCents = param (pLfoDepth);
    const bool modeB = (int) param (pMode) == 1;
    const float wet = param (pDryWet) * 0.01f;

    for (int c = 0; c < 2; ++c)
    {
        const float in = inputs[0][(size_t) c];

        // sine LFO detune in the cents domain, per channel (original's scheme)
        double actualDelay = modeB ? baseDelay * 0.5 : baseDelay;
        if (lfoDepthCents > 0.001f && lfoRate > 0.0f)
        {
            lfoPhase[c] += lfoRate / sampleRate;
            if (lfoPhase[c] >= 1.0) lfoPhase[c] -= 1.0;
            const double lfo = std::sin (lfoPhase[c] * juce::MathConstants<double>::twoPi);
            actualDelay /= std::pow (2.0, lfo * lfoDepthCents / 1200.0);
        }
        actualDelay = juce::jlimit (2.0, (double) maxDelaySamples - 2.0, actualDelay);

        const float delayed = readDelayLinear (c, actualDelay);
        lpfState[c] += lpfCoeff * (delayed - lpfState[c]);
        const float damped = lpfState[c];

        // Mode A: positive-feedback comb; Mode B: negative feedback (square-ish)
        const float resOut = modeB ? std::tanh (in - feedback * damped)
                                   : std::tanh (in + feedback * damped);

        line[(size_t) c][(size_t) writeIndex[c]] = resOut;
        if (++writeIndex[c] >= maxDelaySamples)
            writeIndex[c] = 0;

        outputs[0][(size_t) c] = in * (1.0f - wet) + resOut * wet;
    }
}

static ModuleDescriptor resonateDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.resonate";
    d.displayName = "Resonator";
    d.description =
        "A tuned comb resonator set in MIDI note numbers: Mode A is a positive-feedback comb with "
        "damping, Mode B a negative-feedback one with a squarer character. Feed it Noise for a "
        "sung, pitched drone. A staple effect in atmospheric soundscape music.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 9;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeSteppedList ("note", "Note", midiNoteNameChoices(), 60, 0),
        makeRotary ("decay",    "Decay",     0.0f, 100.0f, 95.0f, 0, "%"),
        makeRotary ("color",    "Color",     0.0f, 100.0f, 74.0f, 0, "%"),
        makeRotary ("lfoDepth", "LFO Depth", 0.0f, 20.0f, 0.0f, 0),
        makeRotary ("dryWet",   "Dry/Wet",   0.0f, 100.0f, 50.0f, 0, "%"),
        makeCombo  ("mode", "Mode", { "A", "B" }, 0, 1, 2),
        makeRotary ("lfoRate", "LFO Rate", 0.1f, 5.0f, 0.5f, 1, "Hz", true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ResonateModule, resonateDescriptor)
