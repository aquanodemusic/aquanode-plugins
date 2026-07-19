#pragma once

#include "ModuleCore.h"

// Audio Thru - does nothing at all: audio in, the identical audio out. Exists
// purely for patch-map housekeeping: park one in the middle of a patch to
// bunch several cables into a single junction, or to give a long cable run a
// visual waypoint. Fan-in works like everywhere else in the graph (multiple
// cables into Audio In are summed), fan-out is free.
// Flexible lane, stateless: sitting inside a per-voice chain it stays
// per-voice, so it never forces an early voice sum.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class AudioThruModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        outputs[0] = inputs[0];
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        outputs[0] = inputs[0];
    }
};
