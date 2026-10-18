#pragma once

#include "ModuleCore.h"

// Wind - blown pipes, physically modelled: a delay line is the air column, and
// what drives it decides the instrument.
//
//   Flute      an air jet across an open pipe. The jet takes a moment to cross
//              the mouthpiece (its own short delay) and then flips into or out
//              of the pipe (a soft cubic curve); the pipe's reflection pulls it
//              back - so the note speaks with a breathy onset.
//   Pan Flute  the same jet, slower across a wider edge, over a duller pipe
//              with more breath noise around it - the hollow, airy pan pipe.
//   Clarinet   a reed over a cylindrical bore: the pressure difference across
//              the reed opens and closes it (a clipped straight line), and the
//              bore's half-length tuning gives the odd-harmonic, woody tone.
//
// Each instrument's tuning is measured: the jet and the loop's filters add
// delay of their own, so the pipe length for a note is worked back from it.
// Tone is a filter after the pipe, soft and dark to bright and edgy.
//
// Breath is the playing control: the note's breath rises over Attack and falls
// over Release (velocity sets how hard it starts), and Breath In adds to it by
// Breath Mod - patch Midi Controls' mod wheel or aftertouch there to play it
// like a wind controller. Noise is the air around the tone; Vibrato fades in
// after its Delay. Env In, patched, replaces the internal breath envelope.
// Per-voice generator. Inputs: 0 = Breath In, 1 = Env In, 2 = Add Midi In.
// Output: 0 = Audio Out.
class WindModule : public aquanode::SynthModule
{
public:
    static constexpr int kBoreSize = 8192;
    static constexpr int kJetSize = 4096;

    enum ParamIndex { pVolume = 0, pInstrument, pBreath, pTone, pNoise, pVibrato, pVibRate, pVibDelay,
                      pAttack, pRelease, pBreathMod, pVoices, pGlide };
    enum Instrument { flute = 0, panFlute, clarinet };

    WindModule();

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override;
    void voiceReset (int v) override;
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override;
    void voiceVelocity (int v, float velocity01) override { velocity[v] = velocity01; }
    double voiceTailSeconds() const override { return 0.5; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    struct Voice
    {
        std::vector<float> bore, jet;
        int boreWrite { 0 }, jetWrite { 0 };
        float loop { 0.0f }, lastBore { 0.0f };        // the bore's loss filter
        float dcIn { 0.0f }, dcOut { 0.0f };            // its DC blocker
        float outLp { 0.0f }, outDcIn { 0.0f }, outDcOut { 0.0f };
        float breath { 0.0f };                          // the breath envelope
        double vibPhase { 0.0 };
        double sinceOn { 0.0 };                         // seconds since the note began
        juce::uint32 noise { 0x1234567u };
    };

    float readDelay (const std::vector<float>& buf, int write, double delay, int size) const;

    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    bool gateOn[aquanode::kMaxVoices] {};
    float velocity[aquanode::kMaxVoices] {};
    Voice voices[aquanode::kMaxVoices];
};
