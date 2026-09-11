#pragma once

#include "ModuleCore.h"

// KeyTrack - outputs each voice's note pitch as a modulation signal, scaled
// so that patching it into a filter's Cutoff Mod at 100% Mod Depth gives
// exact 1-octave-per-octave keyboard tracking (out = (note - center) / 60,
// and the filters' exponential curve is 2^(mod * 5) octaves).
// No inputs. Output: 0 = Modulation Out (bipolar).
class KeyTrackModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCenter = 0 };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void reset() override
    {
        for (auto& n : noteOfVoice) n = 60;
    }

    void voiceReset (int v) override { noteOfVoice[v] = 60; }
    void voiceNoteOn (int v, int note, bool) override { noteOfVoice[v] = note; }

    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        const float out = ((float) noteOfVoice[v] - param (pCenter)) / 60.0f;
        outputs[0][0] = out;
        outputs[0][1] = out;
    }

private:
    int noteOfVoice [aquanode::kMaxVoices] { };
};
