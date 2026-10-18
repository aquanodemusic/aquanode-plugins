#pragma once

#include "ModuleCore.h"
#include "DXNativeFM.h"

// DX Pitch EG - the DX7's pitch envelope as a module (DX family).
// Four rates and four levels exactly as on the DX7: level 50 is the played
// pitch, the curve is the hardware's lopsided one (fine around the centre,
// up to about four octaves at the extremes), and the envelope starts every
// note from L4 and returns to L4 on release. Transpose adds a fixed offset,
// the voice's TRANSPOSE.
//
// Pitch Out is in the KeyTrack convention (semitones / 60): patch it into
// every DX Operator's Pitch In. Per voice.
// No inputs. Output: 0 = Pitch Out.
class DXPitchEGModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pR1 = 0, pR2, pR3, pR4, pTranspose, pL1, pL2, pL3, pL4 };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (int v = 0; v < aquanode::kMaxVoices; ++v) voiceReset (v); }

    void voiceReset (int v) override { env[v] = dxnative::PitchEnv(); ctrlLeft[v] = 0; current[v] = 0.0f; }
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override { env[v].keyOff(); }
    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 80; }

private:
    void configure (int v);

    static constexpr int kCtrlBlock = 16;
    dxnative::PitchEnv env[aquanode::kMaxVoices];
    int ctrlLeft[aquanode::kMaxVoices] {};
    float current[aquanode::kMaxVoices] {}, step[aquanode::kMaxVoices] {};
};
