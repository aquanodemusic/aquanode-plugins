#pragma once

#include "ModuleCore.h"

// Complex Osc - a West Coast (Buchla 259-style) voice. Two ideas, no filter:
// a modulation oscillator FMs the principal oscillator, and the result is then
// pushed through a wavefolder. Where a subtractive voice REMOVES harmonics
// from a rich wave, this one ADDS them to a sine by folding it back on itself,
// which is why Timbre sounds nothing like a filter sweep.
//
// Symmetry offsets the wave before folding, so the folds land asymmetrically
// and even harmonics appear. Mod Ratio at non-integer values gives the classic
// clangorous Buchla bell.
// Inputs: 0 = FM In, 1 = Timbre In, 2 = Env In, 3 = Add Midi In.
// Output: 0 = Audio Out.
class ComplexOscModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pTimbre, pSymmetry, pFolds, pModRatio, pModIndex, pTimbreMod, pVoices, pGlide };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        glide.resetVoice (v);
        gate.resetVoice (v);
        phase[v] = modPhase[v] = 0.0;
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        gate.noteOn (v);
        if (! retrigger)
            phase[v] = modPhase[v] = 0.0;
    }

    void voiceNoteOff (int v) override
    {
        pool.noteOff (v, voiceLimit());
        gate.noteOff (v);
    }

    void voiceVelocity (int v, float velocity01) override { gate.setVelocity (v, velocity01); }
    double voiceTailSeconds() const override { return 0.02; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int v, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

private:
    // classic reflecting wavefolder: anything past +/-1 bounces back inside
    static float fold (float x, int stages)
    {
        for (int i = 0; i < stages * 4 && (x > 1.0f || x < -1.0f); ++i)
            x = x > 1.0f ? 2.0f - x : -2.0f - x;
        return juce::jlimit (-1.0f, 1.0f, x);
    }

    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;
    double phase [aquanode::kMaxVoices] {};
    double modPhase [aquanode::kMaxVoices] {};
};
