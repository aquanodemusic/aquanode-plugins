#pragma once

#include "ModuleCore.h"

// Unison - the supersaw. Up to nine copies of one wave, detuned around the
// note and panned across the stereo field. Note this is NOT what Midi Add
// does: Midi Add plays extra NOTES, this thickens a SINGLE note, which is why
// it stays in tune no matter what you play.
//
// Detune spacing follows the classic JP-8000 curve (the outer copies pull away
// faster than the inner ones), Blend sets centre-vs-sides level, and Spread is
// the stereo width. Each copy gets a random start phase so the stack never
// begins as one thin in-phase blip.
// Inputs: 0 = FM In, 1 = Env In, 2 = Add Midi In. Output: 0 = Audio Out.
class UnisonModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxUnison = 9;

    enum ParamIndex { pVolume = 0, pUnison, pDetune, pSpread, pBlend, pWaveform, pVoices, pGlide };

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
        for (int u = 0; u < kMaxUnison; ++u)
            phase[v][u] = random.nextDouble();
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        gate.noteOn (v);
        if (! retrigger)
            for (int u = 0; u < kMaxUnison; ++u)
                phase[v][u] = random.nextDouble();   // scattered phases = fat, not thin
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
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;
    double phase [aquanode::kMaxVoices][kMaxUnison] {};
    juce::Random random;
};
