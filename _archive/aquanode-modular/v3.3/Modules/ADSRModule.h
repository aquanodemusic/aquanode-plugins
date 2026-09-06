#pragma once

#include "ModuleCore.h"

// ADSR - per-voice envelope generator. The engine calls it once per active
// voice; its Modulation Out is that voice's OWN envelope, so patching it into
// an Oscillator's Env In gives true per-note amplitude shaping (G2-style).
// Output is always unipolar [0, 1].
// No inputs. Output: 0 = Modulation Out.
class ADSRModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pAttack = 0, pDecay, pSustain, pRelease };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

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
        stage[v] = Stage::Idle;
        level[v] = 0.0f;
        releaseStep[v] = 0.0f;
    }

    void voiceNoteOn (int voice, int note, bool retrigger) override;
    void voiceNoteOff (int voice) override;
    void processVoiceSample (int voice, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override;

    // the engine keeps voices alive for the longest tail in the patch
    double voiceTailSeconds() const override { return param (pRelease) * 0.001; }

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    Stage stage [aquanode::kMaxVoices] {};
    float level [aquanode::kMaxVoices] {};
    float releaseStep [aquanode::kMaxVoices] {};
};
