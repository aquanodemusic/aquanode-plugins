#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Analog EG - an RC envelope as on vintage analog synths: Delay, Attack, Hold,
// Decay, Sustain, Release, with the convex charging attack and exponential
// decay and release of a capacitor rather than ADSR's straight lines. Rev Out
// is the inverted envelope (1 - env), an inverted EG output.
// PerVoice. No inputs. Outputs: 0 = Mod Out (0..1), 1 = Rev Out (1..0).
class AnalogEGModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pDelay = 0, pAttack, pHold, pDecay, pSustain, pRelease, pVelocity };

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

    void voiceReset (int v) override { eg[v].reset(); vel[v] = 1.0f; }
    // like ADSR: a (re)trigger always re-attacks, continuing from the current level
    void voiceNoteOn (int v, int, bool) override { eg[v].gateOn(); }
    void voiceVelocity (int v, float velocity01) override { vel[v] = velocity01; }
    void voiceNoteOff (int v) override { eg[v].gateOff(); }

    // the release is exponential: by 2x the Release time it is ~-80 dB
    double voiceTailSeconds() const override
    {
        return getParameterBase (pRelease) * 0.002 + 0.05;
    }

    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        const float e = eg[v].process (sampleRate, param (pDelay), param (pAttack), param (pHold),
                                       param (pDecay), param (pSustain), param (pRelease));
        const float velAmt = param (pVelocity) * 0.01f;
        const float out = e * (1.0f - velAmt + velAmt * vel[v]);
        outputs[0] = { out, out };
        outputs[1] = { 1.0f - out, 1.0f - out };
    }

private:
    vintagedsp::AnalogEG eg[aquanode::kMaxVoices];
    float vel[aquanode::kMaxVoices] {};
};
