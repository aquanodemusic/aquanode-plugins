#pragma once

#include "ModuleCore.h"

// AquaFilter - the filter core of Laurent's standalone AquaFilter plugin,
// stripped to a module: a Huovilainen Moog ladder (tanh in every stage) for
// the lowpass modes plus a Simper SVF for bandpass / highpass. LP24+ runs the
// ladder output through an extra saturation stage. Drive saturates BEFORE the
// filter (with gain compensation), and Res Curve picks how the resonance knob
// maps - Quadratic reaches screaming resonance earlier, Cubic keeps the lower
// half of the knob tamer. The plugin's built-in chorus and filter envelope
// are gone: patch a Chorus, ADSR or Env Follow into Mod In instead.
// Flexible lane: per-voice when fed per-voice, global otherwise.
// Inputs: 0 = Audio In, 1 = Mod In (cutoff). Output: 0 = Audio Out.
class AquaFilterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCutoff = 0, pResonance, pDrive, pModDepth, pType, pResCurve };

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
        {
            for (auto& s : ladder[v][c].y)
                s = 0.0f;
            svf[v][c].ic1 = svf[v][c].ic2 = 0.0f;
        }
    }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);   // global lane = voice slot 0
    }

private:
    // fast tanh approximation shared by drive and the ladder stages
    static float tanhA (float x)
    {
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // resonance knob (0..1) -> feedback shaping, per the plugin's two curves
    static float shapeResonance (float resonance, int curve)
    {
        if (curve == 0)   // Quadratic
            return juce::jlimit (0.0f, 0.992f, resonance * resonance * 0.6f + resonance * 0.4f);

        const float r2 = resonance * resonance;   // Cubic
        return juce::jlimit (0.0f, 0.995f, r2 * resonance * 0.80f + r2 * 0.12f + resonance * 0.08f);
    }

    struct LadderState { float y[4] {}; };
    struct SvfState    { float ic1 {}, ic2 {}; };

    float ladderProcess (LadderState& s, float x, float tune, float res4) const;

    LadderState ladder [aquanode::kMaxVoices][2];
    SvfState    svf    [aquanode::kMaxVoices][2];
};
