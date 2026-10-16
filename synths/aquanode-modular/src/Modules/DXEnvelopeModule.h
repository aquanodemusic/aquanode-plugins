#pragma once

#include "ModuleCore.h"
#include "DXNativeFM.h"

// DX Envelope - a DX7 operator's envelope on its own (DX family):
// the eight-knob rate/level envelope with the DX7's log-domain segments, its
// accelerated attack, rate scaling and velocity sensitivity, and the output
// level's 0.75 dB steps. Env Out is the resulting gain, 0..1, so it drops
// into any generator's Env In (or a Volume's Mod In) to give it a DX-style
// envelope: plucks that fall like an E.Piano, swells that never sound linear.
// Per voice; its tail follows R4.
// No inputs. Output: 0 = Env Out.
class DXEnvelopeModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pR1 = 0, pR2, pR3, pR4, pL1, pL2, pL3, pL4, pLevel, pRateScale, pVelSens };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (int v = 0; v < aquanode::kMaxVoices; ++v) voiceReset (v); }

    void voiceReset (int v) override;
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override { env[v].keyOff(); }
    void voiceVelocity (int v, float velocity01) override { velocity[v] = juce::jlimit (1, 127, juce::roundToInt (velocity01 * 127.0f)); }
    double voiceTailSeconds() const override;
    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 86; }

private:
    static constexpr int kCtrlBlock = 32;

    dxnative::OperatorEnv env[aquanode::kMaxVoices];
    float gain[aquanode::kMaxVoices] {}, gainStep[aquanode::kMaxVoices] {};
    int ctrlLeft[aquanode::kMaxVoices] {};
    int noteOf[aquanode::kMaxVoices] {};
    int velocity[aquanode::kMaxVoices] {};
};
