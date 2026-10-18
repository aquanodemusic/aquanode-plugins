#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Dual Filter - a 12 dB high-pass then a 12 dB
// low-pass in series, each with Cutoff and Peak. Peak drives a soft-clipped
// resonance path, so high settings scream rather than explode, and from ~9.2
// up the filter sings on its own. Each filter has two CV inputs with their own
// depth knobs: MG/T.Ext and EG2/Ext (10 = 3 octaves per
// unit of CV). An external CV patched in takes over the EG2/Ext
// knob - patch it the same way here.
// Flexible lane: one filter pair per note when fed per-voice.
// Inputs: 0 Audio In, 1 HPF MG, 2 HPF Ext, 3 LPF MG, 4 LPF Ext. Output: 0 Audio Out.
class DualFilterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pHpfCutoff = 0, pHpfPeak, pHpfMg, pHpfExt,
                      pLpfCutoff, pLpfPeak, pLpfMg, pLpfExt, pOutput };

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
        for (auto& f : filt[v])
            f.reset();
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        constexpr float octPerUnit = 3.0f * 0.1f;   // knob 10 = 3 octaves per unit CV
        const float hpfOct = octPerUnit * (param (pHpfMg) * cv (inputs, 1) + param (pHpfExt) * cv (inputs, 2));
        const float lpfOct = octPerUnit * (param (pLpfMg) * cv (inputs, 3) + param (pLpfExt) * cv (inputs, 4));
        const double hpfHz = param (pHpfCutoff) * std::exp2 (hpfOct);
        const double lpfHz = param (pLpfCutoff) * std::exp2 (lpfOct);
        const float hpfPeak = param (pHpfPeak) * 0.1f, lpfPeak = param (pLpfPeak) * 0.1f;
        const float out = param (pOutput);

        for (size_t c = 0; c < 2; ++c)
            outputs[0][c] = filt[v][c].process (inputs[0][c], hpfHz, hpfPeak, lpfHz, lpfPeak, sampleRate) * out;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);   // global lane = voice slot 0
    }

private:
    float cv (const aquanode::StereoFrame* inputs, int i) const
    {
        return isInputConnected (i) ? inputs[i][0] : 0.0f;
    }

    vintagedsp::DualFilterCore filt[aquanode::kMaxVoices][2];
};
