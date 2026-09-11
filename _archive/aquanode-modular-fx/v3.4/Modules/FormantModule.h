#pragma once

#include "ModuleCore.h"

// Formant - a singing oscillator. A buzzy glottal pulse feeds three parallel
// band-pass "formants"; where those peaks sit is what makes a vowel a vowel,
// so morphing them turns the note into A-E-I-O-U. Unlike the Vocoder this
// needs no modulator signal - it sings on its own.
//
// Vowel sweeps continuously through the five vowels (patch an LFO into Vowel
// In for talking pads). Size shifts every formant at once, which is really
// throat length: down for a large chest, up for a small child. Breath mixes
// noise into the glottal source for whispering.
// Inputs: 0 = Vowel In, 1 = Env In, 2 = Add Midi In. Output: 0 = Audio Out.
class FormantModule : public aquanode::SynthModule
{
public:
    static constexpr int kFormants = 3;
    static constexpr int kVowels = 5;   // A E I O U

    enum ParamIndex { pVolume = 0, pVowel, pSize, pBreath, pShape, pVowelMod, pVoices, pGlide };

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
        for (int f = 0; f < kFormants; ++f)
            low[v][f] = band[v][f] = 0.0f;
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
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;
    double phase [aquanode::kMaxVoices] {};
    float low  [aquanode::kMaxVoices][kFormants] {};
    float band [aquanode::kMaxVoices][kFormants] {};
    juce::Random random;
};
