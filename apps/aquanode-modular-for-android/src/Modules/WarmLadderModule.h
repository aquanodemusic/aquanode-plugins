#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Warm Ladder - a 24 dB resonant low-pass in the style of classic polysynth chips,
// with saturating stages, resonance that thins the bass as the chip does, and
// self-oscillation near the top of Resonance. EG Int (10 = +6 octaves at full
// envelope), Kbd Track (100 % = the cutoff follows the key one-to-one, around
// middle C) and MG Level (10 = +-2 octaves) are its controls.
// Flexible lane: one filter per note when fed per-voice.
// Inputs: 0 = Audio In, 1 = EG In, 2 = MG In. Output: 0 = Audio Out.
class WarmLadderModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCutoff = 0, pResonance, pEgInt, pKbdTrack, pMgLevel, pInput };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
        {
            voiceReset (v);
            noteOf[v] = 60;
        }
    }

    void voiceReset (int v) override
    {
        for (auto& f : filt[v])
            f.reset();
    }

    void voiceNoteOn (int v, int note, bool) override { noteOf[v] = note; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        const float eg = isInputConnected (1) ? inputs[1][0] : 0.0f;
        const float mg = isInputConnected (2) ? inputs[2][0] : 0.0f;
        const float octaves = param (pEgInt) * 0.6f * eg
                            + param (pKbdTrack) * 0.01f * (float) (noteOf[v] - 60) / 12.0f
                            + param (pMgLevel) * 0.2f * mg;
        const double hz = param (pCutoff) * std::exp2 (octaves);
        const float reso = param (pResonance) * 0.1f, in = param (pInput) * 0.5f;

        for (size_t c = 0; c < 2; ++c)
            outputs[0][c] = filt[v][c].process (inputs[0][c] * in, hz, reso, sampleRate);
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);   // global lane = voice slot 0 (Kbd Track at middle C)
    }

private:
    vintagedsp::WarmLadderCore filt[aquanode::kMaxVoices][2];
    int noteOf[aquanode::kMaxVoices] {};   // set to middle C in reset()
};
