#pragma once

#include "ModuleCore.h"

// Ladder Filter - Moog-style 4-pole ladder with drive and self-oscillating
// resonance. Flexible lane: per-voice when fed per-voice (one ladder per
// note), global otherwise. Modes: 12dB LP / 24dB LP / 12dB HP / 24dB HP.
// Inputs: 0 = Audio In, 1 = Mod In (cutoff). Output: 0 = Audio Out.
class LadderFilterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCutoff = 0, pResonance, pDrive, pModDepth, pOutputVolume, pMode };

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
        for (int c = 0; c < 2; ++c)
            s1[v][c] = s2[v][c] = s3[v][c] = s4[v][c] = 0.0f;
    }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);   // global lane = voice slot 0
    }

private:
    float s1[aquanode::kMaxVoices][2] {}, s2[aquanode::kMaxVoices][2] {};
    float s3[aquanode::kMaxVoices][2] {}, s4[aquanode::kMaxVoices][2] {};
};
