#pragma once

#include "ModuleCore.h"

// Hats - 606/808-style metallic percussion: six detuned square oscillators
// summed, band-passed, high-passed and shaped by a snappy exponential decay.
// Short Decay = closed hat, long = open hat / ride-ish. Metal spreads the
// oscillator ratios for clangier tones. Trig In edges and MIDI note-on fire it.
// Input: 0 = Trig In. Output: 0 = Audio Out.
class HatsModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumOscs = 6;
    enum ParamIndex { pTune = 0, pDecay, pTone, pMetal };

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
        env[v] = 0.0f;
        lastTrig[v] = 0.0f;
        bpLow[v] = bpBand[v] = 0.0f;
        hpState[v] = hpPrevIn[v] = 0.0f;
        for (int o = 0; o < kNumOscs; ++o)
            phase[v][o] = o * 0.13;   // decorrelated start phases
    }

    void voiceNoteOn (int v, int, bool) override { pool.noteOn (v, voiceLimit()); env[v] = 1.0f; }

    double voiceTailSeconds() const override { return param (pDecay) * 0.001 + 0.1; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int voice, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    aquanode::ModuleVoicePool pool;
    float env [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    double phase [aquanode::kMaxVoices][kNumOscs] {};
    float bpLow [aquanode::kMaxVoices] {};
    float bpBand [aquanode::kMaxVoices] {};
    float hpState [aquanode::kMaxVoices] {};
    float hpPrevIn [aquanode::kMaxVoices] {};
};
