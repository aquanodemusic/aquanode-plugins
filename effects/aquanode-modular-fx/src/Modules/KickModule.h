#pragma once

#include "ModuleCore.h"

// Kick - 808-style kick voice: sine with an exponential pitch-drop envelope,
// a click transient, and tanh drive. Fires on rising edges at Trig In
// (Step Seq / Euclid / Clock gates) AND on MIDI note-on when played from the
// keyboard (Flexible: per-voice with notes, global when sequencer-driven).
// Input: 0 = Trig In. Output: 0 = Audio Out.
class KickModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTune = 0, pDrop, pPitchDecay, pDecay, pClick, pDrive };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

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
        ampEnv[v] = pitchEnv[v] = 0.0f;
        phase[v] = 0.0;
        clickSamples[v] = 0;
        lastTrig[v] = 0.0f;
    }

    void voiceNoteOn (int v, int, bool) override { pool.noteOn (v, voiceLimit()); trigger (v); }

    double voiceTailSeconds() const override { return aquanode::decayTailSeconds (juce::jmax (5.0f, param (pDecay))) + 0.05; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int voice, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
        renderGlobalMidiHits (inputs, outputs, 0);   // notes on Midi In, see ModuleCore.h
    }

    // global lane: notes arriving on Midi In play as overlapping hits
    bool acceptsGlobalMidiNotes() const override { return true; }

private:
    aquanode::ModuleVoicePool pool;

    void trigger (int v)
    {
        ampEnv[v] = 1.0f;
        pitchEnv[v] = 1.0f;
        phase[v] = 0.0;
        clickSamples[v] = (int) (0.0015 * sampleRate);   // 1.5 ms click burst
    }

    float ampEnv   [aquanode::kMaxVoices] {};
    float pitchEnv [aquanode::kMaxVoices] {};
    double phase   [aquanode::kMaxVoices] {};
    int clickSamples [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    juce::Random random;
};
