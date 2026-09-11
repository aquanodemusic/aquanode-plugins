#pragma once

#include "ModuleCore.h"

// Bowed - the sustaining half of physical modelling. Pluck excites a string
// once and lets it die; this one keeps feeding energy in, so it sings for as
// long as you hold the note. The trick in both cases is the same delay line;
// what differs is the nonlinearity driving it.
//
//   Bow  - a friction curve: the bow grabs the string, the string slips free,
//          it grabs again. That stick-slip cycle is the whole violin.
//   Blow - a reed/jet table: pressure difference pushes the reed shut, which
//          cuts the pressure, which lets it open. Clarinet, roughly.
//
// Pressure is the expressive control (too little and it whispers, too much
// and it chokes); Position is where along the string you bow, which notches
// out harmonics; Damp is the loop low-pass, i.e. how bright the body is.
// Pairs beautifully with Voices = 1 and Glide.
// Inputs: 0 = Pressure In, 1 = Env In, 2 = Add Midi In. Output: 0 = Audio Out.
class BowedModule : public aquanode::SynthModule
{
public:
    static constexpr int maxDelaySamples = 4096;

    enum ParamIndex { pVolume = 0, pMode, pPressure, pPosition, pDamp, pPressMod, pVoices, pGlide };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    BowedModule()
    {
        for (auto& l : line)
            l.assign (maxDelaySamples, 0.0f);
    }

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
        if ((int) line[v].size() == maxDelaySamples)
            std::fill (line[v].begin(), line[v].end(), 0.0f);
        writeIndex[v] = 0;
        lpState[v] = 0.0f;
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        gate.noteOn (v);
        juce::ignoreUnused (retrigger);   // legato keeps the string ringing
    }

    void voiceNoteOff (int v) override
    {
        pool.noteOff (v, voiceLimit());
        gate.noteOff (v);
    }

    void voiceVelocity (int v, float velocity01) override { gate.setVelocity (v, velocity01); }
    double voiceTailSeconds() const override { return 0.4; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int v, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

private:
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;

    std::vector<float> line [aquanode::kMaxVoices];
    int writeIndex [aquanode::kMaxVoices] {};
    float lpState [aquanode::kMaxVoices] {};
};
