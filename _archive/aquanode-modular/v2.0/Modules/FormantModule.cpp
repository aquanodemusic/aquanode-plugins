#include "FormantModule.h"

using namespace aquanode;

namespace
{
    // measured formant centres and relative levels for a sung male voice,
    // in vowel order A E I O U
    const float kFreq[FormantModule::kVowels][FormantModule::kFormants] = {
        {  730.0f, 1090.0f, 2440.0f },   // A  "father"
        {  530.0f, 1840.0f, 2480.0f },   // E  "bed"
        {  270.0f, 2290.0f, 3010.0f },   // I  "feet"
        {  570.0f,  840.0f, 2410.0f },   // O  "law"
        {  300.0f,  870.0f, 2240.0f }    // U  "boot"
    };

    const float kLevel[FormantModule::kVowels][FormantModule::kFormants] = {
        { 1.0f, 0.50f, 0.20f },
        { 1.0f, 0.32f, 0.16f },
        { 1.0f, 0.20f, 0.13f },
        { 1.0f, 0.45f, 0.10f },
        { 1.0f, 0.28f, 0.08f }
    };
}

void FormantModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
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

void FormantModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double freq = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                           ! pool.isMuted (v), sampleRate));

    // glottal source: a saw sharpened by Shape into a narrow pulse, which is
    // what gives the formants something harmonically rich to resonate on
    phase[v] += freq / sampleRate;
    phase[v] -= std::floor (phase[v]);

    const float shape = param (pShape) * 0.01f;
    const float saw = (float) (2.0 * phase[v] - 1.0);
    const float pulse = phase[v] < (0.5 - 0.45 * shape) ? 1.0f : -1.0f;
    float source = saw * (1.0f - shape) + pulse * shape;

    const float breath = param (pBreath) * 0.01f;
    if (breath > 0.0f)
        source = source * (1.0f - breath) + (random.nextFloat() * 2.0f - 1.0f) * breath;

    // morph continuously through A E I O U
    const float vowelPos = juce::jlimit (0.0f, 1.0f,
        param (pVowel) * 0.01f + param (pVowelMod) * 0.01f * inputs[0][0]) * (kVowels - 1);
    const int i0 = juce::jlimit (0, kVowels - 1, (int) vowelPos);
    const int i1 = juce::jmin (kVowels - 1, i0 + 1);
    const float t = vowelPos - (float) i0;

    const float sizeScale = std::pow (2.0f, param (pSize) / 12.0f);

    float sum = 0.0f;
    for (int f = 0; f < kFormants; ++f)
    {
        const float centre = (kFreq[i0][f] * (1.0f - t) + kFreq[i1][f] * t) * sizeScale;
        const float level  =  kLevel[i0][f] * (1.0f - t) + kLevel[i1][f] * t;

        // Chamberlin state-variable band-pass, fairly high Q so it "rings"
        const float fc = juce::jlimit (60.0f, (float) (sampleRate * 0.22), centre);
        const float fq = 2.0f * std::sin (juce::MathConstants<float>::pi * fc / (float) sampleRate);

        low[v][f] += fq * band[v][f];
        const float hp = source - low[v][f] - band[v][f] * 0.12f;
        band[v][f] += fq * hp;

        sum += band[v][f] * level;
    }

    const bool envConnected = isInputConnected (1);
    const float gain = gate.next (v, envConnected, inputs[1][0], sampleRate);

    const float out = std::tanh (sum * 0.9f) * gain * param (pVolume);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor formantDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.formant";
    d.displayName = "Formant";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 13;
    d.sockets = {
        modIn    ("vowelIn",   "Vowel In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",   "Volume",   0.0f, 1.0f, 0.7f, 0),
        makeRotary ("vowel",    "Vowel",    0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("size",     "Size",     -12.0f, 12.0f, 0.0f, 0, "st"),
        makeRotary ("breath",   "Breath",   0.0f, 100.0f, 5.0f, 1, "%"),
        makeRotary ("shape",    "Shape",    0.0f, 100.0f, 40.0f, 1, "%"),
        makeRotary ("vowelMod", "Vowel Mod", -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("voices",   "Voices",   1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
        makeRotary ("glide",    "Glide",    0.0f, 1000.0f, 0.0f, 2, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (FormantModule, formantDescriptor)
