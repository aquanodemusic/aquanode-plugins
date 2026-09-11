#include "LFOModule.h"

using namespace aquanode;

void LFOModule::processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs)
{
    phase[v] += param (pRate) / sampleRate;
    if (phase[v] >= 1.0)
    {
        phase[v] -= std::floor (phase[v]);
        shValue[v] = random.nextFloat() * 2.0f - 1.0f;   // new random value each cycle
    }

    float wave = 0.0f;
    switch ((int) param (pWaveform))
    {
        case 0: wave = (float) std::sin (phase[v] * juce::MathConstants<double>::twoPi); break;   // Sine
        case 1: wave = (float) (1.0 - 4.0 * std::abs (phase[v] - 0.5)); break;                    // Triangle
        case 2: wave = (float) (2.0 * phase[v] - 1.0); break;                                     // Saw Up
        case 3: wave = (float) (1.0 - 2.0 * phase[v]); break;                                     // Saw Down
        case 4: wave = phase[v] < 0.5 ? 1.0f : -1.0f; break;                                      // Square
        case 5: wave = shValue[v]; break;                                                         // Random S&H
    }

    // Offset shifts the centre point, so the output can be made fully unipolar
    const float out = juce::jlimit (-1.0f, 1.0f, wave * param (pLevel) + param (pOffset));
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor lfoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.lfo";
    d.displayName = "LFO";
    d.description =
        "A per-voice low-frequency oscillator, phase-retriggered at note-on so every note gets "
        "the same shape from its start. The patch's default modulation source: drop its Mod Out "
        "on any knob to make that knob move. For custom shapes use the Draw LFO Module.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 0;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = {
        makeRotary ("rate", "Rate", 0.1f, 100.0f, 1.0f, 0, "Hz", true),
        makeCombo  ("waveform", "Waveform",
                    { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random S&H" }, 0, 0, 2),
        makeRotary ("offset", "Offset", -1.0f, 1.0f, 0.0f, 0),
        makeRotary ("level",  "Level",  0.0f, 1.0f, 1.0f, 0)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (LFOModule, lfoDescriptor)
