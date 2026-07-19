#pragma once

#include "ModuleCore.h"

// LFO - per-voice low-frequency oscillator, phase-retriggered at note-on so
// every note gets the same modulation shape from its start (G2-style poly LFO).
// No inputs. Output: 0 = Modulation Out.
class LFOModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pWaveform, pOffset, pLevel };

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
        phase[v] = 0.0;
        shValue[v] = 0.0f;
    }

    void voiceNoteOn (int v, int, bool) override
    {
        phase[v] = 0.0;
        shValue[v] = random.nextFloat() * 2.0f - 1.0f;
    }

    void processVoiceSample (int voice, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override;

private:
    double phase [aquanode::kMaxVoices] {};
    float shValue [aquanode::kMaxVoices] {};
    juce::Random random;
};
