#pragma once

#include "ModuleCore.h"

// Quantize - snaps a pitch-scaled modulation signal (semitones / 60, the
// KeyTrack / Step Seq / Pluck convention) to the nearest note of a scale.
// The classic use: random S&H -> Quantize -> Pluck Pitch In = instant melody.
// Stateless; Flexible so it quantizes per-voice signals per voice.
// Input: 0 = Mod In. Output: 0 = Mod Out.
class QuantizerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pScale = 0, pRoot };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        static const bool scaleMasks[8][12] = {
            { 1,1,1,1,1,1,1,1,1,1,1,1 },   // Chromatic
            { 1,0,1,0,1,1,0,1,0,1,0,1 },   // Major
            { 1,0,1,1,0,1,0,1,1,0,1,0 },   // Minor
            { 1,0,1,1,0,1,0,1,1,0,0,1 },   // Harmonic Minor
            { 1,0,1,0,1,0,0,1,0,1,0,0 },   // Major Pent
            { 1,0,0,1,0,1,0,1,0,0,1,0 },   // Minor Pent
            { 1,0,0,1,0,1,1,1,0,0,1,0 },   // Blues
            { 1,0,0,0,0,0,0,1,0,0,0,0 }    // Fifths
        };

        const int scale = juce::jlimit (0, 7, (int) param (pScale));
        const int root = juce::jlimit (0, 11, (int) param (pRoot));

        const float semis = inputs[0][0] * 60.0f;
        const int nearest = (int) std::round (semis);

        // search outward for the closest allowed pitch class
        int best = nearest;
        for (int offset = 0; offset <= 6; ++offset)
        {
            const int up = nearest + offset;
            const int dn = nearest - offset;
            if (scaleMasks[scale][(((up - root) % 12) + 12) % 12]) { best = up; break; }
            if (scaleMasks[scale][(((dn - root) % 12) + 12) % 12]) { best = dn; break; }
        }

        const float out = (float) best / 60.0f;
        outputs[0][0] = out;
        outputs[0][1] = out;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
