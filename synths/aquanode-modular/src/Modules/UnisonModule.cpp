#include "UnisonModule.h"

using namespace aquanode;

void UnisonModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
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

void UnisonModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const int count = juce::jlimit (1, kMaxUnison, (int) param (pUnison));
    const double baseFreq = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                               ! pool.isMuted (v), sampleRate));
    const int waveform = (int) param (pWaveform);
    const double detuneCents = param (pDetune);
    const float spread = param (pSpread) * 0.01f;
    const float blend = param (pBlend) * 0.01f;
    const float fmIn = inputs[0][0];

    float sumL = 0.0f, sumR = 0.0f, norm = 0.0f;

    for (int u = 0; u < count; ++u)
    {
        // -1..+1 across the stack (a single copy sits dead centre)
        const double pos = count > 1 ? (2.0 * u / (count - 1) - 1.0) : 0.0;

        // JP-8000 style: outer copies detune disproportionately far
        const double curve = pos * std::abs (pos) * 0.5 + pos * 0.5;
        const double cents = curve * detuneCents;
        const double freq = baseFreq * std::pow (2.0, cents / 1200.0);

        phase[v][u] += freq / sampleRate;
        phase[v][u] -= std::floor (phase[v][u]);

        double rp = phase[v][u] + (double) fmIn;
        rp -= std::floor (rp);

        const float y = waveform == 0 ? (float) (2.0 * rp - 1.0)          // saw
                                      : (rp < 0.5 ? 1.0f : -1.0f);       // square

        // centre copy vs the detuned sides
        const bool isCentre = (count > 1) && (u == count / 2) && (count % 2 == 1);
        const float lvl = isCentre ? (1.0f - blend) + blend * 0.5f : blend;

        const float pan = 0.5f + 0.5f * (float) pos * spread;   // 0 = L, 1 = R
        sumL += y * lvl * std::sqrt (1.0f - pan);
        sumR += y * lvl * std::sqrt (pan);
        norm += lvl;
    }

    const float inv = norm > 0.0001f ? 1.4f / norm : 0.0f;

    const bool envConnected = isInputConnected (1);
    const float gain = gate.next (v, envConnected, inputs[1][0], sampleRate) * param (pVolume) * inv;

    outputs[0][0] = sumL * gain;
    outputs[0][1] = sumR * gain;
}

static ModuleDescriptor unisonDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.unison";
    d.displayName = "Unison";
    d.description =
        "The supersaw: up to nine copies of one wave, detuned around the note and panned across "
        "the stereo field. Note this is NOT what Midi Add does - Midi Add plays extra NOTES, this "
        "thickens a SINGLE note, so it stays in tune whatever you play. Env In wants an ADSR.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 12;
    d.sockets = {
        modIn    ("fmIn",      "FM In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",   "Volume",   0.0f, 1.0f, 0.7f, 0),
        makeRotary ("unison",   "Unison",   1.0f, (float) UnisonModule::kMaxUnison, 7.0f, 0, {}, false, 1.0f),
        makeRotary ("detune",   "Detune",   0.0f, 50.0f, 18.0f, 0, "ct"),
        makeRotary ("spread",   "Spread",   0.0f, 100.0f, 70.0f, 1, "%"),
        makeRotary ("blend",    "Blend",    0.0f, 100.0f, 75.0f, 1, "%"),
        makeCombo  ("waveform", "Waveform", { "Saw", "Square" }, 0, 1, 2),
        makeRotary ("voices",   "Voices",   1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
        makeRotary ("glide",    "Glide",    0.0f, 1000.0f, 0.0f, 2, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (UnisonModule, unisonDescriptor)
