#pragma once

#include "ModuleCore.h"

// Snare - the missing piece of the Kick/Hats kit: two detuned sine "shells"
// giving the drum its pitch, plus a band-passed noise burst for the snare
// wires, each with its own decay. Snap sets the balance of wires to shell,
// Tone opens the noise band up from a dry thud to a bright crack.
// Fires on rising edges at Trig In and on MIDI note-on, like the other drums.
// Input: 0 = Trig In. Output: 0 = Audio Out.
class SnareModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTune = 0, pShellDecay, pNoiseDecay, pSnap, pTone, pDrive, pVoices };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        shellEnv[v] = noiseEnv[v] = 0.0f;
        phaseA[v] = phaseB[v] = 0.0;
        bpLow[v] = bpBand[v] = 0.0f;
        lastTrig[v] = 0.0f;
    }

    void voiceNoteOn (int v, int, bool) override
    {
        pool.noteOn (v, voiceLimit());
        trigger (v);
    }

    double voiceTailSeconds() const override
    {
        return aquanode::decayTailSeconds (juce::jmax (5.0f, param (pShellDecay), param (pNoiseDecay))) + 0.05;
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int v, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
        renderGlobalMidiHits (inputs, outputs, 0);   // notes on Midi In, see ModuleCore.h
    }

    // global lane: notes arriving on Midi In play as overlapping hits
    bool acceptsGlobalMidiNotes() const override { return true; }

private:
    void trigger (int v)
    {
        shellEnv[v] = 1.0f;
        noiseEnv[v] = 1.0f;
        phaseA[v] = phaseB[v] = 0.0;
    }

    aquanode::ModuleVoicePool pool;
    float shellEnv [aquanode::kMaxVoices] {};
    float noiseEnv [aquanode::kMaxVoices] {};
    double phaseA [aquanode::kMaxVoices] {}, phaseB [aquanode::kMaxVoices] {};
    float bpLow [aquanode::kMaxVoices] {}, bpBand [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    juce::Random random;
};
