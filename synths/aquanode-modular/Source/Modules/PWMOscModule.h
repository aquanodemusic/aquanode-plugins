#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// PWM Osc - a polysynth VCO: Saw, PW or PWM at 16' 8' 4', and the square
// sub-oscillator one or two octaves down (a flip-flop divider, so it stays
// locked to the main oscillator). In PW the PW/PWM knob sets the width; in PWM
// it sets the depth of the built-in PWM LFO, whose speed has its own knob.
// Like Dual VCO it never gates: put the VCA after the filter.
// Inputs: 0 = Pitch In (semitones x Pitch Mod), 1 = Add Midi In. Output: 0 = Audio Out.
class PWMOscModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pOctave = 0, pWave, pPw, pPwmSpeed, pSub, pSubLevel, pTune,
                      pLevel, pAnalog, pPitchMod, pVoices, pGlide };

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
        osc[v].reset ((uint32_t) v + 1u);
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        if (! retrigger)
            osc[v].reset (++noteCounter * 2246822519u + (uint32_t) v);
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
                          + pitchBendSemitones() + param (pTune) * 0.01;
        const double pitchMod = isInputConnected (0) ? inputs[0][0] * param (pPitchMod) : 0.0;

        const float y = osc[v].process (sampleRate, note, pitchMod, (int) param (pOctave), (int) param (pWave),
                                        param (pPw) * 0.1f, param (pPwmSpeed), (int) param (pSub),
                                        param (pSubLevel), param (pAnalog) * 0.01f)
                      * param (pLevel);
        outputs[0] = { y * gain, y * gain };
    }

private:
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    vintagedsp::PwmOsc osc[aquanode::kMaxVoices];
    uint32_t noteCounter { 0 };
};
