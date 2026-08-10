#include "BowedModule.h"

using namespace aquanode;

void BowedModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void BowedModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double freq = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                           ! pool.isMuted (v), sampleRate));
    const double delaySamples = juce::jlimit (4.0, (double) maxDelaySamples - 2.0,
                                              sampleRate / juce::jmax (20.0, freq));

    // read the string
    double readPos = (double) writeIndex[v] - delaySamples;
    while (readPos < 0.0)
        readPos += maxDelaySamples;
    const int i0 = (int) readPos % maxDelaySamples;
    const int i1 = (i0 + 1) % maxDelaySamples;
    const float frac = (float) (readPos - std::floor (readPos));
    const float delayed = line[v][(size_t) i0] * (1.0f - frac) + line[v][(size_t) i1] * frac;

    // second tap: bowing/blowing away from the end notches out the harmonics
    // whose nodes fall on that point (a comb, physically)
    const double posTap = delaySamples * juce::jlimit (0.02, 0.5, (double) param (pPosition) * 0.01);
    double rp2 = (double) writeIndex[v] - posTap;
    while (rp2 < 0.0)
        rp2 += maxDelaySamples;
    const float atPos = line[v][(size_t) ((int) rp2 % maxDelaySamples)];

    // loop damping = body brightness
    const float dampCoeff = juce::jlimit (0.02f, 1.0f, 1.0f - param (pDamp) * 0.0095f);
    lpState[v] += dampCoeff * (delayed - lpState[v]);

    const bool envConnected = isInputConnected (1);
    const float envGain = gate.next (v, envConnected, inputs[1][0], sampleRate);

    // Pressure In rides on top of the knob, and the gate opens/closes the bow
    const float pressure = juce::jlimit (0.0f, 1.5f,
        (param (pPressure) * 0.01f + param (pPressMod) * 0.01f * inputs[0][0]) * envGain);

    float excitation;
    if ((int) param (pMode) == 0)
    {
        // BOW: velocity difference between bow and string drives a friction
        // curve. Small differences stick (energy in), large ones slip (nothing).
        const float bowVelocity = pressure;
        const float delta = bowVelocity - (lpState[v] + atPos * 0.4f);
        const float slip = 1.0f / (1.0f + 90.0f * delta * delta);   // stick-slip
        excitation = delta * slip * 0.9f;
    }
    else
    {
        // BLOW: pressure across a reed that closes as the difference grows
        const float diff = pressure - lpState[v] * 0.8f;
        const float reed = juce::jlimit (-1.0f, 1.0f, 1.0f - 1.4f * diff);
        excitation = diff * reed * 0.7f;
    }

    const float looped = juce::jlimit (-2.0f, 2.0f, lpState[v] * 0.995f + excitation);
    line[v][(size_t) writeIndex[v]] = looped;
    if (++writeIndex[v] >= maxDelaySamples)
        writeIndex[v] = 0;

    const float out = std::tanh ((looped - atPos * 0.5f) * 1.2f) * param (pVolume);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor bowedDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.bowed";
    d.displayName = "Bowed";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 14;
    d.sockets = {
        modIn    ("pressIn",   "Pressure In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",   "Volume",   0.0f, 1.0f, 0.7f, 0),
        makeCombo  ("mode",     "Mode",     { "Bow", "Blow" }, 0, 0, 2),
        makeRotary ("pressure", "Pressure", 0.0f, 100.0f, 45.0f, 0, "%"),
        makeRotary ("position", "Position", 2.0f, 50.0f, 14.0f, 1, "%"),
        makeRotary ("damp",     "Damp",     0.0f, 100.0f, 35.0f, 1, "%"),
        makeRotary ("pressMod", "Press Mod", -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("voices",   "Voices",   1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
        makeRotary ("glide",    "Glide",    0.0f, 1000.0f, 0.0f, 2, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (BowedModule, bowedDescriptor)
