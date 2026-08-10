#pragma once

#include "ModuleCore.h"

// Phase Distort - Casio CZ-style phase distortion. A cosine is read with a
// warped phase index: bend the index and the SHAPE changes while the pitch
// stays put, which is why the CZ could sound like a filter sweep without
// having a filter. DCW ("depth of cyclic waveform") is the whole instrument -
// at 0 every mode is a plain cosine, and opening it up morphs toward the
// named shape. The three Reso modes are the CZ's signature: a sine locked to
// a multiple of the pitch, windowed by the fundamental, giving a swept
// formant peak.
//
// Faithful in spirit rather than a bit-exact CZ clone.
// Inputs: 0 = DCW In (Mod), 1 = Env In (Mod), 2 = Add Midi In.
// Output: 0 = Audio Out.
class PhaseDistortModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pDcw, pMode, pDcwMod, pVoices, pGlide };
    enum Mode { mSaw = 0, mSquare, mPulse, mDblSine, mSawPulse, mReso1, mReso2, mReso3 };

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
        phase[v] = 0.0;
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        gate.noteOn (v);
        if (! retrigger)
            phase[v] = 0.0;
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
    static double warpSaw (double p, double dcw);
    static float shape (int mode, double p, double dcw);

    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;
    double phase [aquanode::kMaxVoices] {};
};
