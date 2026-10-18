#pragma once

#include "ModuleCore.h"

// Gate - outputs 1 while a voice's note is held, 0 after release, with a
// tiny (~0.5 ms) anti-click ramp. Feed it through a Slew Limiter to build
// custom attack/release shapes.
// No inputs. Output: 0 = Modulation Out (unipolar).
class GateModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override { on[v] = false; level[v] = 0.0f; }
    void voiceNoteOn (int v, int, bool) override { on[v] = true; }
    void voiceNoteOff (int v) override { on[v] = false; }

    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        const float step = (float) (1.0 / (0.0005 * sampleRate));   // 0.5 ms ramp
        const float target = on[v] ? 1.0f : 0.0f;
        if (level[v] < target)      level[v] = juce::jmin (target, level[v] + step);
        else if (level[v] > target) level[v] = juce::jmax (target, level[v] - step);
        outputs[0][0] = level[v];
        outputs[0][1] = level[v];
    }

private:
    bool on [aquanode::kMaxVoices] { };
    float level [aquanode::kMaxVoices] { };
};
