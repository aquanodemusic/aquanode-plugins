#pragma once

#include "ModuleCore.h"

// Clap - the 808 clap trick: instead of one noise burst, three short bursts
// fire a few milliseconds apart (that stutter is what the ear reads as many
// hands), followed by a longer diffuse tail. Spread sets the gap between the
// bursts, Tone the band-pass they all share.
// Input: 0 = Trig In. Output: 0 = Audio Out.
class ClapModule : public aquanode::SynthModule
{
public:
    static constexpr int kBursts = 3;

    enum ParamIndex { pTone = 0, pSpread, pBurstDecay, pTailDecay, pTailMix, pVoices };

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
        burstEnv[v] = tailEnv[v] = 0.0f;
        burstsLeft[v] = 0;
        burstTimer[v] = 0.0;
        bpLow[v] = bpBand[v] = 0.0f;
        lastTrig[v] = 0.0f;
        running[v] = false;
    }

    void voiceNoteOn (int v, int, bool) override
    {
        pool.noteOn (v, voiceLimit());
        trigger (v);
    }

    double voiceTailSeconds() const override { return param (pTailDecay) * 0.001 + 0.15; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int v, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

private:
    void trigger (int v)
    {
        burstEnv[v] = 1.0f;
        burstsLeft[v] = kBursts;      // this one plus two more
        burstTimer[v] = 0.0;
        tailEnv[v] = 0.0f;
        running[v] = true;
    }

    aquanode::ModuleVoicePool pool;
    float burstEnv [aquanode::kMaxVoices] {};
    float tailEnv  [aquanode::kMaxVoices] {};
    int burstsLeft [aquanode::kMaxVoices] {};
    double burstTimer [aquanode::kMaxVoices] {};
    float bpLow [aquanode::kMaxVoices] {}, bpBand [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    bool running [aquanode::kMaxVoices] {};
    juce::Random random;
};
