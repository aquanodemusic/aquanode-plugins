#pragma once

#include "ModuleCore.h"

// FORKED for Aqua Vibrio: added Poles, Slope and per-side Shape so the Virus
// Filter Bank's own controls have something to drive. Upstream Aquanode has
// the same module without those four parameters.
//
// Formant Filter - the Formant module's vowel bank, aimed at whatever you
// patch in instead of its own glottal buzz. Three parallel band-passes sit on
// the measured formant peaks of A E I O U, and Vowel morphs continuously
// between them, so a saw pad turns into a choir and a drum loop starts
// talking.
//
// Size shifts every formant at once - really throat length: down for a chest,
// up for a child. Resonance is how sharply the peaks ring: low is a gentle
// vowel colour, high is a vocoder-ish singing resonance. Every knob is
// modulatable, and Vowel In takes a mod cable directly for talking pads.
//
// Flexible lane: inside a per-voice chain each voice gets its own filter
// state and can be given its own vowel; in a global chain it runs once.
// Inputs: 0 = Audio In, 1 = Vowel In. Output: 0 = Audio Out.
class FormantFilterModule : public aquanode::SynthModule
{
public:
    static constexpr int kFormants = 3;
    static constexpr int kVowels = 5;   // A E I O U

    enum ParamIndex { pVowel = 0, pSize, pResonance, pVowelMod, pDryWet, pLevel,
                      pPoles, pSlope, pShapeL, pShapeR };

    // How many band-pass stages each formant may cascade. Four poles is a
    // 24 dB skirt around the peak, which is where a vowel stops colouring a
    // sound and starts screaming - the reason this control exists.
    static constexpr int kMaxPoles = 4;

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < kNumLanes; ++i)
            clearLane (i);
    }

    void voiceReset (int voice) override { clearLane (voice); }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        renderLane (voice, inputs, outputs);
    }

    void processSample (const aquanode::StereoFrame* inputs,
                        aquanode::StereoFrame* outputs) override
    {
        renderLane (aquanode::kMaxVoices, inputs, outputs);   // the global lane
    }

private:
    static constexpr int kNumLanes = aquanode::kMaxVoices + 1;

    void renderLane (int lane, const aquanode::StereoFrame* inputs,
                     aquanode::StereoFrame* outputs);

    void clearLane (int lane)
    {
        if (lane < 0 || lane >= kNumLanes)
            return;
        for (int ch = 0; ch < 2; ++ch)
            for (int f = 0; f < kFormants; ++f)
                for (int p = 0; p < kMaxPoles; ++p)
                    low[lane][ch][f][p] = band[lane][ch][f][p] = 0.0f;
    }

    // Chamberlin state-variable band-pass state, per lane / channel / formant
    float low  [kNumLanes][2][kFormants][kMaxPoles] {};
    float band [kNumLanes][2][kFormants][kMaxPoles] {};
};
