#pragma once

#include "ModuleCore.h"

// Velocity - outputs each voice's note-on velocity as a unipolar modulation
// signal [0..1]. Patch into an Oscillator's Env In for touch-sensitive
// levels, or into any Mod In for velocity-controlled brightness etc.
// No inputs. Output: 0 = Modulation Out.
class VelocityModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void reset() override
    {
        for (auto& v : vel) v = 0.0f;
    }

    void voiceReset (int v) override { vel[v] = 0.0f; }
    void voiceVelocity (int v, float velocity01) override { vel[v] = velocity01; }

    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        outputs[0][0] = vel[v];
        outputs[0][1] = vel[v];
    }

private:
    float vel [aquanode::kMaxVoices] { };
};
