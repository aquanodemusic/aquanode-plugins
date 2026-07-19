#include "FormantFilterModule.h"

using namespace aquanode;

namespace
{
    // the same measured formant centres and relative levels the Formant
    // module sings with, in vowel order A E I O U
    const float kFreq[FormantFilterModule::kVowels][FormantFilterModule::kFormants] = {
        {  730.0f, 1090.0f, 2440.0f },   // A  "father"
        {  530.0f, 1840.0f, 2480.0f },   // E  "bed"
        {  270.0f, 2290.0f, 3010.0f },   // I  "feet"
        {  570.0f,  840.0f, 2410.0f },   // O  "law"
        {  300.0f,  870.0f, 2240.0f }    // U  "boot"
    };

    const float kLevel[FormantFilterModule::kVowels][FormantFilterModule::kFormants] = {
        { 1.0f, 0.50f, 0.20f },
        { 1.0f, 0.32f, 0.16f },
        { 1.0f, 0.20f, 0.13f },
        { 1.0f, 0.45f, 0.10f },
        { 1.0f, 0.28f, 0.08f }
    };
}

void FormantFilterModule::renderLane (int lane, const StereoFrame* inputs, StereoFrame* outputs)
{
    if (lane < 0 || lane >= kNumLanes)
    {
        outputs[0] = inputs[0];
        return;
    }

    // morph continuously through A E I O U; Vowel In sweeps on top of the knob
    const float vowelMod = isInputConnected (1) ? inputs[1][0] : 0.0f;
    const float vowelPos = juce::jlimit (0.0f, 1.0f,
        param (pVowel) * 0.01f + param (pVowelMod) * 0.01f * vowelMod) * (kVowels - 1);

    const int i0 = juce::jlimit (0, kVowels - 1, (int) vowelPos);
    const int i1 = juce::jmin (kVowels - 1, i0 + 1);
    const float t = vowelPos - (float) i0;

    const float sizeScale = std::pow (2.0f, param (pSize) / 12.0f);

    // Resonance sets the band-pass damping: less damping = sharper, ringier
    // peaks. The floor stops it from going unstable at the top of the knob.
    const float damping = juce::jlimit (0.02f, 1.0f, 1.0f - param (pResonance) * 0.0098f);

    const float wet = juce::jlimit (0.0f, 1.0f, param (pDryWet) * 0.01f);
    const float level = param (pLevel);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = inputs[0][ch];
        float sum = 0.0f;

        for (int f = 0; f < kFormants; ++f)
        {
            const float centre = (kFreq[i0][f] * (1.0f - t) + kFreq[i1][f] * t) * sizeScale;
            const float lvl    =  kLevel[i0][f] * (1.0f - t) + kLevel[i1][f] * t;

            const float fc = juce::jlimit (60.0f, (float) (sampleRate * 0.22), centre);
            const float fq = 2.0f * std::sin (juce::MathConstants<float>::pi * fc / (float) sampleRate);

            low[lane][ch][f] += fq * band[lane][ch][f];
            const float hp = x - low[lane][ch][f] - band[lane][ch][f] * damping;
            band[lane][ch][f] += fq * hp;

            sum += band[lane][ch][f] * lvl;
        }

        // the resonant peaks can stack well past unity on a rich input, so the
        // soft clip keeps a screaming vowel from turning into a blowout
        const float voiced = std::tanh (sum * 0.9f) * level;
        outputs[0][ch] = x * (1.0f - wet) + voiced * wet;
    }
}

//==============================================================================
static ModuleDescriptor formantFilterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.formant";
    d.displayName = "Formant Filter";
    d.description =
        "The Formant module's vowel bank aimed at whatever you patch in: three band-passes on the "
        "peaks of A E I O U, with Vowel morphing between them, so a saw pad turns into a choir "
        "and a drum loop starts talking. Vowel In wants an LFO for talking pads. Flexible lane: "
        "fed per-voice, each note can hold its own vowel.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 6;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        modIn    ("vowelIn",  "Vowel In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("vowel",     "Vowel",     0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("size",      "Size",      -12.0f, 12.0f, 0.0f, 0, "st"),
        makeRotary ("resonance", "Resonance", 0.0f, 100.0f, 88.0f, 0, "%"),
        makeRotary ("vowelMod",  "Vowel Mod", -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("dryWet",    "Dry/Wet",   0.0f, 100.0f, 100.0f, 1, "%"),
        makeRotary ("level",     "Level",     0.0f, 4.0f, 1.6f, 1)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (FormantFilterModule, formantFilterDescriptor)
