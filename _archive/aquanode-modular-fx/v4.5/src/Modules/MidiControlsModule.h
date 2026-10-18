#pragma once

#include "ModuleCore.h"

// Midi Controls - the non-note side of MIDI as modulation cables: pitch bend,
// mod wheel, aftertouch, any CC by number, and the sustain pedal. Patch them
// into knobs (mod wheel -> filter cutoff, aftertouch -> vibrato depth...) or
// into any modulation input.
//
// Two things work without this module at all: the sustain pedal holds played
// keys, and pitch bend bends every pitched generator, by 2 semitones. Bend
// Range here changes that range for the whole patch.
//
// Outputs: 0 = Bend (-1..1), 1 = Mod Wheel, 2 = Aftertouch, 3 = CC, 4 = Sustain
// (all 0..1 unless noted). A few milliseconds of smoothing take the zipper out
// of 7-bit controller steps.
class MidiControlsModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCcNumber = 0, pBendRange, pSmooth };
    static constexpr int kOutputs = 5;

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& s : smoothed) s = 0.0f; }

    float bendRangeSetting() const override { return param (pBendRange); }

    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        float target[kOutputs] {};
        if (midiControls != nullptr)
        {
            target[0] = midiControls->bend;
            target[1] = midiControls->modWheel;
            target[2] = midiControls->aftertouch;
            target[3] = midiControls->cc[juce::jlimit (0, 127, (int) param (pCcNumber))];
            target[4] = midiControls->sustain ? 1.0f : 0.0f;
        }

        const float ms = juce::jmax (0.0f, param (pSmooth));
        const float coeff = ms < 0.05f ? 1.0f
                                       : 1.0f - std::exp ((float) (-1.0 / (ms * 0.001 * sampleRate)));
        for (int i = 0; i < kOutputs; ++i)
        {
            smoothed[i] += coeff * (target[i] - smoothed[i]);
            outputs[i] = { smoothed[i], smoothed[i] };
        }
    }

private:
    float smoothed[kOutputs] {};
};
