#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Dual VCO - two analog-style oscillators and their mixer in one generator.
// VCO 1: Triangle / Saw / Pulse (with its PW knob) / Noise at 32' 16' 8' 4';
// VCO 2: Saw / Pulse 50% / Pulse 10% / Ring Mod at 16' 8' 4' 2', with its own
// +-12 semitone Pitch. Band-limited (PolyBLEP), with a slow analog drift.
//
// Unlike Oscillator there is no gate: the VCOs run for as long as the voice
// lives, as on an analog synth, so the VCA belongs after the filter (Volume with
// an Analog EG in Vol Mod). That is what lets a release tail ring through the
// filter instead of being cut at the oscillator.
// Inputs: 0 = Pitch In (semitones x Pitch Mod), 1 = Add Midi In. Output: 0 = Audio Out.
class DualVcoModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pWave1 = 0, pPw1, pScale1, pWave2, pPitch2, pScale2, pFine,
                      pLevel1, pLevel2, pAnalog, pPitchMod, pVoices, pGlide };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        glide.resetVoice (v);
        vco[v].reset ((uint32_t) v + 1u);
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        // a free-running analog VCO: every new note catches it at some phase
        if (! retrigger)
            vco[v].reset (++noteCounter * 2654435761u + (uint32_t) v);
    }

    void voiceNoteOff (int v) override { pool.noteOff (v, voiceLimit()); }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        const float gain = pool.nextGain (v, sampleRate);
        if (pool.isSilent (v))
        {
            outputs[0] = { 0.0f, 0.0f };
            return;
        }

        const double note = glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate)
                          + pitchBendSemitones();
        const double pitchMod = isInputConnected (0) ? inputs[0][0] * param (pPitchMod) : 0.0;

        const float y = vco[v].process (sampleRate, note, pitchMod,
                                        (int) param (pWave1), param (pPw1) * 0.1f, (int) param (pScale1),
                                        (int) param (pWave2), param (pPitch2), (int) param (pScale2),
                                        param (pFine), param (pLevel1) * 0.1f, param (pLevel2) * 0.1f,
                                        param (pAnalog) * 0.01f);
        outputs[0] = { y * gain, y * gain };
    }

private:
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    vintagedsp::DualVco vco[aquanode::kMaxVoices];
    uint32_t noteCounter { 0 };
};
