#pragma once

#include "ModuleCore.h"

// SVF Filter - Chamberlin state-variable filter, LP/BP/HP. Flexible lane:
// when fed by a per-voice source it runs one filter PER VOICE (each note gets
// its own filter, per-voice cutoff modulation included); in a global chain
// (e.g. after a reverb) it runs once, using voice slot 0's state.
// Inputs: 0 = Audio In, 1 = Mod In (cutoff). Output: 0 = Audio Out.
class SVFFilterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCutoff = 0, pResonance, pMode, pModDepth };

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
            low[v][c] = band[v][c] = 0.0f;
    }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);   // global lane = voice slot 0
    }

private:
    float low [aquanode::kMaxVoices][2] {};
    float band[aquanode::kMaxVoices][2] {};
};
